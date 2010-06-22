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

#include <QContextMenuEvent>
#include <QDebug>
#include <QList>
#include <QMenu>
#include <QPainter>
#include <QStringList>
#include <QSortFilterProxyModel>
#include <QUrl>

#include "qxmpp/QXmppConstants.h"
#include "qxmpp/QXmppDiscoveryIq.h"
#include "qxmpp/QXmppMessage.h"
#include "qxmpp/QXmppRoster.h"
#include "qxmpp/QXmppRosterIq.h"
#include "qxmpp/QXmppUtils.h"
#include "qxmpp/QXmppVCardManager.h"

#include "chat_roster.h"
#include "chat_roster_item.h"

enum RosterColumns {
    ContactColumn = 0,
    ImageColumn,
    SortingColumn,
    MaxColumn,
};

static void paintMessages(QPixmap &icon, int messages)
{
    QString pending = QString::number(messages);
    QPainter painter(&icon);
    QFont font = painter.font();
    font.setWeight(QFont::Bold);
    painter.setFont(font);

    // text rectangle
    QRect rect = painter.fontMetrics().boundingRect(pending);
    rect.setWidth(rect.width() + 4);
    if (rect.width() < rect.height())
        rect.setWidth(rect.height());
    else
        rect.setHeight(rect.width());
    rect.moveTop(2);
    rect.moveRight(icon.width() - 2);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::red);
    painter.setPen(Qt::white);
    painter.drawEllipse(rect);
    painter.drawText(rect, Qt::AlignCenter, pending);
}

ChatRosterModel::ChatRosterModel(QXmppClient *xmppClient)
    : client(xmppClient)
{
    rootItem = new ChatRosterItem(ChatRosterItem::Root);
    connect(client, SIGNAL(connected()), this, SLOT(connected()));
    connect(client, SIGNAL(disconnected()), this, SLOT(disconnected()));
    connect(client, SIGNAL(discoveryIqReceived(const QXmppDiscoveryIq&)), this, SLOT(discoveryIqReceived(const QXmppDiscoveryIq&)));
    connect(client, SIGNAL(presenceReceived(const QXmppPresence&)), this, SLOT(presenceReceived(const QXmppPresence&)));
    connect(&client->getRoster(), SIGNAL(presenceChanged(const QString&, const QString&)), this, SLOT(presenceChanged(const QString&, const QString&)));
    connect(&client->getRoster(), SIGNAL(rosterChanged(const QString&)), this, SLOT(rosterChanged(const QString&)));
    connect(&client->getRoster(), SIGNAL(rosterReceived()), this, SLOT(rosterReceived()));
    connect(&client->getVCardManager(), SIGNAL(vCardReceived(const QXmppVCard&)), this, SLOT(vCardReceived(const QXmppVCard&)));
}

ChatRosterModel::~ChatRosterModel()
{
    delete rootItem;
}

int ChatRosterModel::columnCount(const QModelIndex &parent) const
{
    return MaxColumn;
}

void ChatRosterModel::connected()
{
    /* request own vCard */
    nickName = client->getConfiguration().user();
    client->getVCardManager().requestVCard(
        client->getConfiguration().jidBare());
}

QPixmap ChatRosterModel::contactAvatar(const QString &bareJid) const
{
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
        return item->data(AvatarRole).value<QPixmap>();
    return QPixmap();
}

/** Returns the full JID of an online contact which has the requested feature.
 *
 * @param bareJid
 * @param feature
 */
QStringList ChatRosterModel::contactFeaturing(const QString &bareJid, ChatRosterModel::Feature feature) const
{
    QStringList jids;
    const QString sought = bareJid + "/";
    foreach (const QString &key, clientFeatures.keys())
        if (key.startsWith(sought) && (clientFeatures.value(key) & feature))
            jids << key;
    return jids;
}

/** Determine the display name for a contact.
 *
 *  If the user has set a name for the roster entry, it will be used,
 *  otherwise we fall back to information from the vCard.
 *
 * @param bareJid
 */
QString ChatRosterModel::contactName(const QString &bareJid) const
{
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
        return item->data(Qt::DisplayRole).toString();
    return bareJid.split("@").first();
}

static QString contactStatus(const QModelIndex &index)
{
    const int typeVal = index.data(ChatRosterModel::StatusRole).toInt();
    QXmppPresence::Status::Type type = static_cast<QXmppPresence::Status::Type>(typeVal);
    if (type == QXmppPresence::Status::Offline)
        return "offline";
    else if (type == QXmppPresence::Status::Online || type == QXmppPresence::Status::Chat)
        return "available";
    else if (type == QXmppPresence::Status::Away || type == QXmppPresence::Status::XA)
        return "away";
    else
        return "busy";
}

