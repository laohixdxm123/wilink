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

#include <QApplication>
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAuthenticator>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeItem>
#include <QDeclarativeNetworkAccessManagerFactory>
#include <QDeclarativeView>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMenuBar>
#include <QMessageBox>
#include <QPluginLoader>
#include <QPushButton>
#include <QShortcut>
#include <QStringList>
#include <QTimer>

#include "QSoundMeter.h"
#include "QSoundPlayer.h"

#include "QXmppCallManager.h"
#include "QXmppConfiguration.h"
#include "QXmppConstants.h"
#include "QXmppDiscoveryManager.h"
#include "QXmppLogger.h"
#include "QXmppMessage.h"
#include "QXmppMucManager.h"
#include "QXmppRosterIq.h"
#include "QXmppRosterManager.h"
#include "QXmppRtpChannel.h"
#include "QXmppTransferManager.h"
#include "QXmppUtils.h"

#include "application.h"
#include "chat.h"
#include "chat_accounts.h"
#include "chat_client.h"
#include "chat_history.h"
#include "chat_plugin.h"
#include "chat_roster.h"
#include "chat_status.h"
#include "chat_utils.h"
#include "plugins/console.h"
#include "plugins/conversations.h"
#include "plugins/declarative.h"
#include "plugins/diagnostics.h"
#include "plugins/discovery.h"
#include "plugins/phone.h"
#include "plugins/phone/sip.h"
#include "plugins/photos.h"
#include "plugins/player.h"
#include "plugins/rooms.h"
#include "plugins/shares/model.h"
#include "systeminfo.h"
#include "updatesdialog.h"

class NetworkAccessManagerFactory : public QDeclarativeNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject * parent)
    {
        return new NetworkAccessManager(parent);
    }
};

class ChatPrivate
{
public:
    QAction *findAction;
    QAction *findAgainAction;

    ChatClient *client;
    ChatRosterModel *rosterModel;
    QDeclarativeView *rosterView;
    QString windowTitle;

    QList<ChatPlugin*> plugins;
};

