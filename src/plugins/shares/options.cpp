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

#include <QDesktopServices>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QTreeView>


#include "plugins/application.h"
#include "options.h"

FolderModel::FolderModel(QObject *parent)
    : QFileSystemModel(parent)
{
    // set role names
    QHash<int, QByteArray> names;
    names.insert(QFileSystemModel::FileNameRole, "name");
    names.insert(QFileSystemModel::FilePathRole, "path");
    setRoleNames(names);

    setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot);
    setRootPath(QDir::rootPath());
    setReadOnly(true);
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::CheckStateRole && index.isValid() && !index.column())
    {
        const QString path = filePath(index);
        if (path == QDir::rootPath() || !QFileInfo(path).isDir())
            return QVariant();

        Qt::CheckState state = Qt::Unchecked;
        QStringList allSelected = m_selected;
        allSelected << m_forced;
        for (int i = 0; i < allSelected.size(); ++i)
        {
            const QString currentPath = allSelected[i];
            if (currentPath == path)
                return Qt::Checked;
            else if (currentPath.startsWith(path + "/"))
                state = Qt::PartiallyChecked;
            else if (path.startsWith(currentPath + "/"))
                return QVariant();
        }
        return state;
    } else
        return QFileSystemModel::data(index, role);
}

bool FolderModel::setData(const QModelIndex &changedIndex, const QVariant &value, int role)
{
    if (role == Qt::CheckStateRole && changedIndex.isValid() && !changedIndex.column())
    {
        const QString changedPath = filePath(changedIndex);
        if (changedPath == QDir::rootPath() || !QFileInfo(changedPath).isDir())
            return false;

        if (changedPath == m_forced ||
            changedPath.startsWith(m_forced + "/"))
            return false;

        if (value.toInt() == Qt::Checked)
        {
            // unselect any children or parents
            for (int i = m_selected.size() - 1; i >= 0; --i)
            {
                const QString currentPath = m_selected[i];
                if (currentPath.startsWith(changedPath + "/") ||
                    changedPath.startsWith(currentPath + "/"))
                    m_selected.removeAt(i);
            }
            if (!m_selected.contains(changedPath))
                m_selected << changedPath;
        }
        else
        {
            m_selected.removeAll(changedPath);
        }

        // refresh items which have changed
        QModelIndex idx = changedIndex;
        while (idx.isValid())
        {
            emit dataChanged(idx, idx);
            idx = idx.parent();
        }

        // refresh children of the changed index
        emit dataChanged(
            index(0, 0, changedIndex),
            index(rowCount(changedIndex), columnCount(changedIndex), changedIndex));

        emit selectedFoldersChanged(m_selected);
        return true;
    } else
        return false;
}

Qt::ItemFlags FolderModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QFileSystemModel::flags(index);
    flags |= Qt::ItemIsUserCheckable;
    return flags;
}

QString FolderModel::forcedFolder() const
{
    return m_forced;
}

void FolderModel::setForcedFolder(const QString &forced)
{
    if (forced == m_forced)
        return;

    // make a note of changed directories
    QStringList changed;
    if (!m_forced.isEmpty())
        changed << m_forced;
    changed << forced;
    for (int i = m_selected.size() - 1; i >= 0; --i)
    {
        const QString currentPath = m_selected[i];
        if (currentPath == forced ||
            currentPath.startsWith(forced + "/"))
        {
            changed << currentPath;
            m_selected.removeAt(i);
            emit selectedFoldersChanged(m_selected);
        }
    }

    m_forced = forced;

    // emit changes
    foreach (const QString changedPath, changed)
    {
        QModelIndex idx = index(changedPath);
        emit dataChanged(idx, idx);
    }
    emit forcedFolderChanged(m_forced);
}

QStringList FolderModel::selectedFolders() const
{
    return m_selected;
}

void FolderModel::setSelectedFolders(const QStringList &selected)
{
    m_selected = selected;
}

PlaceModel::PlaceModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
    QList<QDesktopServices::StandardLocation> locations;
    locations << QDesktopServices::DocumentsLocation;
    locations << QDesktopServices::MusicLocation;
    locations << QDesktopServices::MoviesLocation;
    locations << QDesktopServices::PicturesLocation;
    foreach (QDesktopServices::StandardLocation location, locations)
    {
        const QString path = QDesktopServices::storageLocation(location);
        QDir dir(path);
        if (path.isEmpty() || dir == QDir::home())
            continue;
        m_paths << path;
    }
}

QModelIndex PlaceModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= m_paths.size())
        return QModelIndex();

    return createIndex(row, column, 0);
}

QModelIndex PlaceModel::parent(const QModelIndex &index) const
{
    return QModelIndex();
}

int PlaceModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

int PlaceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    else
        return m_paths.size();
}

QModelIndex PlaceModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid())
        return QModelIndex();

    const QString path = sourceIndex.data(QFileSystemModel::FilePathRole).toString();
    const int row = m_paths.indexOf(path);
    if (row < 0)
        return QModelIndex();
    else
        return createIndex(row, sourceIndex.column(), 0);
}

QModelIndex PlaceModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid())
        return QModelIndex();

    const int row = proxyIndex.row();
    if (row < 0 || row >= m_paths.size())
        return QModelIndex();
    else
        return m_fsModel->index(m_paths.at(row));
}

void PlaceModel::sourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    QModelIndex proxyTopLeft = mapFromSource(topLeft);
    QModelIndex proxyBottomRight = mapFromSource(bottomRight);
    if (proxyTopLeft.isValid() && proxyBottomRight.isValid())
        emit dataChanged(proxyTopLeft, proxyBottomRight);
}

