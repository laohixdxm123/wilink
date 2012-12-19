/*
 * QNetIO
 * Copyright (C) 2008-2012 Bolloré telecom
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

#ifndef __QNETIO_FILESYSTEM_WIFIRST_H__
#define __QNETIO_FILESYSTEM_WIFIRST_H__

#include <QHash>

#include "filesystem.h"

class QDomElement;
class QNetworkAccessManager;
class QNetworkReply;

using namespace QNetIO;

/** Support for access to Wifirst.net photo shares as a file system.
 */
class WifirstFileSystem : public FileSystem
{
    Q_OBJECT

public:
    WifirstFileSystem(QObject *parent=NULL);

public slots:
    FileSystemJob *get(const QUrl &fileUrl, ImageSize type);
    FileSystemJob* open(const QUrl &url);
    FileSystemJob* list(const QUrl &dirUrl, const QString &filter = QString());
    FileSystemJob* mkdir(const QUrl &dirUrl);
    FileSystemJob* put(const QUrl &fileUrl, QIODevice *data);
    FileSystemJob* remove(const QUrl &fileUrl);
    FileSystemJob* rmdir(const QUrl &dirUrl);

private:
    FileInfo parseAlbum(const QDomElement &element);
    FileInfoList parseFiles(const QDomElement &element, QString listBase);

    QNetworkAccessManager *network;
    QUrl baseUrl;
    QString baseVirtual;

    QHash<QString, int> albumHash;
    QHash<int, QString> pictureHash;

    friend class WifirstFileSystemJob;
};

#endif