Chat::Chat(QWidget *parent)
    : QMainWindow(parent),
    d(new ChatPrivate)
{
    bool check;

    qmlRegisterUncreatableType<QXmppClient>("QXmpp", 0, 4, "QXmppClient", "");
    qmlRegisterUncreatableType<QXmppCall>("QXmpp", 0, 4, "QXmppCall", "");
    qmlRegisterUncreatableType<QXmppCallManager>("QXmpp", 0, 4, "QXmppCallManager", "");
    qmlRegisterUncreatableType<DiagnosticManager>("QXmpp", 0, 4, "DiagnosticManager", "");
    qmlRegisterUncreatableType<QXmppDiscoveryManager>("QXmpp", 0, 4, "QXmppDiscoveryManager", "");
    qmlRegisterUncreatableType<QXmppLogger>("QXmpp", 0, 4, "QXmppLogger", "");
    qmlRegisterType<QXmppDeclarativeMessage>("QXmpp", 0, 4, "QXmppMessage");
    qmlRegisterUncreatableType<QXmppMucManager>("QXmpp", 0, 4, "QXmppMucManager", "");
    qmlRegisterUncreatableType<QXmppMucRoom>("QXmpp", 0, 4, "QXmppMucRoom", "");
    qmlRegisterUncreatableType<QXmppRosterManager>("QXmpp", 0, 4, "QXmppRosterManager", "");
    qmlRegisterUncreatableType<QXmppRtpAudioChannel>("QXmpp", 0, 4, "QXmppRtpAudioChannel", "");
    qmlRegisterUncreatableType<QXmppTransferJob>("QXmpp", 0, 4, "QXmppTransferJob", "");
    qmlRegisterUncreatableType<QXmppTransferManager>("QXmpp", 0, 4, "QXmppTransferManager", "");

    qmlRegisterUncreatableType<ChatClient>("wiLink", 1, 2, "Client", "");
    qmlRegisterType<Conversation>("wiLink", 1, 2, "Conversation");
    qmlRegisterType<DiscoveryModel>("wiLink", 1, 2, "DiscoveryModel");
    qmlRegisterUncreatableType<ChatHistoryModel>("wiLink", 1, 2, "HistoryModel", "");
    qmlRegisterType<ListHelper>("wiLink", 1, 2, "ListHelper");
    qmlRegisterType<LogModel>("wiLink", 1, 2, "LogModel");
    qmlRegisterType<PhoneCallsModel>("wiLink", 1, 2, "PhoneCallsModel");
    qmlRegisterUncreatableType<SipClient>("wiLink", 1, 2, "SipClient", "");
    qmlRegisterUncreatableType<SipCall>("wiLink", 1, 2, "SipCall", "");
    qmlRegisterUncreatableType<PhotoUploadModel>("wiLink", 1, 2, "PhotoUploadModel", "");
    qmlRegisterType<PhotoModel>("wiLink", 1, 2, "PhotoModel");
    qmlRegisterType<PlayerModel>("wiLink", 1, 2, "PlayerModel");
    qmlRegisterType<RoomModel>("wiLink", 1, 2, "RoomModel");
    qmlRegisterType<RoomListModel>("wiLink", 1, 2, "RoomListModel");
    qmlRegisterUncreatableType<ChatRosterModel>("wiLink", 1, 2, "RosterModel", "");
    qmlRegisterType<ShareModel>("wiLink", 1, 2, "ShareModel");
    qmlRegisterUncreatableType<QSoundPlayer>("wiLink", 1, 2, "SoundPlayer", "");
    qmlRegisterType<VCard>("wiLink", 1, 2, "VCard");
    qmlRegisterUncreatableType<Chat>("wiLink", 1, 2, "Window", "");

    // crutches for Qt..
    qmlRegisterUncreatableType<QAbstractItemModel>("wiLink", 1, 2, "QAbstractItemModel", "");
    qmlRegisterUncreatableType<QFileDialog>("wiLink", 1, 2, "QFileDialog", "");
    qmlRegisterUncreatableType<QMessageBox>("wiLink", 1, 2, "QMessageBox", "");
    qmlRegisterType<QDeclarativeSortFilterProxyModel>("wiLink", 1, 2, "SortFilterProxyModel");

    // create client
    d->client = new ChatClient(this);
    d->rosterModel =  new ChatRosterModel(d->client, this);
    connect(d->rosterModel, SIGNAL(pendingMessages(int)), this, SLOT(pendingMessages(int)));

    QXmppLogger *logger = new QXmppLogger(this);
    logger->setLoggingType(QXmppLogger::SignalLogging);
    d->client->setLogger(logger);

    // create gui
    QWidget *centralWidget = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout;
    layout->setMargin(0);
    layout->setSpacing(0);
    centralWidget->setLayout(layout);

    // create declarative view
    d->rosterView = new QDeclarativeView;
    d->rosterView->setMinimumWidth(240);
    d->rosterView->setResizeMode(QDeclarativeView::SizeRootObjectToView);
    d->rosterView->engine()->addImageProvider("photo", new PhotoImageProvider);
    d->rosterView->engine()->addImageProvider("roster", new ChatRosterImageProvider);
    d->rosterView->engine()->setNetworkAccessManagerFactory(new NetworkAccessManagerFactory);

    d->rosterView->setAttribute(Qt::WA_OpaquePaintEvent);
    d->rosterView->setAttribute(Qt::WA_NoSystemBackground);
    d->rosterView->viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    d->rosterView->viewport()->setAttribute(Qt::WA_NoSystemBackground);

    QDeclarativeContext *context = d->rosterView->rootContext();
    context->setContextProperty("application", wApp);
    context->setContextProperty("window", this);

    d->rosterView->setSource(QUrl("qrc:/main.qml"));

    layout->addWidget(d->rosterView);
    layout->addWidget(new ChatStatus(d->client));
    setCentralWidget(centralWidget);

    /* "File" menu */
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *action = fileMenu->addAction(QIcon(":/options.png"), tr("&Preferences"));
    action->setMenuRole(QAction::PreferencesRole);
    connect(action, SIGNAL(triggered()), this, SLOT(showPreferences()));

    action = fileMenu->addAction(QIcon(":/chat.png"), tr("Chat accounts"));
    connect(action, SIGNAL(triggered(bool)), qApp, SLOT(showAccounts()));

    if (wApp->updatesDialog())
    {
        action = fileMenu->addAction(QIcon(":/refresh.png"), tr("Check for &updates"));
        connect(action, SIGNAL(triggered(bool)), wApp->updatesDialog(), SLOT(check()));
    }

    action = fileMenu->addAction(QIcon(":/close.png"), tr("&Quit"));
    action->setMenuRole(QAction::QuitRole);
    action->setShortcut(QKeySequence(Qt::ControlModifier + Qt::Key_Q));
    connect(action, SIGNAL(triggered(bool)), qApp, SLOT(quit()));

    /* "Edit" menu */
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));

    d->findAction = editMenu->addAction(QIcon(":/search.png"), tr("&Find"));
    d->findAction->setEnabled(false);
    d->findAction->setShortcut(QKeySequence(Qt::ControlModifier + Qt::Key_F));

    d->findAgainAction = editMenu->addAction(tr("Find a&gain"));
    d->findAgainAction->setShortcut(QKeySequence(Qt::ControlModifier + Qt::Key_G));
    d->findAgainAction->setEnabled(false);

    /* set up client */
    connect(d->client, SIGNAL(error(QXmppClient::Error)), this, SLOT(error(QXmppClient::Error)));

    /* set up keyboard shortcuts */
