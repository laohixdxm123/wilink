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

#include <QApplication>
#include <QAuthenticator>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDomDocument>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>

#include "qnetio/wallet.h"

#include "chat.h"
#include "config.h"
#include "application.h"
#include "diagnostics.h"
#include "photos.h"
#include "trayicon.h"
#include "updatesdialog.h"

static const QUrl baseUrl("https://www.wifirst.net/w/");
static const QString authSuffix = "@wifirst.net";
static int retryInterval = 15000;

TrayIcon::TrayIcon()
    : chat(NULL), diagnostics(NULL), photos(NULL),
    connected(false),
    refreshInterval(0)
{
    userAgent = QString(qApp->applicationName() + "/" + qApp->applicationVersion()).toAscii();

    /* initialise settings */
    settings = new QSettings(this);
    if (Application::isInstalled() && !settings->contains("OpenAtLogin"))
        openAtLogin(true);

    /* set icon */
#ifdef Q_WS_MAC
    setIcon(QIcon(":/wDesktop-black.png"));
#else
    setIcon(QIcon(":/wDesktop.png"));
#endif

    /* set initial menu */
    QMenu *menu = new QMenu;
    QAction *action = menu->addAction(QIcon(":/diagnostics.png"), tr("&Diagnostics"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(showDiagnostics()));

    action = menu->addAction(QIcon(":/remove.png"), tr("&Quit"));
    connect(action, SIGNAL(triggered(bool)), qApp, SLOT(quit()));
    setContextMenu(menu);

#ifndef Q_WS_MAC
    /* catch left clicks, except on OS X */
    connect(this, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(onActivated(QSystemTrayIcon::ActivationReason)));
#endif

    /* prepare network manager */
    network = new QNetworkAccessManager(this);
    connect(Wallet::instance(), SIGNAL(credentialsRequired(const QString&, QAuthenticator *)), this, SLOT(getCredentials(const QString&, QAuthenticator *)));
    connect(network, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)), Wallet::instance(), SLOT(onAuthenticationRequired(QNetworkReply*, QAuthenticator*)));

    /* prepare chat */
    chat = new Chat(this);

    /* prepare diagnostics */
    diagnostics = new Diagnostics;

    /* prepare updates */
    updates = new UpdatesDialog;
    updatesTimer = new QTimer(this);
    connect(updatesTimer, SIGNAL(timeout()), updates, SLOT(check()));

    /* fetch menu */
    QTimer::singleShot(1000, this, SLOT(fetchMenu()));
}

void TrayIcon::fetchIcon()
{
    if (icons.empty())
        return;

    QPair<QUrl, QAction*> entry = icons.first();
    QNetworkRequest req(entry.first);
    req.setRawHeader("User-Agent", userAgent);
    QNetworkReply *reply = network->get(req);
    connect(reply, SIGNAL(finished()), this, SLOT(showIcon()));
}

void TrayIcon::fetchMenu()
{
    QNetworkRequest req(baseUrl);
    req.setRawHeader("Accept", "application/xml");
    req.setRawHeader("Accept-Language", QLocale::system().name().toAscii());
    req.setRawHeader("User-Agent", userAgent);
    QNetworkReply *reply = network->get(req);
    connect(reply, SIGNAL(finished()), this, SLOT(showMenu()));
}

/** Prompt the user for credentials.
 */
void TrayIcon::getCredentials(const QString &realm, QAuthenticator *authenticator)
{
    const QString prompt = QString("Please enter your credentials for '%1'.").arg(realm);

    /* create dialog */
    QDialog *dialog = new QDialog;
    QGridLayout *layout = new QGridLayout;

    layout->addWidget(new QLabel(prompt), 0, 0, 1, 2);

    layout->addWidget(new QLabel("User:"), 1, 0);
    QLineEdit *user = new QLineEdit();
    layout->addWidget(user, 1, 1);

    layout->addWidget(new QLabel("Password:"), 2, 0);
    QLineEdit *password = new QLineEdit();
    password->setEchoMode(QLineEdit::Password);
    layout->addWidget(password, 2, 1);

    QPushButton *btn = new QPushButton("OK");
    dialog->connect(btn, SIGNAL(clicked()), dialog, SLOT(accept()));
    layout->addWidget(btn, 3, 1);

    dialog->setLayout(layout);

    /* prompt user */
    while (user->text().isEmpty() || password->text().isEmpty())
    {
        if (!dialog->exec())
            return;
    }
    QString userName = user->text();
    if (realm == baseUrl.host() && !userName.endsWith(authSuffix))
        userName += authSuffix;
    authenticator->setUser(userName);
    authenticator->setPassword(password->text());

    /* store credentials */
    QNetIO::Wallet::instance()->setCredentials(realm, userName, password->text());
}

void TrayIcon::openUrl()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action)
        QDesktopServices::openUrl(action->data().toUrl());
}

void TrayIcon::showIcon()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    Q_ASSERT(reply != NULL);

    /* display current icon */
    QPair<QUrl, QAction*> entry = icons.takeFirst();
    QPixmap pixmap;
    QByteArray data = reply->readAll();
    if (!pixmap.loadFromData(data, 0))
    {
        qWarning() << "could not load icon" << entry.first;
        return;
    }
    entry.second->setIcon(QIcon(pixmap));

    /* fetch next icon */
    fetchIcon();
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
    {
        showChat();
#if 0
        // FIXME: this implies that the system tray is at the bottom of the screen..
        QMenu *menu = contextMenu();
        QPoint delta = QPoint(0, menu->sizeHint().height());
        menu->popup(geometry().topLeft() - delta);
#endif
    }
}

