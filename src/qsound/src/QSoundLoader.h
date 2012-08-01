/*
 * wiLink
 * Copyright (C) 2009-2012 Wifirst
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

#ifndef __WILINK_SOUNDS_H__
#define __WILINK_SOUNDS_H__

#include <QObject>
#include <QUrl>

class QSoundLoaderPrivate;

class QSoundLoader : public QObject
{
    Q_OBJECT
    Q_ENUMS(Status)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)

public:
    enum Status { Null = 0, Playing, Loading, Error };

    QSoundLoader(QObject *parent = 0);
    ~QSoundLoader();

    QUrl source() const;
    void setSource(const QUrl &source);

    Status status() const;

signals:
    void sourceChanged();
    void statusChanged();

private slots:
    void _q_replyFinished();

private:
    QSoundLoaderPrivate *d;
    friend class QSoundLoaderPrivate;
};

#endif
