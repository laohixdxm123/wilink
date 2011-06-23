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

import QtQuick 1.0
import QXmpp 0.4
import wiLink 1.2

Dialog {
    id: dialog

    property alias room: configurationModel.room

    title: qsTr('Chat room configuration')

    RoomConfigurationModel {
        id: configurationModel
    }

    Item {
        anchors.fill: contents

        ListView {
            id: view

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.bottomMargin: appStyle.spacing.vertical
            anchors.left: parent.left
            anchors.right: scrollBar.left
            clip: true

            model: configurationModel
            delegate: Item {
                id: item

                width: parent.width - 1
                height: 24

                CheckBox {
                    id: label

                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    text: model.label
                }
            }
        }

        ScrollBar {
            id: scrollBar

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            flickableItem: view
        }
    }

    onAccepted: {
        permissionModel.save();
        dialogLoader.hide();
    }
}
