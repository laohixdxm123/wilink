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

import QtQuick 1.1
import wiLink 2.4
import 'scripts/utils.js' as Utils

Panel {
    id: panel

    property alias iconSource: vcard.avatar
    property alias jid: conversation.jid
    property alias title: vcard.name
    property alias presenceStatus: vcard.status
    property string subTitle

    subTitle: {
        var domain = Utils.jidToDomain(vcard.jid);
        if (domain == 'wifirst.net') {
            // for wifirst accounts, return the nickname if it is
            // different from the display name
            if (vcard.name != vcard.nickName)
                return vcard.nickName;
            else
                return '';
        } else {
            return vcard.jid;
        }
    }

    SoundLoader {
        id: incomingMessageSound
        source: appSettings.incomingMessageSound ? 'sounds/message-incoming.ogg' : ''
    }

    SoundLoader {
        id: outgoingMessageSound
        source: appSettings.outgoingMessageSound ? 'sounds/message-outgoing.ogg' : ''
    }

    Conversation {
        id: conversation

        onJidChanged: {
            conversation.client = accountModel.clientForJid(jid);
        }

        onRemoteStateChanged: {
            switch (remoteState) {
                case QXmppMessage.Composing:
                    historyView.composing('composing');
                    break;
                case QXmppMessage.Paused:
                    historyView.composing('paused');
                    break;
                default:
                    historyView.composing('gone');
            }
        }
    }

    Column {
        id: widgetBar
        objectName: 'widgetBar'

        anchors.top: parent.top
        anchors.topMargin: appStyle.margin.large
        anchors.left: parent.left
        anchors.right: parent.right
        z: 1

        /** Call widget.
         */
        Connections {
            ignoreUnknownSignals: true
            target: Qt.isQtObject(conversation.client) ? conversation.client.callManager : null

            onCallStarted: {
                if (Utils.jidToBareJid(call.jid) == conversation.jid) {
                    var component = Qt.createComponent('CallWidget.qml');

                    function finishCreation() {
                        if (component.status != Component.Ready)
                            return;

                        var widget = component.createObject(widgetBar);
                        widget.call = call;
                    }

                    if (component.status == Component.Loading)
                        component.statusChanged.connect(finishCreation);
                    else
                        finishCreation();
                }
            }
        }

        /** File transfer widget.
         */
        Connections {
            ignoreUnknownSignals: true
            target: Qt.isQtObject(conversation.client) ? conversation.client.transferManager : null

            onJobStarted: {
                if (Utils.jidToBareJid(job.jid) == conversation.jid) {
                    var component = Qt.createComponent('TransferWidget.qml');

                    function finishCreation() {
                        if (component.status != Component.Ready)
                            return;

                        var widget = component.createObject(widgetBar);
                        widget.job = job;
                    }

                    if (component.status == Component.Loading)
                        component.statusChanged.connect(finishCreation);
                    else
                        finishCreation();
                }
            }
        }
    }

    HistoryView {
        id: historyView

        property real footerOpacity: 0
        property int footerHeight: 0
        property string footerIcon: 'icon-comment-alt'

        anchors.top: widgetBar.bottom
        anchors.bottom: chatInput.top
        anchors.left: parent.left
        anchors.right: parent.right
        model: conversation.historyModel

        signal composing(string remoteState)

        onParticipantClicked: chatInput.talkAt(participant)

        onComposing: {
            state = remoteState;
            view.positionViewAtEnd();
        }

        state: 'gone'
        states: [
            State { name: 'composing'
                    PropertyChanges { target: historyView; footerOpacity: 1; footerHeight: 48; footerIcon:'icon-comment-alt' }
            },
            State { name: 'paused'
                    PropertyChanges { target: historyView; footerOpacity: 0.7; footerHeight: 48; footerIcon:'icon-cloud' }
            },
            State { name: 'gone'
                    PropertyChanges { target: historyView; footerOpacity: 0; footerHeight: 0; footerIcon:'icon-comment-alt' }
            }
        ]

        historyFooter: Rectangle {
                id: footer

                width: parent.width - 16
                anchors.horizontalCenter: parent.horizontalCenter
                opacity: footerOpacity
                height: footerHeight

                Image {
                    id: avatar

                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    asynchronous: true
                    source: vcard.avatar
                    sourceSize.height: appStyle.icon.normalSize
                    sourceSize.width: appStyle.icon.normalSize
                    height: appStyle.icon.normalSize
                    width: appStyle.icon.normalSize

                    Icon {
                        style: footerIcon
                        size: 20
                        color: '#333'
                        opacity: 0.5
                        anchors.top: parent.top
                        anchors.topMargin: -5
                        anchors.left: parent.right
                        anchors.leftMargin: 10
                    }
                }

            }

        Connections {
            target: historyView.model
            onMessageReceived: {
                if (contacts.currentJid != jid) {
                    // show notification
                    if (appSettings.incomingMessageNotification) {
                        var handle = appNotifier.showMessage(vcard.name, text, qsTranslate('ConversationPanel', 'Show this conversation'));
                        if (handle) {
                            handle.clicked.connect(function() {
                                window.showAndRaise();
                                showConversation(jid);
                            });
                        }
                    }

                    // notify alert
                    appNotifier.alert();

                    // play a sound
                    incomingMessageSound.start();

                    // add pending message
                    rosterModel.addPendingMessage(jid);
                }
            }
        }

        DropArea {
            anchors.fill: parent
            enabled: vcard.features & VCard.FileTransferFeature
            visible: enabled

            onFilesDropped: {
                for (var i in files) {
                    var fullJid = vcard.jidForFeature(VCard.FileTransferFeature);
                    conversation.client.transferManager.sendFile(fullJid, files[i]);
                }
            }
        }

        // Button to fetch older messages.
        Item {
            id: fetchPrevious

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: appStyle.margin.normal
            height: appStyle.icon.smallSize + 2*appStyle.margin.normal
            width: appStyle.icon.smallSize + 2*appStyle.margin.normal + appStyle.spacing.horizontal + fetchLabel.width
            opacity: 0

            Rectangle {
                anchors.fill: parent
                opacity: 0.8
                radius: appStyle.margin.large
                smooth: true
                gradient: Gradient {
                    GradientStop { position: 0; color: '#9bbdf4' }
                    GradientStop { position: 1; color: '#90acd8' }
                }
            }

            Icon {
                id: fetchIcon

                anchors.left: parent.left
                anchors.leftMargin: appStyle.margin.normal
                anchors.verticalCenter: parent.verticalCenter
                style: 'icon-info-sign'
            }

            Label {
                id: fetchLabel

                anchors.left: fetchIcon.right
                anchors.leftMargin: appStyle.spacing.horizontal
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr('Fetch older messages')
            }

            MouseArea {
                id: fetchArea

                anchors.fill: parent
                hoverEnabled: true

                onClicked: {
                    if (fetchPrevious.state == 'active') {
                        conversation.historyModel.fetchPreviousPage();
                    }
                }
            }

            states: State {
                name: 'active'
                when: conversation.historyModel.hasPreviousPage && historyView.atYBeginning
                PropertyChanges { target: fetchPrevious; opacity: 1 }
            }

            transitions: [
                Transition {
                    to: 'active'
                    SequentialAnimation {
                        PauseAnimation { duration: 500 }
                        NumberAnimation { properties: 'opacity'; duration: appStyle.animation.normalDuration }
                    }
                },
                Transition {
                    from: 'active'
                    NumberAnimation { properties: 'opacity'; duration: appStyle.animation.normalDuration }
                }
            ]
        }
    }

    ChatEdit {
        id: chatInput

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        menuComponent: Menu {
            id: menu

            onItemClicked: {
                var item = menu.model.get(index);
                if (item.action == 'call') {
                    var fullJid = vcard.jidForFeature(VCard.VoiceFeature);
                    conversation.client.callManager.call(fullJid);
                } else if (item.action == 'send') {
                    var dialog = appNotifier.fileDialog();
                    dialog.windowTitle = qsTr('Send a file');
                    dialog.fileMode = QFileDialog.ExistingFile;
                    if (dialog.exec()) {
                        for (var i in dialog.selectedFiles) {
                            var filePath = dialog.selectedFiles[i];
                            var fullJid = vcard.jidForFeature(VCard.FileTransferFeature);
                            conversation.client.transferManager.sendFile(fullJid, filePath);
                        }
                    }
                } else if (item.action == 'clear') {
                    conversation.historyModel.clear();
                }
            }

            Component.onCompleted: {
                if (vcard.features & VCard.VoiceFeature) {
                    menu.model.append({
                        action: 'call',
                        iconStyle: 'icon-phone',
                        text: qsTr('Call')});
                }
                if (vcard.features & VCard.FileTransferFeature) {
                    menu.model.append({
                        action: 'send',
                        iconStyle: 'icon-upload',
                        text: qsTr('Send')});
                }
                menu.model.append({
                    action: 'clear',
                    iconStyle: 'icon-remove',
                    text: qsTr('Clear')});
            }
        }

        onChatStateChanged: {
            conversation.localState = chatInput.chatState;
        }

        onReturnPressed: {
            if (Qt.isQtObject(conversation)) {
                var text = chatInput.text;
                if (conversation.sendMessage(text)) {
                    chatInput.text = '';
                    outgoingMessageSound.start();
                }
            }
        }
    }

    resources: [
        VCard {
            id: vcard
            jid: conversation.jid
        }
    ]

    Keys.forwardTo: historyView
}
