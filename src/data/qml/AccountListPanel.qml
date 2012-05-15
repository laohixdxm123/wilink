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
import wiLink 2.0
import 'utils.js' as Utils

Item {
    id: panel

    property alias model: accountView.model

    signal addClicked

    PanelHelp {
        id: help

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: qsTr('In addition to your %1 account, %2 can connect to additional chat accounts such as Google Talk and Facebook.').replace('%1', application.organizationName).replace('%2', application.applicationName)
    }

    ScrollView {
        id: accountView

        anchors.top: help.bottom
        anchors.topMargin: appStyle.spacing.vertical
        anchors.bottom: footer.top
        anchors.bottomMargin: appStyle.spacing.vertical
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        model: AccountModel {}
        delegate: Item {
            height: appStyle.icon.smallSize + 4
            width: parent.width

            Item {
                anchors.fill: parent
                anchors.margins: 2

                Image {
                    id: image

                    anchors.top: parent.top
                    anchors.left: parent.left
                    source: model.type == 'web' ? 'image://icon/web' : 'image://icon/chat'
                    sourceSize.height: appStyle.icon.smallSize
                    sourceSize.width: appStyle.icon.smallSize
                }

                Label {
                    anchors.left: image.right
                    anchors.leftMargin: appStyle.spacing.horizontal
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: model.username
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        accountView.currentIndex = model.index;
                    }
                }
            }
        }
    }

    Row {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: appStyle.icon.smallSize + 8

        Button {
            iconSource: 'image://icon/add'
            onClicked: panel.addClicked()
        }

        Button {
            iconSource: 'image://icon/remove'
            onClicked: {
                if (accountView.currentIndex >= 0) {
                    accountView.model.remove(accountView.currentIndex);
                }
            }
        }
    }
}