QVariant ChatRosterModel::data(const QModelIndex &index, int role) const
{
    ChatRosterItem *item = static_cast<ChatRosterItem*>(index.internalPointer());
    if (!index.isValid() || !item)
        return QVariant();

    QString bareJid = item->id();
    int messages = item->data(MessagesRole).toInt();

    if (role == IdRole) {
        return bareJid;
    } else if (role == TypeRole) {
        return item->type();
    } else if (role == StatusRole && item->type() == ChatRosterItem::Contact) {
        QXmppPresence::Status::Type statusType = QXmppPresence::Status::Offline;
        foreach (const QXmppPresence &presence, client->getRoster().getAllPresencesForBareJid(bareJid))
        {
            QXmppPresence::Status::Type type = presence.status().type();
            if (type == QXmppPresence::Status::Offline)
                continue;
            // FIXME : we should probably be using the priority rather than
            // stop at the first available contact
            else if (type == QXmppPresence::Status::Online ||
                     type == QXmppPresence::Status::Chat)
                return type;
            else
                statusType = type;
        }
        return statusType;
    } else if (role == Qt::DisplayRole && index.column() == ImageColumn) {
        return QVariant();
    } else if(role == Qt::FontRole && index.column() == ContactColumn) {
        if (messages)
            return QFont("", -1, QFont::Bold, true);
    } else if(role == Qt::BackgroundRole && index.column() == ContactColumn) {
        if (messages)
        {
            QLinearGradient grad(QPointF(0, 0), QPointF(0.8, 0));
            grad.setColorAt(0, QColor(255, 0, 0, 144));
            grad.setColorAt(1, Qt::transparent);
            grad.setCoordinateMode(QGradient::ObjectBoundingMode);
            return QBrush(grad);
        }
    } else {
        if (item->type() == ChatRosterItem::Contact)
        {
            if (role == Qt::DecorationRole && index.column() == ContactColumn) {
                QPixmap icon(QString(":/contact-%1.png").arg(contactStatus(index)));
                if (messages)
                    paintMessages(icon, messages);
                return icon;
            } else if (role == Qt::DecorationRole && index.column() == ImageColumn) {
                return QIcon(contactAvatar(bareJid));
            } else if (role == Qt::DisplayRole && index.column() == ContactColumn) {
                return contactName(bareJid);
            } else if (role == Qt::DisplayRole && index.column() == SortingColumn) {
                return (contactStatus(index) + "_" + contactName(bareJid)).toLower() + "_" + bareJid.toLower();
            }
        } else if (item->type() == ChatRosterItem::Room) {
            if (role == Qt::DecorationRole && index.column() == ContactColumn) {
                QPixmap icon(":/chat.png");
                if (messages)
                    paintMessages(icon, messages);
                return icon;
            } else if (role == Qt::DisplayRole && index.column() == SortingColumn) {
                return QString("chatroom_") + bareJid.toLower();
            }
        } else if (item->type() == ChatRosterItem::RoomMember) {
            if (role == Qt::DisplayRole && index.column() == SortingColumn) {
                return QString("chatuser_") + contactStatus(index) + "_" + bareJid.toLower();
            } else if (role == Qt::DecorationRole && index.column() == ContactColumn) {
                return QIcon(QString(":/contact-%1.png").arg(contactStatus(index)));
            }
        }
    }
    if (role == Qt::DecorationRole && index.column() == ImageColumn)
        return QVariant();

    return item->data(role);
}

void ChatRosterModel::disconnected()
{
    clientFeatures.clear();
    for (int i = 0; i < rootItem->size(); i++)
    {
        ChatRosterItem *child = rootItem->child(i);
        if (child->type() != ChatRosterItem::Contact)
            continue;

        emit dataChanged(createIndex(child->row(), ContactColumn, child),
                         createIndex(child->row(), SortingColumn, child));
    }
}

void ChatRosterModel::discoveryIqReceived(const QXmppDiscoveryIq &disco)
{
    if (!clientFeatures.contains(disco.from()) ||
        disco.type() != QXmppIq::Result ||
        disco.queryType() != QXmppDiscoveryIq::InfoQuery)
        return;

    int features = 0;
    foreach (const QString &var, disco.features())
    {
        if (var == ns_chat_states)
            features |= ChatStatesFeature;
        else if (var == ns_stream_initiation_file_transfer)
            features |= FileTransferFeature;
        else if (var == ns_version)
            features |= VersionFeature;
    }
    foreach (const QXmppDiscoveryIq::Identity& id, disco.identities())
    {
        if (id.name() == "iChatAgent")
            features |= ChatStatesFeature;
    }
    clientFeatures.insert(disco.from(), features);
}