#ifdef Q_OS_MAC
    QShortcut *shortcut = new QShortcut(QKeySequence(Qt::ControlModifier + Qt::Key_W), this);
    connect(shortcut, SIGNAL(activated()), this, SLOT(close()));
#endif

    // resize
    QSize size = QApplication::desktop()->availableGeometry(this).size();
    size.setHeight(size.height() - 100);
    size.setWidth((size.height() * 4.0) / 3.0);
    resize(size);
}

Chat::~Chat()
{
    // disconnect
    d->client->disconnectFromServer();

    // unload plugins
    for (int i = d->plugins.size() - 1; i >= 0; i--)
        d->plugins[i]->finalize(this);

    delete d;
}

void Chat::alert()
{
    // show the chat window
    if (!isVisible()) {
#ifdef Q_OS_MAC
        show();
#else
        showMinimized();
#endif
    }

    /* NOTE : in Qt built for Mac OS X using Cocoa, QApplication::alert
     * only causes the dock icon to bounce for one second, instead of
     * bouncing until the user focuses the window. To work around this
     * we implement our own version.
     */
    wApp->alert(this);
}

void Chat::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange)
        emit isActiveWindowChanged();
}

/** Handle an error talking to the chat server.
 *
 * @param error
 */
void Chat::error(QXmppClient::Error error)
{
    if(error == QXmppClient::XmppStreamError)
    {
        if (d->client->xmppStreamError() == QXmppStanza::Error::Conflict)
        {
            // if we received a resource conflict, exit
            qWarning("Received a resource conflict from chat server");
            qApp->quit();
        }
        else if (d->client->xmppStreamError() == QXmppStanza::Error::NotAuthorized)
        {
            // prompt user for credentials at the next main loop execution
            QTimer::singleShot(0, this, SLOT(promptCredentials()));
        }
    }
}

/** The number of pending messages changed.
 */
void Chat::pendingMessages(int messages)
{
    QString title = d->windowTitle;
    if (messages)
        title += " - " + tr("%n message(s)", "", messages);
    QWidget::setWindowTitle(title);
}

/** Prompt for credentials then connect.
 */
void Chat::promptCredentials()
{
    QXmppConfiguration config = d->client->configuration();
    QString password = config.password();
    if (ChatAccounts::getPassword(config.jidBare(), password, this) &&
        password != config.password())
    {
        config.setPassword(password);
        d->client->connectToServer(config);
    }
}

QFileDialog *Chat::fileDialog()
{
    QFileDialog *dialog = new QDeclarativeFileDialog(this);
    return dialog;
}

QMessageBox *Chat::messageBox()
{
    QMessageBox *box = new QMessageBox(this);
    box->setIcon(QMessageBox::Question);
    return box;
}

