/*
 * wiLink
 * Copyright (C) 2009-2010 Bolloré telecom
 * See AUTHORS file for a full list of contributors.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTime>
#include <QTimer>

#include "qxmpp/QXmppShareIq.h"
#include "qxmpp/QXmppUtils.h"

#include "chat_shares_database.h"

Q_DECLARE_METATYPE(QXmppShareSearchIq)

#define SEARCH_MAX_TIME 5000
#define HASH_MAX_SIZE 1024 * 1024

/** Fingerprint a file.
 */
static QByteArray hashFile(const QString &path)
{
    QFile fp(path);

    if (!fp.open(QIODevice::ReadOnly))
        return QByteArray();

    QCryptographicHash hasher(QCryptographicHash::Md5);
    char buffer[16384];
    int hashed = 0;
    while (fp.bytesAvailable() && hashed < HASH_MAX_SIZE)
    {
        int len = fp.read(buffer, sizeof(buffer));
        if (len < 0)
            return QByteArray();
        hasher.addData(buffer, len);
        hashed += len;
    }
    return hasher.result();
}

ChatSharesDatabase::ChatSharesDatabase(const QString &path, QObject *parent)
    : QObject(parent), sharesDir(path)
{
    // prepare database
    sharesDb = QSqlDatabase::addDatabase("QSQLITE");
    sharesDb.setDatabaseName(":memory:");
    Q_ASSERT(sharesDb.open());
    sharesDb.exec("CREATE TABLE files (path TEXT, date DATETIME, size INT, hash VARCHAR(32))");
    sharesDb.exec("CREATE UNIQUE INDEX files_path ON files (path)");

    // start indexing
    indexTimer = new QTimer(this);
    indexTimer->setInterval(60 * 60 * 1000); // 1 hour
    indexTimer->setSingleShot(true);
    connect(indexTimer, SIGNAL(timeout()), this, SLOT(index()));
    index();
}

QSqlDatabase ChatSharesDatabase::database() const
{
    return sharesDb;
}

QDir ChatSharesDatabase::directory() const
{
    return sharesDir;
}

/** Handle a get request.
  */
void ChatSharesDatabase::get(const QXmppShareGetIq &requestIq)
{
    QThread *worker = new GetThread(this, requestIq);
    connect(worker, SIGNAL(logMessage(QXmppLogger::MessageType,QString)),
            this, SIGNAL(logMessage(QXmppLogger::MessageType,QString)));
    connect(worker, SIGNAL(finished()),
            worker, SLOT(deleteLater()));
    connect(worker, SIGNAL(getFinished(QXmppShareGetIq, QXmppTransferFileInfo)),
            this, SIGNAL(getFinished(QXmppShareGetIq, QXmppTransferFileInfo)));
    worker->start();
 }

/** Update database of available files.
 */
void ChatSharesDatabase::index()
{
    // start indexing
    QThread *worker = new IndexThread(this);
    connect(worker, SIGNAL(logMessage(QXmppLogger::MessageType,QString)),
        this, SIGNAL(logMessage(QXmppLogger::MessageType,QString)));
    connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(worker, SIGNAL(finished()), indexTimer, SLOT(start()));
    worker->start();
}

QString ChatSharesDatabase::locate(const QString &publishId)
{
    QSqlQuery query("SELECT path FROM files WHERE path = :path", sharesDb);
    query.bindValue(":path", publishId);
    query.exec();
    if (!query.next())
        return QString();

    // check file is still readable
    const QString path = query.value(0).toString();
    QFileInfo info(sharesDir.filePath(path));
    if (!info.isReadable())
    {
        deleteFile(path);
        return QString();
    }
    return sharesDir.filePath(path);
}

/** Handle a search request.
 */
