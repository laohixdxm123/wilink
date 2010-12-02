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

#ifndef __WILINK_PHONE_MODELS_H__
#define __WILINK_PHONE_MODELS_H__

#include <QAbstractListModel>
#include <QList>
#include <QUrl>
#include <QTableView>

class QNetworkAccessManager;
class QNetworkRequest;
class PhoneCallsItem;
class SipCall;

class PhoneCallsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    PhoneCallsModel(QNetworkAccessManager *network, QObject *parent = 0);
    ~PhoneCallsModel();

    void addCall(SipCall *call);
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    void setUrl(const QUrl &url);

private slots:
    void handleCreate();
    void handleFinished();
    void handleList();

private:
    QNetworkRequest buildRequest(const QUrl &url) const;

    QList<PhoneCallsItem*> m_items;
    QNetworkAccessManager *m_network;
    QList<PhoneCallsItem*> m_pending;
    QUrl m_url;
};

class PhoneCallsView : public QTableView
{
    Q_OBJECT

public:
    PhoneCallsView(PhoneCallsModel *model, QWidget *parent = 0);

protected:
    void resizeEvent(QResizeEvent *e);
};

#endif