/** Return this window's chat client.
 */
ChatClient *Chat::client()
{
    return d->client;
}

/** Return this window's chat roster model.
 */
ChatRosterModel *Chat::rosterModel()
{
    return d->rosterModel;
}

/** Open the connection to the chat server.
 *
 * @param jid
 */
bool Chat::open(const QString &jid)
{
    QXmppConfiguration config;
    config.setResource(qApp->applicationName());

    /* get user and domain */
    if (!isBareJid(jid))
    {
        qWarning("Cannot connect to chat server using invalid JID");
        return false;
    }
    config.setJid(jid);
    setObjectName(config.jidBare());

    /* get password */
    QString password;
    if (!ChatAccounts::getPassword(config.jidBare(), password, this))
    {
        qWarning("Cannot connect to chat server without a password");
        return false;
    }
    config.setPassword(password);

    /* set security parameters */
    if (config.domain() == QLatin1String("wifirst.net"))
    {
        config.setStreamSecurityMode(QXmppConfiguration::TLSRequired);
        config.setIgnoreSslErrors(false);
    }

    /* set keep alive */
    config.setKeepAliveTimeout(15);

    /* connect to server */
    d->client->connectToServer(config);

    /* load plugins */
    QObjectList plugins = QPluginLoader::staticInstances();
    foreach (QObject *object, plugins)
    {
        ChatPlugin *plugin = qobject_cast<ChatPlugin*>(object);
        if (plugin)
        {
            plugin->initialize(this);
            d->plugins << plugin;
        }
    }

    /* Create "Help" menu here, so that it remains last */
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    QAction *action = helpMenu->addAction(tr("%1 FAQ").arg(qApp->applicationName()));
#ifdef Q_OS_MAC
    action->setShortcut(QKeySequence(Qt::ControlModifier + Qt::Key_Question));
#else
    action->setShortcut(QKeySequence(Qt::Key_F1));
#endif
    connect(action, SIGNAL(triggered(bool)), this, SLOT(showHelp()));

    action = helpMenu->addAction(tr("About %1").arg(qApp->applicationName()));
    action->setMenuRole(QAction::AboutRole);
    connect(action, SIGNAL(triggered(bool)), this, SLOT(showAbout()));

    return true;
}

/** When asked to open an XMPP URI, let plugins handle it.
 */
void Chat::openUrl(const QUrl &url)
{
    emit urlClick(url);
}

void Chat::setWindowTitle(const QString &title)
{
    d->windowTitle = title;
    QWidget::setWindowTitle(title);
}

static QLayout *aboutBox()
{
    QHBoxLayout *hbox = new QHBoxLayout;
    QLabel *icon = new QLabel;
    icon->setPixmap(QPixmap(":/wiLink-64.png"));
    hbox->addWidget(icon);
    hbox->addSpacing(20);
    hbox->addWidget(new QLabel(QString("<p style=\"font-size: xx-large;\">%1</p>"
        "<p style=\"font-size: large;\">%2</p>")
        .arg(qApp->applicationName(),
            QString("version %1").arg(qApp->applicationVersion()))));
    return hbox;
}

/** Display an "about" dialog.
 */
void Chat::showAbout()
{
    QDialog dlg;
    dlg.setWindowTitle(tr("About %1").arg(qApp->applicationName()));

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addLayout(aboutBox());
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttonBox, SIGNAL(accepted()), &dlg, SLOT(accept()));
    layout->addWidget(buttonBox);
    dlg.setLayout(layout);
    dlg.exec();
}

/** Display the help web page.
 */
void Chat::showHelp()
{
    QDesktopServices::openUrl(QUrl(HELP_URL));
}

/** Display the preferenes dialog.
 */
void Chat::showPreferences(const QString &focusTab)
{
    ChatPreferences *dialog = new ChatPreferences(this);
    dialog->setWindowModality(Qt::WindowModal);

    connect(dialog, SIGNAL(finished(int)),
            dialog, SLOT(deleteLater()));

    dialog->addTab(new ChatOptions);
    dialog->addTab(new SoundOptions);
    foreach (ChatPlugin *plugin, d->plugins)
        plugin->preferences(dialog);

    dialog->setCurrentTab(focusTab);
#ifdef WILINK_EMBEDDED
    dialog->showMaximized();
#else
    dialog->show();
#endif
}