QModelIndex ChatRosterModel::findItem(const QString &bareJid) const
{
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
        return createIndex(item->row(), 0, item);
    else
        return QModelIndex();
}

Qt::ItemFlags ChatRosterModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);
    if (!index.isValid())
        return defaultFlags;

    ChatRosterItem *item = static_cast<ChatRosterItem*>(index.internalPointer());
    if (item->type() == ChatRosterItem::Contact)
        return Qt::ItemIsDragEnabled | defaultFlags;
    else
        return defaultFlags;
}

QModelIndex ChatRosterModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    ChatRosterItem *parentItem;
    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<ChatRosterItem*>(parent.internalPointer());

    ChatRosterItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    else
        return QModelIndex();
}

QString ChatRosterModel::ownName() const
{
    return nickName;
}

QModelIndex ChatRosterModel::parent(const QModelIndex & index) const
{
    if (!index.isValid())
        return QModelIndex();

    ChatRosterItem *childItem = static_cast<ChatRosterItem*>(index.internalPointer());
    ChatRosterItem *parentItem = childItem->parent();

    if (parentItem == rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

QMimeData *ChatRosterModel::mimeData(const QModelIndexList &indexes) const
{
    QList<QUrl> urls;
    foreach (QModelIndex index, indexes)
        if (index.isValid() && index.column() == ContactColumn)
            urls << QUrl(index.data(ChatRosterModel::IdRole).toString());

    QMimeData *mimeData = new QMimeData();
    mimeData->setUrls(urls);
    return mimeData;
}

QStringList ChatRosterModel::mimeTypes() const
{
    return QStringList() << "text/uri-list";
}

void ChatRosterModel::presenceChanged(const QString& bareJid, const QString& resource)
{
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
        emit dataChanged(createIndex(item->row(), ContactColumn, item),
                         createIndex(item->row(), SortingColumn, item));
}

void ChatRosterModel::presenceReceived(const QXmppPresence &presence)
{
    const QString jid = presence.from();
    const QString bareJid = jidToBareJid(jid);
    const QString resource = jidToResource(jid);

    // handle features discovery
    if (!resource.isEmpty())
    {
        if (presence.type() == QXmppPresence::Unavailable)
            clientFeatures.remove(jid);
        else if (presence.type() == QXmppPresence::Available && !clientFeatures.contains(jid))
        {
            clientFeatures.insert(jid, 0);

            // discover remote party features
            QXmppDiscoveryIq disco;
            disco.setTo(jid);
            disco.setQueryType(QXmppDiscoveryIq::InfoQuery);
            client->sendPacket(disco);
        }
    }

    // handle chat rooms
    ChatRosterItem *roomItem = rootItem->find(bareJid);
    if (!roomItem || roomItem->type() != ChatRosterItem::Room)
        return;

    ChatRosterItem *memberItem = roomItem->find(jid);
    if (presence.type() == QXmppPresence::Available)
    {
        if (!memberItem)
        {
            // create roster entry
            memberItem = new ChatRosterItem(ChatRosterItem::RoomMember, jid);
            memberItem->setData(StatusRole, presence.status().type());
            beginInsertRows(createIndex(roomItem->row(), 0, roomItem), roomItem->size(), roomItem->size());
            roomItem->append(memberItem);
            endInsertRows();
        } else {
            // update roster entry
            memberItem->setData(StatusRole, presence.status().type());
            emit dataChanged(createIndex(memberItem->row(), ContactColumn, memberItem),
                             createIndex(memberItem->row(), SortingColumn, memberItem));
        }

        // check whether we own the room
        foreach (const QXmppElement &x, presence.extensions())
        {
            if (x.tagName() == "x" && x.attribute("xmlns") == ns_muc_user)
            {
                QXmppElement item = x.firstChildElement("item");
                if (item.attribute("jid") == client->getConfiguration().jid())
                {
                    int flags = 0;
                    // role
                    if (item.attribute("role") == "moderator")
                        flags |= KickFlag;
                    // affiliation
                    if (item.attribute("affiliation") == "owner")
                        flags |= (OptionsFlag | MembersFlag);
                    else if (item.attribute("affiliation") == "admin")
                        flags |= MembersFlag;
                    roomItem->setData(FlagsRole, flags);
                }
            }
        }
    }
    else if (presence.type() == QXmppPresence::Unavailable && memberItem)
    {
        beginRemoveRows(createIndex(roomItem->row(), 0, roomItem), memberItem->row(), memberItem->row());
        roomItem->remove(memberItem);
        endRemoveRows();
    }
}

void ChatRosterModel::rosterChanged(const QString &jid)
{
    ChatRosterItem *item = rootItem->find(jid);
    QXmppRoster::QXmppRosterEntry entry = client->getRoster().getRosterEntry(jid);

    qDebug() << "roster changed" << jid;
    // remove an existing entry
    if (entry.subscriptionType() == QXmppRoster::QXmppRosterEntry::Remove)
    {
        if (item)
        {
            beginRemoveRows(QModelIndex(), item->row(), item->row());
            rootItem->remove(item);
            endRemoveRows();
        }
        return;
    }

    if (item)
    {
        // update an existing entry
        if (!entry.name().isEmpty())
            item->setData(Qt::DisplayRole, entry.name());
        emit dataChanged(createIndex(item->row(), ContactColumn, item),
                         createIndex(item->row(), SortingColumn, item));
    } else {
        // add a new entry
        item = new ChatRosterItem(ChatRosterItem::Contact, jid);
        if (!entry.name().isEmpty())
            item->setData(Qt::DisplayRole, entry.name());
        beginInsertRows(QModelIndex(), rootItem->size(), rootItem->size());
        rootItem->append(item);
        endInsertRows();
    }

    // fetch vCard
    client->getVCardManager().requestVCard(jid);
}

void ChatRosterModel::rosterReceived()
{
    // remove existing contacts
    QList <ChatRosterItem*> goners;
    QMap<QString, int> pending;
    for (int i = 0; i < rootItem->size(); i++)
    {
        ChatRosterItem *child = rootItem->child(i);
        if (child->type() == ChatRosterItem::Contact)
        {
            pending.insert(child->id(), child->data(MessagesRole).toInt());
            goners << child;
        }
    }
    foreach (ChatRosterItem *child, goners)
        rootItem->remove(child);

    // add received contacts
    foreach (const QString &jid, client->getRoster().getRosterBareJids())
    {
        QXmppRoster::QXmppRosterEntry entry = client->getRoster().getRosterEntry(jid);
        if (entry.subscriptionType() == QXmppRoster::QXmppRosterEntry::Remove)
            continue;

        ChatRosterItem *item = new ChatRosterItem(ChatRosterItem::Contact, jid);
        if (!entry.name().isEmpty())
            item->setData(Qt::DisplayRole, entry.name());
        item->setData(MessagesRole, pending.value(jid));
        rootItem->append(item);

        // fetch vCard
        client->getVCardManager().requestVCard(jid);
    }
    reset();
}

bool ChatRosterModel::removeRows(int row, int count, const QModelIndex &parent)
{
    ChatRosterItem *parentItem;
    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<ChatRosterItem*>(parent.internalPointer());

    const int minIndex = qMax(0, row);
    const int maxIndex = qMin(row + count, parentItem->size()) - 1;
    for (int i = maxIndex; i >= minIndex; --i)
        parentItem->removeAt(i);
    return maxIndex > minIndex;
}

int ChatRosterModel::rowCount(const QModelIndex &parent) const
{
    ChatRosterItem *parentItem;
    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<ChatRosterItem*>(parent.internalPointer());
    return parentItem->size();
}

void ChatRosterModel::vCardReceived(const QXmppVCard& vcard)
{
    const QString bareJid = vcard.from();
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
    {
        const QImage &image = vcard.photoAsImage();
        item->setData(AvatarRole, QPixmap::fromImage(image));

        // Store the nickName or fullName found in the vCard for display,
        // unless the roster entry has a name.
        QXmppRoster::QXmppRosterEntry entry = client->getRoster().getRosterEntry(bareJid);
        if (entry.name().isEmpty())
        {
            if (!vcard.nickName().isEmpty())
                item->setData(Qt::DisplayRole, vcard.nickName());
            else if (!vcard.fullName().isEmpty())
                item->setData(Qt::DisplayRole, vcard.fullName());
        }

        const QString url = vcard.url();
        if (!url.isEmpty())
            item->setData(UrlRole, vcard.url());

        emit dataChanged(createIndex(item->row(), ContactColumn, item),
                         createIndex(item->row(), SortingColumn, item));
    }
    if (bareJid == client->getConfiguration().jidBare())
    {
        if (!vcard.nickName().isEmpty())
            nickName = vcard.nickName();
    }
}

void ChatRosterModel::addPendingMessage(const QString &bareJid)
{
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
    {
        item->setData(MessagesRole, item->data(MessagesRole).toInt() + 1);
        emit dataChanged(createIndex(item->row(), ContactColumn, item),
                         createIndex(item->row(), SortingColumn, item));
    }
}

void ChatRosterModel::addItem(ChatRosterItem::Type type, const QString &id, const QString &name, const QIcon &icon, const QModelIndex &parent)
{
    ChatRosterItem *parentItem;
    if (parent.isValid())
        parentItem = static_cast<ChatRosterItem*>(parent.internalPointer());
    else
        parentItem = rootItem;

    // check the item does not already exist
    if (parentItem->find(id))
        return;

    // prepare item
    ChatRosterItem *item = new ChatRosterItem(type, id);
    if (!name.isEmpty())
        item->setData(Qt::DisplayRole, name);
    if (!icon.isNull())
        item->setData(Qt::DecorationRole, icon);

    // add item
    beginInsertRows(parent, parentItem->size(), parentItem->size());
    rootItem->append(item);
    endInsertRows();
}

void ChatRosterModel::clearPendingMessages(const QString &bareJid)
{
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
    {
        item->setData(MessagesRole, 0);
        emit dataChanged(createIndex(item->row(), ContactColumn, item),
                         createIndex(item->row(), SortingColumn, item));
    }
}

void ChatRosterModel::removeItem(const QString &bareJid)
{
    ChatRosterItem *item = rootItem->find(bareJid);
    if (item)
    {
        beginRemoveRows(QModelIndex(), item->row(), item->row());
        rootItem->remove(item);
        endRemoveRows();
    }
}

ChatRosterView::ChatRosterView(ChatRosterModel *model, QWidget *parent)
    : QTreeView(parent), rosterModel(model)
{
    QSortFilterProxyModel *sortedModel = new QSortFilterProxyModel(this);
    sortedModel->setSourceModel(model);
    sortedModel->setDynamicSortFilter(true);
    setModel(sortedModel);

    setAlternatingRowColors(true);
    setColumnHidden(SortingColumn, true);
    setColumnWidth(ImageColumn, 40);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDragEnabled(true);
    setDropIndicatorShown(false);
    setHeaderHidden(true);
    setIconSize(QSize(32, 32));
    setMinimumWidth(200);
    setRootIsDecorated(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSortingEnabled(true);
    sortByColumn(SortingColumn, Qt::AscendingOrder);
}

void ChatRosterView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex &index = currentIndex();
    if (!index.isValid())
        return;

    int type = index.data(ChatRosterModel::TypeRole).toInt();
    const QString bareJid = index.data(ChatRosterModel::IdRole).toString();
    
    // allow plugins to populate menu
    QMenu *menu = new QMenu(this);
    emit itemMenu(menu, index);

    // FIXME : is there a better way to test if a menu is empty?
    if (menu->sizeHint().height() > 4)
        menu->popup(event->globalPos());
    else
        delete menu;
}

void ChatRosterView::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void ChatRosterView::dragMoveEvent(QDragMoveEvent *event)
{
    // ignore by default
    event->ignore();

    // let plugins process event
    QModelIndex index = indexAt(event->pos());
    if (index.isValid())
        emit itemDrop(event, index);
}

void ChatRosterView::dropEvent(QDropEvent *event)
{
    // ignore by default
    event->ignore();

    // let plugins process event
    QModelIndex index = indexAt(event->pos());
    if (index.isValid())
        emit itemDrop(event, index);
}

void ChatRosterView::resizeEvent(QResizeEvent *e)
{
    QTreeView::resizeEvent(e);
    setColumnWidth(ContactColumn, e->size().width() - 40);
}

void ChatRosterView::selectContact(const QString &jid)
{
    for (int i = 0; i < model()->rowCount(); i++)
    {
        QModelIndex index = model()->index(i, 0);
        if (index.data(ChatRosterModel::IdRole).toString() == jid)
        {
            if (index != currentIndex())
                setCurrentIndex(index);
            return;
        }
    }
    setCurrentIndex(QModelIndex());
}

void ChatRosterView::selectionChanged(const QItemSelection & selected, const QItemSelection &deselected)
{
    foreach (const QModelIndex &index, selected.indexes())
        expand(index);
}

QSize ChatRosterView::sizeHint () const
{
    if (!model()->rowCount())
        return QTreeView::sizeHint();

    QSize hint(200, 0);
    hint.setHeight(model()->rowCount() * sizeHintForRow(0));
    return hint;
}