void ChatSharesDatabase::search(const QXmppShareSearchIq &requestIq)
{
    QThread *worker = new SearchThread(this, requestIq);
    connect(worker, SIGNAL(logMessage(QXmppLogger::MessageType,QString)),
        this, SIGNAL(logMessage(QXmppLogger::MessageType,QString)));
    connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(worker, SIGNAL(searchFinished(const QXmppShareSearchIq&)), this, SIGNAL(searchFinished(const QXmppShareSearchIq&)));
    worker->start();
}

void ChatSharesDatabase::deleteFile(const QString &path)
{
    QSqlQuery deleteQuery("DELETE FROM files WHERE path = :path", sharesDb);
    deleteQuery.bindValue(":path", path);
    deleteQuery.exec();
}

ChatSharesThread::ChatSharesThread(ChatSharesDatabase *database)
    : QThread(database), sharesDatabase(database)
{
};

GetThread::GetThread(ChatSharesDatabase *database, const QXmppShareGetIq &request)
    : ChatSharesThread(database), requestIq(request)
{
};

void GetThread::run()
{
    QXmppShareGetIq responseIq;
    responseIq.setId(requestIq.id());
    responseIq.setTo(requestIq.from());
    responseIq.setType(QXmppIq::Result);

    QXmppTransferFileInfo fileInfo;
    QString filePath = sharesDatabase->locate(requestIq.node());
    if (filePath.isEmpty())
    {
        logMessage(QXmppLogger::WarningMessage, "Could not find local file " + requestIq.node());
        QXmppStanza::Error error(QXmppStanza::Error::Cancel, QXmppStanza::Error::ItemNotFound);
        responseIq.setError(error);
        responseIq.setType(QXmppIq::Error);
        emit getFinished(responseIq, fileInfo);
        return;
    }

    logMessage(QXmppLogger::InformationMessage, "Sending file " + filePath);
    fileInfo.setName(filePath);
    responseIq.setSid(generateStanzaHash());
    emit getFinished(responseIq, fileInfo);
};

IndexThread::IndexThread(ChatSharesDatabase *database)
    : ChatSharesThread(database), scanAdded(0), scanUpdated(0)
{
};

void IndexThread::run()
{
    QDir sharesDir = sharesDatabase->directory();
    logMessage(QXmppLogger::DebugMessage, "Scan started for " + sharesDir.path());

    // store existing entries
    QSqlQuery query("SELECT path FROM files", sharesDatabase->database());
    query.exec();
    while (query.next())
        scanOld.insert(query.value(0).toString(), 1);

    // perform scan
    QTime t;
    t.start();
    scanDir(sharesDir);

    // remove obsolete entries
    foreach (const QString &path, scanOld.keys())
        sharesDatabase->deleteFile(path);
    logMessage(QXmppLogger::DebugMessage, QString("Scan completed in %1 s ( %2 added, %3 updated, %4 removed )")
               .arg(QString::number(double(t.elapsed()) / 1000.0))
               .arg(scanAdded)
               .arg(scanUpdated)
               .arg(scanOld.size()));
}

void IndexThread::scanDir(const QDir &dir)
{
    QSqlDatabase sharesDb = sharesDatabase->database();
    QDir sharesDir = sharesDatabase->directory();
    QSqlQuery addQuery("INSERT INTO files (path, date, size) "
                       "VALUES(:path, :date, :size)", sharesDb);
    QSqlQuery updateQuery("UPDATE files SET date = :date, size = :size WHERE path = :path", sharesDb);

    foreach (const QFileInfo &info, dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable))
    {
        if (info.isDir())
        {
            scanDir(QDir(info.filePath()));
        } else {
            const QString relativePath = sharesDir.relativeFilePath(info.filePath());
            if (scanOld.remove(relativePath))
            {
                updateQuery.bindValue(":path", relativePath);
                updateQuery.bindValue(":date", info.lastModified());
                updateQuery.bindValue(":size", info.size());
                updateQuery.exec();
                scanUpdated++;
            } else {
                addQuery.bindValue(":path", relativePath);
                addQuery.bindValue(":date", info.lastModified());
                addQuery.bindValue(":size", info.size());
                addQuery.exec();
                scanAdded++;
            }
        }
    }
}