ChatOptions::ChatOptions()
{
    QVBoxLayout *layout = new QVBoxLayout;

    // GENERAL OPTIONS
    QGroupBox *group = new QGroupBox(tr("General options"));
    QVBoxLayout *vbox = new QVBoxLayout;
    group->setLayout(vbox);
    layout->addWidget(group);

    if (wApp->isInstalled())
    {
        openAtLogin = new QCheckBox(tr("Open at login"));
        openAtLogin->setChecked(wApp->openAtLogin());
        vbox->addWidget(openAtLogin);
    } else {
        openAtLogin = 0;
    }

    showOfflineContacts = new QCheckBox(tr("Show offline contacts"));
    showOfflineContacts->setChecked(wApp->showOfflineContacts());
    vbox->addWidget(showOfflineContacts);

    // ABOUT
    group = new QGroupBox(tr("About %1").arg(wApp->applicationName()));
    group->setLayout(aboutBox());
    layout->addWidget(group);

    setLayout(layout);
    setWindowIcon(QIcon(":/options.png"));
    setWindowTitle(tr("General"));
}

bool ChatOptions::save()
{
    if (openAtLogin)
        wApp->setOpenAtLogin(openAtLogin->isChecked());
    wApp->setShowOfflineContacts(showOfflineContacts->isChecked());
    return true;
}

PluginOptions::PluginOptions()
{
    QVBoxLayout *layout = new QVBoxLayout;

    QGroupBox *group = new QGroupBox(tr("Plugins"));
    QVBoxLayout *box = new QVBoxLayout;
    group->setLayout(box);
    QObjectList plugins = QPluginLoader::staticInstances();
    foreach (QObject *object, plugins)
    {
        ChatPlugin *plugin = qobject_cast<ChatPlugin*>(object);
        if (plugin) {
            box->addWidget(new QLabel(plugin->name()));
        }
    }
    layout->addWidget(group);

    setLayout(layout);
    setWindowIcon(QIcon(":/plugin.png"));
    setWindowTitle(tr("Plugins"));
}

#define SOUND_TEST_SECONDS 5

SoundOptions::SoundOptions()
{
    QVBoxLayout *layout = new QVBoxLayout;

    // DEVICES
    QGroupBox *group = new QGroupBox(tr("Sound devices"));
    QGridLayout *devicesLayout = new QGridLayout;
    devicesLayout->setColumnStretch(2, 1);
    group->setLayout(devicesLayout);
    layout->addWidget(group);

    // output
    QLabel *label = new QLabel;
    label->setPixmap(QPixmap(":/audio-output.png"));
    devicesLayout->addWidget(label, 0, 0);
    devicesLayout->addWidget(new QLabel(tr("Audio playback device")), 0, 1);
    outputDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    outputCombo = new QComboBox;
    foreach (const QAudioDeviceInfo &info, outputDevices) {
        outputCombo->addItem(info.deviceName());
        if (info.deviceName() == wApp->audioOutputDevice().deviceName())
            outputCombo->setCurrentIndex(outputCombo->count() - 1);
    }
    devicesLayout->addWidget(outputCombo, 0, 2);

    // input
    label = new QLabel;
    label->setPixmap(QPixmap(":/audio-input.png"));
    devicesLayout->addWidget(label, 1, 0);
    devicesLayout->addWidget(new QLabel(tr("Audio capture device")), 1, 1);
    inputDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    inputCombo = new QComboBox;
    foreach (const QAudioDeviceInfo &info, inputDevices) {
        inputCombo->addItem(info.deviceName());
        if (info.deviceName() == wApp->audioInputDevice().deviceName())
            inputCombo->setCurrentIndex(inputCombo->count() - 1);
    }
    devicesLayout->addWidget(inputCombo, 1, 2);

    // test
    testBuffer = new QBuffer(this);
    testBar = new QSoundMeterBar;
    testBar->hide();
    devicesLayout->addWidget(testBar, 2, 0, 1, 3);
    testLabel = new QLabel;
    testLabel->setWordWrap(true);
    devicesLayout->addWidget(testLabel, 3, 0, 1, 2);
    testButton = new QPushButton(tr("Test"));
    connect(testButton, SIGNAL(clicked()), this, SLOT(startInput()));
    devicesLayout->addWidget(testButton, 3, 2);

    // NOTIFICATIONS
    group = new QGroupBox(tr("Sound notifications"));
    QVBoxLayout *notificationsLayout = new QVBoxLayout;
    group->setLayout(notificationsLayout);
    layout->addWidget(group);

    incomingMessageSound = new QCheckBox(tr("Incoming message"));
    incomingMessageSound->setChecked(!wApp->incomingMessageSound().isEmpty());
    notificationsLayout->addWidget(incomingMessageSound);

    outgoingMessageSound = new QCheckBox(tr("Outgoing message"));
    outgoingMessageSound->setChecked(!wApp->outgoingMessageSound().isEmpty());
    notificationsLayout->addWidget(outgoingMessageSound);

    setLayout(layout);
    setWindowIcon(QIcon(":/audio-output.png"));
    setWindowTitle(tr("Sound"));
}

