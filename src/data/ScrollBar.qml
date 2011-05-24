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

Item {
    id: scrollBar

    property Flickable flickableItem
    property string moveAction: ''
    property int moveQuantity: 30
    property bool moveRepeat: false
    property real position: flickableItem.visibleArea.yPosition
    property real pageSize: flickableItem.visibleArea.heightRatio

    state: pageSize == 1 ? 'collapsed' : ''
    width: 11

    Rectangle {
        id: track

        anchors.top: scrollBar.top
        anchors.left: scrollBar.left
        anchors.topMargin: -1
        border.color: '#84bde8'
        border.width: 1
        gradient: Gradient {
            GradientStop {id: trackStop1; position: 0.0; color: '#bedfe7'}
            GradientStop {id: trackStop2; position: 0.5; color: '#ffffff'}
            GradientStop {id: trackStop3; position: 1.0; color: '#dfeff3'}
        }
        height: parent.width
        width: parent.height - 2 * ( scrollBar.width - 1 )
        transform: Rotation {
            angle: 90
            origin.x: 0
            origin.y: track.height
        }

        Rectangle {
            id: handle

            property int desiredHeight: Math.ceil(scrollBar.pageSize * (track.width - 2))

            border.color: track.border.color
            border.width: 1
            gradient: Gradient {
                GradientStop {id: handleStop1; position: 0.0; color: '#ffffff'}
                GradientStop {id: handleStop2; position: 0.5; color: '#7ac6d8'}
                GradientStop {id: handleStop3; position: 1.0; color: '#ffffff'}
            }
            radius: 10
            smooth: true

            height: parent.height
            width: Math.max(desiredHeight, 20)
            x: Math.floor(scrollBar.position * (track.width + desiredHeight - width - 2)) + 1
            y: 0

            states: State {
                name: 'pressed'
                PropertyChanges { target: handleStop2; color: '#57c7e7' }
            }
        }
    }

    Rectangle {
        id: buttonUp

        anchors.top: parent.top
        border.color: track.border.color
        color: '#bedfe7'
        height: parent.width - 1
        width: parent.width - 1

        Text {
            id: textButtonUp
            anchors.fill: parent
            color: '#0d88a4'
            font.pixelSize: scrollBar.width - 4
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: '<html>&#9650;</html>'
        }

        MouseArea {
            anchors.fill: parent

            onPressed: {
                buttonUp.state = 'pressed';
                moveAction = 'up';
                moveQuantity = -30;
                scrollBar.moveBy(moveQuantity);
            }

            onReleased: {
                buttonUp.state = '';
                moveAction = '';
                moveRepeat = false;
            }
        }

        states: State {
            name: 'pressed'
            PropertyChanges { target: buttonUp; color: '#ffffff' }
            PropertyChanges { target: textButtonUp; color: '#5fb0c3' }
        }
    }

    Rectangle {
        id: buttonDown

        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        border.color: track.border.color
        color: '#bedfe7'
        height: parent.width - 1
        width: parent.width - 1

        Text {
            id: textButtonDown
            anchors.fill: parent
            color: '#0d88a4'
            font.pixelSize: scrollBar.width - 4
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: '<html>&#9660;</html>'
        }

        MouseArea {
            anchors.fill: parent

            onPressed: {
                buttonDown.state = 'pressed';
                moveAction = 'down';
                moveQuantity = 30;
                scrollBar.moveBy(moveQuantity);
            }

            onReleased: {
                buttonDown.state = '';
                moveAction = '';
                moveRepeat = false;
            }
        }
        states: State {
            name: 'pressed'
            PropertyChanges { target: buttonDown; color: '#ffffff' }
            PropertyChanges { target: textButtonDown; color: '#5fb0c3' }
        }
    }

    MouseArea {
        id: clickableArea

        property real pressContentY
        property real pressMouseY
        property real pressPageSize

        anchors.top: buttonUp.bottom
        anchors.bottom: buttonDown.top
        anchors.left: parent.left
        anchors.right: parent.right
        drag.axis: Drag.YAxis

        onPressed: {
            if (mouse.y < handle.x) {
                moveAction = 'up';
                moveQuantity = -flickableItem.height;
                scrollBar.moveBy(moveQuantity);
            } else if (mouse.y > handle.x + handle.width) {
                moveAction = 'down';
                moveQuantity = flickableItem.height;
                scrollBar.moveBy(moveQuantity);
            } else {
                handle.state = 'pressed';
                moveAction = 'drag';
                moveQuantity = 0;
                pressContentY = flickableItem.contentY;
                pressMouseY = mouse.y;
                pressPageSize = scrollBar.pageSize;
            }
        }

        onReleased: {
            handle.state = '';
            moveAction = '';
            moveRepeat = false;
        }

        onPositionChanged: {
            if (moveAction == 'drag') {
                scrollBar.moveBy((pressContentY - flickableItem.contentY) + (mouse.y - pressMouseY) / pressPageSize);
            }
        }
    }

    Timer {
        id: delayTimer

        interval: 350
        running: moveAction == 'up' || moveAction == 'down'

        onTriggered: moveRepeat = true
    }

    Timer {
        id: repeatTimer

        interval: 60
        repeat: true
        running: moveRepeat

        onTriggered: scrollBar.moveBy(moveQuantity)
    }

    function moveBy(delta) {
        // do not exceed bottom
        delta = Math.min((1 - position - pageSize) * flickableItem.contentHeight, delta);

        // do not exceed top
        delta = Math.max(-position * flickableItem.contentHeight, delta);

        // move
        flickableItem.contentY = Math.round(flickableItem.contentY + delta);
    }

    states: State {
        name: 'collapsed'
        PropertyChanges { target: scrollBar; width: 0; opacity: 0 }
    }
}