SearchThread::SearchThread(ChatSharesDatabase *database, const QXmppShareSearchIq &request)
    : ChatSharesThread(database), requestIq(request)
{
};

bool SearchThread::updateFile(QXmppShareItem &file, const QSqlQuery &selectQuery, bool updateHash)
{
    QDir sharesDir = sharesDatabase->directory();

    const QString path = selectQuery.value(0).toString();
    qint64 cachedSize = selectQuery.value(1).toInt();
    QByteArray cachedHash = QByteArray::fromHex(selectQuery.value(2).toByteArray());
    QDateTime cachedDate = selectQuery.value(3).toDateTime();

    QSqlDatabase sharesDb = sharesDatabase->database();
    QSqlQuery updateQuery("UPDATE files SET date = :date, hash = :hash, size = :size WHERE path = :path", sharesDb);

    // check file is still readable
    QFileInfo info(sharesDir.filePath(path));
    if (!info.isReadable())
    {
        sharesDatabase->deleteFile(path);
        return false;
    }

    // check whether we need to calculate checksum
    if (cachedDate != info.lastModified() || cachedSize != info.size())
        cachedHash = QByteArray();
    if (updateHash && cachedHash.isEmpty())
    {
        cachedHash = hashFile(info.filePath());
        if (cachedHash.isEmpty())
        {
            logMessage(QXmppLogger::WarningMessage, "Error hashing file " + path);
            sharesDatabase->deleteFile(path);
            return false;
        }
    }

    // update database entry
    if (cachedDate != info.lastModified() || cachedSize != info.size())
    {
        cachedDate = info.lastModified();
        cachedSize = info.size();
        updateQuery.bindValue(":date", cachedDate);
        updateQuery.bindValue(":hash", cachedHash.toHex());
        updateQuery.bindValue(":size", cachedSize);
        updateQuery.bindValue(":path", path);
        updateQuery.exec();
    }

    // fill meta-data
    file.setName(info.fileName());
    if (!cachedHash.isEmpty())
        file.setFileHash(cachedHash);
    file.setFileSize(cachedSize);

    QXmppShareLocation location(requestIq.to());
    location.setNode(path);
    file.setLocations(location);

    return true;
}

void SearchThread::run()
{
    QXmppShareSearchIq responseIq;
    responseIq.setId(requestIq.id());
    responseIq.setTo(requestIq.from());
    responseIq.setTag(requestIq.tag());

    // determine query type
    QXmppShareLocation location;
    location.setJid(requestIq.to());
    location.setNode(requestIq.node());
    responseIq.collection().setLocations(location);

    // clean input
    QString basePrefix = requestIq.node();
    if (!basePrefix.isEmpty() && !basePrefix.endsWith("/"))
        basePrefix += "/";
    int queryDepth = requestIq.depth();
    const QString queryString = requestIq.search().trimmed();
    if (queryString.contains("\\"))
    {
        logMessage(QXmppLogger::WarningMessage, "Received an invalid search: " + queryString);
        QXmppStanza::Error error(QXmppStanza::Error::Cancel, QXmppStanza::Error::BadRequest);
        responseIq.setError(error);
        responseIq.setType(QXmppIq::Error);
        emit searchFinished(responseIq);
        return;
    }

    // check the base path exists
    QDir sharesDir = sharesDatabase->directory();
    QFileInfo info(sharesDir.filePath(basePrefix));
    if (!basePrefix.isEmpty() && !info.exists())
    {
        logMessage(QXmppLogger::WarningMessage, "Base path no longer exists: " + basePrefix);

        // remove obsolete DB entries
        QString like = basePrefix;
        like.replace("%", "\\%");
        like.replace("_", "\\_");
        like += "%";
        QSqlDatabase sharesDb = sharesDatabase->database();
        QSqlQuery query("DELETE FROM files WHERE PATH LIKE :search ESCAPE :escape", sharesDb);
        query.bindValue(":search", like);
        query.bindValue(":escape", "\\");
        query.exec();

        QXmppStanza::Error error(QXmppStanza::Error::Cancel, QXmppStanza::Error::ItemNotFound);
        responseIq.setError(error);
        responseIq.setType(QXmppIq::Error);
        emit searchFinished(responseIq);
        return;
    }

    // perform query
    search(responseIq.collection(), basePrefix, queryString, queryDepth);

    // send response
    responseIq.setType(QXmppIq::Result);
    emit searchFinished(responseIq);
}

