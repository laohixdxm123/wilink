/*
 * wiLink
 * Copyright (C) 2009-2011 Bolloré telecom
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

#ifndef __UPDATES_H__
#define __UPDATES_H__

#include <QMap>
#include <QUrl>

class QNetworkReply;

class Release
{
public:
    QString changes;
    QString package;
    QUrl url;
    QString version;
    QMap<QString, QByteArray> hashes;
};

class UpdatesPrivate;

/** The Updates class handling checking for software updates
 *  and installing them.
 */
class Updates : public QObject
{
    Q_OBJECT
    Q_ENUMS(Error State)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
    enum Error {
        NoError = 0,
        IntegrityError,
        FileError,
        NetworkError,
        SecurityError,
    };

    enum State {
        IdleState = 0,
        CheckState,
        DownloadState,
        PromptState,
        InstallState,
    };

    Updates(QObject *parent);
    ~Updates();

    State state() const;
    QString updateChanges() const;
    QString updateVersion() const;

    QString cacheDirectory() const;
    void setCacheDirectory(const QString &cacheDir);

    static int compareVersions(const QString &v1, const QString v2);

public slots:
    void check();
    void install();

signals:
    void downloadProgress(qint64 done, qint64 total);
    void error(Updates::Error error, const QString &errorString);
    void stateChanged(Updates::State state);

private slots:
    void _q_saveUpdate();
    void _q_processStatus();

private:
    void download();
    UpdatesPrivate * const d;
};

#endif