FolderModel *PlaceModel::sourceModel() const
{
    return m_fsModel;
}

void PlaceModel::setSourceModel(FolderModel *sourceModel)
{
    if (sourceModel != m_fsModel) {
        m_fsModel = sourceModel;

        if (m_fsModel) {
            connect(m_fsModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)),
                    this, SLOT(sourceDataChanged(QModelIndex,QModelIndex)));
            QAbstractProxyModel::setSourceModel(m_fsModel);
        }
        emit sourceModelChanged(m_fsModel);
    }
}

ShareOptions::ShareOptions()
{
    QVBoxLayout *layout = new QVBoxLayout;
    QGroupBox *sharesGroup = new QGroupBox(tr("Shared folders"));
    QVBoxLayout *vbox = new QVBoxLayout;

    QLabel *sharesLabel = new QLabel(tr("Select the folders you want to share. The files you share will only be visible in your residence, they can never be accessed outside your residence."));
    sharesLabel->setWordWrap(true);
    vbox->addWidget(sharesLabel);

    // full view
    m_fsModel = new FolderModel(this);
    m_fsModel->setForcedFolder(wApp->sharesLocation());
    m_fsModel->setSelectedFolders(wApp->sharesDirectories());
    m_fsView = new QTreeView;
    m_fsView->setModel(m_fsModel);
    m_fsView->setColumnHidden(1, true);
    m_fsView->setColumnHidden(2, true);
    m_fsView->setColumnHidden(3, true);
    m_fsView->hide();
    vbox->addWidget(m_fsView);

    // simple view
    m_placesModel = new PlaceModel(this);
    m_placesModel->setSourceModel(m_fsModel);
    m_placesView = new QTreeView;
    m_placesView->setModel(m_placesModel);
    vbox->addWidget(m_placesView);

    QHBoxLayout *toggleBox = new QHBoxLayout;
    toggleBox->setMargin(0);
    toggleBox->addStretch();
    m_moreButton = new QPushButton(tr("More folders.."));
    connect(m_moreButton, SIGNAL(clicked()), this, SLOT(moreFolders()));
    toggleBox->addWidget(m_moreButton);
    m_fewerButton = new QPushButton(tr("Fewer folders.."));
    m_fewerButton->hide();
    connect(m_fewerButton, SIGNAL(clicked()), this, SLOT(fewerFolders()));
    toggleBox->addWidget(m_fewerButton);
    vbox->addLayout(toggleBox);

    sharesGroup->setLayout(vbox);
    layout->addWidget(sharesGroup);

    // downloads folder
    QGroupBox *downloadsGroup = new QGroupBox(tr("Downloads folder"));
    vbox = new QVBoxLayout;

    QLabel *downloadsLabel = new QLabel(tr("Select the folder in which received files will be stored."));
    downloadsLabel->setWordWrap(true);
    vbox->addWidget(downloadsLabel);

    QHBoxLayout *hbox = new QHBoxLayout;
    hbox->setMargin(0);
    m_directoryEdit = new QLineEdit;
    m_directoryEdit->setText(wApp->sharesLocation());
    m_directoryEdit->setEnabled(false);
    hbox->addWidget(m_directoryEdit);
    QPushButton *directoryButton = new QPushButton;
    directoryButton->setIcon(QIcon(":/album.png"));
    connect(directoryButton, SIGNAL(clicked()), this, SLOT(browse()));
    hbox->addWidget(directoryButton);
    vbox->addLayout(hbox);

    downloadsGroup->setLayout(vbox);
    layout->addWidget(downloadsGroup);

    setLayout(layout);
    setObjectName("shares");
    setWindowIcon(QIcon(":/share.png"));
    setWindowTitle(tr("Shares"));
}

void ShareOptions::browse()
{
    QFileDialog *dialog = new QFileDialog(this);
    dialog->setDirectory(m_directoryEdit->text());
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setWindowTitle(tr("Shares folder"));
    dialog->show();

    bool check;
    check = connect(dialog, SIGNAL(finished(int)),
                    dialog, SLOT(deleteLater()));
    Q_ASSERT(check);

    check = connect(dialog, SIGNAL(fileSelected(QString)),
                    this, SLOT(directorySelected(QString)));
    Q_ASSERT(check);
}

void ShareOptions::directorySelected(const QString &path)
{
    m_directoryEdit->setText(path);
    m_fsModel->setForcedFolder(path);
}

void ShareOptions::fewerFolders()
{
    m_fsView->hide();
    m_placesView->show();
    m_fewerButton->hide();
    m_moreButton->show();
}

void ShareOptions::moreFolders()
{
    m_placesView->hide();
    m_fsView->show();
    m_moreButton->hide();
    m_fewerButton->show();
}

bool ShareOptions::save()
{
    const QString path = m_directoryEdit->text();
    const QStringList mapped = m_fsModel->selectedFolders();

    // remember directory
    wApp->setSharesLocation(path);
    wApp->setSharesDirectories(mapped);
    return true;
}

void ShareOptions::scrollToHome()
{
    // scroll to home
    QModelIndex homeIndex = m_fsModel->index(QDir::homePath());
    m_fsView->setExpanded(homeIndex, true);
    m_fsView->scrollTo(homeIndex, QAbstractItemView::PositionAtTop);
}

void ShareOptions::showEvent(QShowEvent *event)
{
    ChatPreferencesTab::showEvent(event);
    QTimer::singleShot(0, this, SLOT(scrollToHome()));
}