void SearchThread::search(QXmppShareItem &rootCollection, const QString &basePrefix, const QString &queryString, int maxDepth)
{
    QTime t;
    t.start();

    // prepare SQL query
    QString like;
    if (!queryString.isEmpty())
    {
        like = queryString;
        like.replace("%", "\\%");
        like.replace("_", "\\_");
        like = "%" + like + "%";
    }
    else if (!basePrefix.isEmpty())
    {
        like = basePrefix;
        like.replace("%", "\\%");
        like.replace("_", "\\_");
        like += "%";
    }

    QString sql("SELECT path, size, hash, date FROM files");
    if (!like.isEmpty())
        sql += " WHERE path LIKE :search ESCAPE :escape";
    sql += " ORDER BY path";
    QSqlDatabase sharesDb = sharesDatabase->database();
    QSqlQuery query(sql, sharesDb);
    if (!like.isEmpty())
    {
        query.bindValue(":search", like);
        query.bindValue(":escape", "\\");
    }

    // store results
    int fileCount = 0;
    query.exec();
    QMap<QString, QXmppShareItem*> subDirs;
    while (query.next())
    {
        const QString path = query.value(0).toString();

        QString prefix;
        if (!queryString.isEmpty())
        {
            int matchIndex = path.indexOf(queryString, 0, Qt::CaseInsensitive);
            if (matchIndex < 0)
            {
                logMessage(QXmppLogger::WarningMessage, QString("Could not find %1 in %2").arg(queryString, path));
                continue;
            }
            int slashIndex = path.lastIndexOf("/", matchIndex);
            if (slashIndex >= 0)
                prefix = path.left(slashIndex + 1);
        } else {
            prefix = basePrefix;
        }

        // find the depth at which we matched
        QStringList relativeBits = path.mid(prefix.size()).split("/");
        if (!relativeBits.size())
        {
            logMessage(QXmppLogger::WarningMessage, "Query returned an empty path");
            continue;
        }
        relativeBits.removeLast();

        QXmppShareItem *parentCollection = &rootCollection;
        QString dirPath;
        const int maxBits = maxDepth ? qMin(maxDepth, relativeBits.size()) : relativeBits.size();
        for (int i = 0; i < maxBits; i++)
        {
            const QString dirName = relativeBits[i];
            if (!dirPath.isEmpty())
                dirPath += "/";
            dirPath += dirName;

            if (!subDirs.contains(dirPath))
            {
                QXmppShareItem collection(QXmppShareItem::CollectionItem);
                collection.setName(dirName);
                QXmppShareLocation location(requestIq.to());
                location.setNode(prefix + dirPath + "/");
                collection.setLocations(location);
                subDirs[dirPath] = parentCollection->appendChild(collection);
            }
            parentCollection = subDirs[dirPath];
        }

        if (!maxDepth || relativeBits.size() < maxDepth)
        {
            // update file info
            QXmppShareItem file(QXmppShareItem::FileItem);
            if (updateFile(file, query, requestIq.hash()))
            {
                fileCount++;
                parentCollection->appendChild(file);
            }
        }

        if (t.elapsed() > SEARCH_MAX_TIME)
        {
            logMessage(QXmppLogger::WarningMessage, QString("Search truncated at %1 results after %2 s").arg(fileCount).arg(double(t.elapsed()) / 1000.0));
            break;
        }
    }
}