void TrayIcon::openAtLogin(bool checked)
{
    Application::setOpenAtLogin(checked);
    settings->setValue("OpenAtLogin", checked);
}

void TrayIcon::showChat()
{
    chat->show();
    chat->raise();
}

void TrayIcon::showDiagnostics()
{
    diagnostics->show();
    diagnostics->raise();
}

void TrayIcon::showMenu()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    Q_ASSERT(reply != NULL);

    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning("Failed to retrieve menu");
        QTimer::singleShot(retryInterval, this, SLOT(fetchMenu()));
        return;
    }

    QMenu *menu = new QMenu;
    QAction *action;

    /* parse menu */
    QDomDocument doc;
    doc.setContent(reply);
    QDomElement item = doc.documentElement().firstChildElement("menu").firstChildElement("menu");
    while (!item.isNull())
    {
        const QString image = item.firstChildElement("image").text();
        const QString link = item.firstChildElement("link").text();
        const QString text = item.firstChildElement("label").text();
        if (text.isEmpty())
            menu->addSeparator();
        else {
            action = menu->addAction(text);
            action->setData(baseUrl.resolved(link));
            connect(action, SIGNAL(triggered(bool)), this, SLOT(openUrl()));
            /* schedule retrieval of icon */
            if (!image.isEmpty())
                icons.append(QPair<QUrl, QAction*>(baseUrl.resolved(image), action));
        }
        item = item.nextSiblingElement("menu");
    }

    /* add static entries */
    if (!menu->isEmpty())
        menu->addSeparator();

    action = menu->addAction(QIcon(":/chat.png"), tr("&Chat"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(showChat()));

    action = menu->addAction(QIcon(":/photos.png"), tr("Upload &photos"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(showPhotos()));

    if (Application::isInstalled())
    {
        QMenu *optionsMenu = new QMenu;

        action = optionsMenu->addAction(tr("Open at login"));
        action->setCheckable(true);
        action->setChecked(settings->value("OpenAtLogin").toBool());
        connect(action, SIGNAL(toggled(bool)), this, SLOT(openAtLogin(bool)));

        action = menu->addAction(QIcon(":/options.png"), tr("&Options"));
        action->setMenu(optionsMenu);
    }

    action = menu->addAction(QIcon(":/diagnostics.png"), tr("&Diagnostics"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(showDiagnostics()));

    action = menu->addAction(QIcon(":/remove.png"), tr("&Quit"));
    connect(action, SIGNAL(triggered(bool)), qApp, SLOT(quit()));
    setContextMenu(menu);

    /* parse messages */
    item = doc.documentElement().firstChildElement("messages").firstChildElement("message");
    while (!item.isNull())
    {
        const QString id = item.firstChildElement("id").text();
        const QString title = item.firstChildElement("title").text();
        const QString text = item.firstChildElement("text").text();
        const int type = item.firstChildElement("type").text().toInt();
        enum QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
        switch (type)
        {
            case 3: icon = QSystemTrayIcon::Critical; break;
            case 2: icon = QSystemTrayIcon::Warning; break;
            case 1: icon = QSystemTrayIcon::Information; break;
            case 0: icon = QSystemTrayIcon::NoIcon; break;
        }
        if (!seenMessages.contains(id))
        {
            showMessage(title, text, icon);
            seenMessages.append(id);
        }
        item = item.nextSiblingElement("message");
    }

    /* parse preferences */
    item = doc.documentElement().firstChildElement("preferences");
    refreshInterval = item.firstChildElement("refresh").text().toInt() * 1000;
    QString urlString = item.firstChildElement("updates").text();
    if (!urlString.isEmpty())
        updates->setUrl(baseUrl.resolved(QUrl(urlString)));
    if (refreshInterval > 0)
        QTimer::singleShot(refreshInterval, this, SLOT(fetchMenu()));

    // FIXME : not yet tested !
    urlString = item.firstChildElement("diagnostics").text();
    if (!urlString.isEmpty()) {
        qDebug() << urlString << "used to report diagnostics";
        diagnostics->setUrl(baseUrl.resolved(QUrl(urlString)));
    } else {
        diagnostics->setUrl(QUrl("http://127.0.0.1:8000")); // FIXME: only for testing. To be removed !
    }

    /* fetch icons */
    fetchIcon();

    /* connect to chat and check for updates */
    if (!connected)
    {
        connected = true;

        /* connect to chat */
        QAuthenticator auth;
        Wallet::instance()->onAuthenticationRequired(baseUrl.host(), &auth);
        chat->open(auth.user(), auth.password());
        showChat();

        /* check for updates now then every week */
        updates->check();
        updatesTimer->start(7 * 24 * 3600 * 1000);
    }
}

void TrayIcon::showPhotos()
{
    if (!photos)
    {
        QAction *action = qobject_cast<QAction *>(sender());
        photos = new Photos("wifirst://www.wifirst.net/w");
        photos->setSystemTrayIcon(this);
    }
    photos->show();
    photos->raise();
}

