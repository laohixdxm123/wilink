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
import wiLink 1.2

Panel {
    id: panel

    PanelHeader {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        icon: 'diagnostics.png'
        title: '<b>' + qsTr('Diagnostics') + '</b>'
        z: 1

        Row {
            id: toolBar

            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right

            ToolButton {
                icon: 'refresh.png'
                enabled: !window.client.diagnosticManager.running
                text: qsTr('Refresh')

                onClicked: window.client.diagnosticManager.refresh();
            }

            ToolButton {
                icon: 'close.png'
                text: qsTr('Close')
                onClicked: panel.close()
            }
        }
    }

    Flickable {
        id: flickable

        anchors.left: parent.left
        anchors.right: scrollBar.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        contentHeight: diagnostic.height

        Text {
            id: diagnostic

            text: window.client.diagnosticManager.html
        }
    }

    ScrollBar {
        id: scrollBar
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        flickableItem: flickable
    }
}

