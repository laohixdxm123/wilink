/*
 * wDesktop
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

#define SEARCH_MAX_TIME 15000

ChatSharesDatabase::ChatSharesDatabase(const QString &path, QObject *parent)
    : QObject(parent), sharesDir(path)
{
    // prepare database
    sharesDb = QSqlDatabase::addDatabase("QSQLITE");
    sharesDb.setDatabaseName("/tmp/shares.db");
    Q_ASSERT(sharesDb.open());
    sharesDb.exec("CREATE TABLE files (path text, size int, hash varchar(32))");
    sharesDb.exec("CREATE UNIQUE INDEX files_path ON files (path)");

    // start indexing
    QThread *worker = new IndexThread(sharesDb, sharesDir, this);
    connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
    worker->start();
}

QString ChatSharesDatabase::locate(const QXmppShareIq::Item &file)
{
    QSqlQuery query("SELECT path FROM files WHERE hash = :hash AND size = :size", sharesDb);
    query.bindValue(":hash", file.fileHash().toHex());
    query.bindValue(":size", file.fileSize());
    query.exec();
    if (!query.next())
        return QString();
    return sharesDir.filePath(query.value(0).toString());
}

void ChatSharesDatabase::search(const QXmppShareSearchIq &requestIq)
{
    QThread *worker = new SearchThread(sharesDb, sharesDir, requestIq, this);
    connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(worker, SIGNAL(searchFinished(const QXmppShareSearchIq&)), this, SIGNAL(searchFinished(const QXmppShareSearchIq&)));
    worker->start();
}

IndexThread::IndexThread(const QSqlDatabase &database, const QDir &dir, QObject *parent)
    : QThread(parent), scanCount(0), sharesDb(database), sharesDir(dir)
{
};

void IndexThread::run()
{
    QTime t;
    t.start();
    scanDir(sharesDir);
    qDebug() << "Scanned" << scanCount << "files in" << double(t.elapsed()) / 1000.0 << "s";
}

void IndexThread::scanDir(const QDir &dir)
{
    QSqlQuery query("INSERT INTO files (path, size) "
                    "VALUES(:path, :size)", sharesDb);

    foreach (const QFileInfo &info, dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable))
    {
        if (info.isDir())
        {
            scanDir(QDir(info.filePath()));
        } else {
            query.bindValue(":path", sharesDir.relativeFilePath(info.filePath()));
            query.bindValue(":size", info.size());
            query.exec();
            scanCount++;
        }
    }
}

SearchThread::SearchThread(const QSqlDatabase &database, const QDir &dir, const QXmppShareSearchIq &request, QObject *parent)
    : QThread(parent), requestIq(request), sharesDb(database), sharesDir(dir)
{
};

bool SearchThread::updateFile(QXmppShareIq::Item &file, const QSqlQuery &selectQuery)
{
    const QString path = selectQuery.value(0).toString();
    qint64 cachedSize = selectQuery.value(1).toInt();
    QByteArray cachedHash = QByteArray::fromHex(selectQuery.value(2).toByteArray());

    QCryptographicHash hasher(QCryptographicHash::Md5);
    QSqlQuery deleteQuery("DELETE FROM files WHERE path = :path", sharesDb);
    QSqlQuery updateQuery("UPDATE files SET hash = :hash, size = :size WHERE path = :path", sharesDb);

    // check file is still readable
    QFileInfo info(sharesDir.filePath(path));
    if (!info.isReadable())
    {
        deleteQuery.bindValue(":path", path);
        deleteQuery.exec();
        return false;
    }

    // check whether we need to calculate checksum
    if (cachedHash.isEmpty() || cachedSize == info.size())
    {
        QFile fp(info.filePath());

        // if we cannot open the file, remove it from database
        if (!fp.open(QIODevice::ReadOnly))
        {
            qWarning() << "Failed to open file" << path;
            deleteQuery.bindValue(":path", path);
            deleteQuery.exec();
            return false;
        }

        // update file object
        while (fp.bytesAvailable())
            hasher.addData(fp.read(16384));
        fp.close();
        cachedHash = hasher.result();
        cachedSize = info.size();

        // update database entry
        updateQuery.bindValue(":hash", cachedHash.toHex());
        updateQuery.bindValue(":size", cachedSize);
        updateQuery.bindValue(":path", path);
        updateQuery.exec();
    }

    // fill meta-data
    file.setName(info.fileName());
    file.setFileHash(cachedHash);
    file.setFileSize(cachedSize);

    QXmppShareIq::Mirror mirror(requestIq.to());
    mirror.setPath(path);
    file.setMirrors(mirror);

    return true;
}

void SearchThread::run()
{
    QXmppShareSearchIq responseIq;
    responseIq.setId(requestIq.id());
    responseIq.setTo(requestIq.from());
    responseIq.setTag(requestIq.tag());

    // determine query type
    QXmppShareIq::Item rootCollection(QXmppShareIq::Item::CollectionItem);
    QXmppShareIq::Mirror mirror;
    mirror.setJid(requestIq.to());
    mirror.setPath(requestIq.base());
    rootCollection.setMirrors(mirror);
    const QString queryString = requestIq.search().trimmed();
    if (queryString.isEmpty())
    {
        if (!browse(rootCollection, requestIq.base()))
        {
            qWarning() << "Browse failed";
            responseIq.setType(QXmppIq::Error);
            emit searchFinished(responseIq);
            return;
        }
    } else {
        // perform search
        if (!search(rootCollection, queryString))
        {
            qWarning() << "Search" << queryString << "failed";
            responseIq.setType(QXmppIq::Error);
            emit searchFinished(responseIq);
            return;
        }
    }

    // send response
    responseIq.setType(QXmppIq::Result);
    responseIq.setCollection(rootCollection);
    emit searchFinished(responseIq);
}

bool SearchThread::browse(QXmppShareIq::Item &rootCollection, const QString &base)
{
    QTime t;
    t.start();

    QString prefix = base;
    if (!prefix.isEmpty() && !prefix.endsWith("/"))
        prefix += "/";

    QString sql("SELECT path, size, hash FROM files");
    if (!prefix.isEmpty())
        sql += " WHERE path LIKE :search ESCAPE :escape";
    sql += " ORDER BY path";
    QSqlQuery query(sql, sharesDb);

    if (!prefix.isEmpty())
    {
        QString like = prefix;
        like.replace("%", "\\%");
        like.replace("_", "\\_");
        like += "%";
        query.bindValue(":search", like);
        query.bindValue(":escape", "\\");
    }
    query.exec();

    QStringList subDirs;
    while (query.next())
    {
        const QString path = query.value(0).toString();
        const QString relativePath = path.mid(prefix.size());
        if (relativePath.count("/") == 0)
        {
            QXmppShareIq::Item file(QXmppShareIq::Item::FileItem);
            if (updateFile(file, query))
                rootCollection.appendChild(file);
        }
        else
        {
            const QString dirName = relativePath.split("/").first();
            if (subDirs.contains(dirName))
                continue;
            subDirs.append(dirName);

            QXmppShareIq::Item &collection = rootCollection.mkpath(dirName);
            QXmppShareIq::Mirror mirror(requestIq.to());
            mirror.setPath(prefix + dirName + "/");
            collection.setMirrors(mirror);
        }
        if (t.elapsed() > SEARCH_MAX_TIME)
            break;
    }
    qDebug() << "Browsed" << rootCollection.children().size() << "files in" << double(t.elapsed()) / 1000.0 << "s";
    return true;
}

bool SearchThread::search(QXmppShareIq::Item &rootCollection, const QString &queryString)
{
    if (queryString.contains("/") ||
        queryString.contains("\\"))
    {
        qWarning() << "Received an invalid search" << queryString;
        return false;
    }

    QCryptographicHash hasher(QCryptographicHash::Md5);
    QTime t;
    t.start();

    // perform search
    QString like = queryString;
    like.replace("%", "\\%");
    like.replace("_", "\\_");
    like = "%" + like + "%";
    QSqlQuery query("SELECT path, size, hash FROM files WHERE path LIKE :search ESCAPE :escape ORDER BY path", sharesDb);
    query.bindValue(":search", like);
    query.bindValue(":escape", "\\");
    query.exec();

    int searchCount = 0;
    while (query.next())
    {
        const QString path = query.value(0).toString();

        // find the depth at which we matched
        QString matchString;
        QRegExp subdirRe(".*(([^/]+)/[^/]+)/[^/]+");
        QRegExp dirRe(".*([^/]+)/[^/]+");
        QRegExp fileRe(".*([^/]+)");
        if (subdirRe.exactMatch(path) && subdirRe.cap(2).contains(queryString, Qt::CaseInsensitive))
            matchString = subdirRe.cap(1);
        else if (dirRe.exactMatch(path) && dirRe.cap(1).contains(queryString, Qt::CaseInsensitive))
            matchString = subdirRe.cap(1); 
        else if (fileRe.exactMatch(path) && fileRe.cap(1).contains(queryString, Qt::CaseInsensitive))
            matchString = "";
        else
            continue;

        // update file info
        QXmppShareIq::Item file(QXmppShareIq::Item::FileItem);
        if (updateFile(file, query))
        {
            // add file to the appropriate collection
            rootCollection.mkpath(matchString).appendChild(file);
            searchCount++;
        }

        // limit maximum search time to 15s
        if (t.elapsed() > SEARCH_MAX_TIME || searchCount > 250)
            break;
    }

    qDebug() << "Found" << searchCount << "files in" << double(t.elapsed()) / 1000.0 << "s";
    return true;
}

