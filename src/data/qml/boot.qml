/*
 * wiLink
 * Copyright (C) 2009-2015 Wifirst
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

import QtQuick 2.3
import wiLink 2.4

FocusScope {
    focus: true

    TranslationLoader {
        Component.onCompleted: {
            if (localeName == 'fr')
                source = 'i18n/' + localeName + '.qm';
            else
                uiLoader.source = 'Main.qml';
        }

        onStatusChanged: {
            if (status == TranslationLoader.Ready || status == TranslationLoader.Error) {
                uiLoader.source = 'Main.qml';
            }
        }
    }

    Loader {
        id: uiLoader

        anchors.fill: parent
        focus: true
    }
}