bool SoundOptions::save()
{
    // devices
    if (!inputDevices.isEmpty())
        wApp->setAudioInputDevice(inputDevices[inputCombo->currentIndex()]);
    if (!outputDevices.isEmpty())
        wApp->setAudioOutputDevice(outputDevices[outputCombo->currentIndex()]);

    // notifications
    wApp->setIncomingMessageSound(
        incomingMessageSound->isChecked() ? QLatin1String(":/message-incoming.ogg") : QString());
    wApp->setOutgoingMessageSound(
        outgoingMessageSound->isChecked() ? QLatin1String(":/message-outgoing.ogg") : QString());
    return true;
}

void SoundOptions::startInput()
{
    if (inputDevices.isEmpty() || outputDevices.isEmpty())
        return;
    testButton->setEnabled(false);

    QAudioFormat format;
    format.setFrequency(8000);
    format.setChannels(1);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    // create audio input and output
#ifdef Q_OS_MAC
    // 128ms at 8kHz
    const int bufferSize = 2048 * format.channels();
#else
    // 160ms at 8kHz
    const int bufferSize = 2560 * format.channels();
#endif
    testInput = new QAudioInput(inputDevices[inputCombo->currentIndex()], format, this);
    testInput->setBufferSize(bufferSize);
    testOutput = new QAudioOutput(outputDevices[outputCombo->currentIndex()], format, this);
    testOutput->setBufferSize(bufferSize);

    // start input
    testBar->show();
    testLabel->setText(tr("Speak into the microphone for %1 seconds and check the sound level.").arg(QString::number(SOUND_TEST_SECONDS)));
    testBuffer->open(QIODevice::WriteOnly);
    testBuffer->reset();
    QSoundMeter *inputMeter = new QSoundMeter(testInput->format(), testBuffer, testInput);
    connect(inputMeter, SIGNAL(valueChanged(int)), testBar, SLOT(setValue(int)));
    testInput->start(inputMeter);
    QTimer::singleShot(SOUND_TEST_SECONDS * 1000, this, SLOT(startOutput()));
}

void SoundOptions::startOutput()
{
    testInput->stop();
    testBar->setValue(0);

    // start output
    testLabel->setText(tr("You should now hear the sound you recorded."));
    testBuffer->open(QIODevice::ReadOnly);
    testBuffer->reset();
    QSoundMeter *outputMeter = new QSoundMeter(testOutput->format(), testBuffer, testOutput);
    connect(outputMeter, SIGNAL(valueChanged(int)), testBar, SLOT(setValue(int)));
    testOutput->start(outputMeter);
    QTimer::singleShot(SOUND_TEST_SECONDS * 1000, this, SLOT(stopOutput()));
}

void SoundOptions::stopOutput()
{
    testOutput->stop();
    testBar->setValue(0);
    testBar->hide();

    // cleanup
    delete testInput;
    delete testOutput;
    testLabel->setText(QString());
    testButton->setEnabled(true);
}

