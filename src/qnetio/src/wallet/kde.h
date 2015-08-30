/*
 * QNetIO
 * Copyright (C) 2008-2012 Bolloré telecom
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

#ifndef __QNETIO_WALLET_GNOME_H__
#define __QNETIO_WALLET_GNOME_H__

#include "wallet.h"
namespace KWallet {
    class Wallet;
}

/** Support for KWallet
 */
class KdeWallet : public QNetIO::Wallet
{
    Q_OBJECT

public:
    KdeWallet(QObject *parent=NULL);

    bool deleteCredentials(const QString &realm, const QString &user);
    bool getCredentials(const QString &realm, QString &user, QString &password);
    bool getRealms(QStringList &realms);
    bool setCredentials(const QString &realm, const QString &user, const QString &password);

private:
    KWallet::Wallet *currentWallet;
};

class KdeWalletPlugin : public QNetIO::WalletPlugin
{
    Q_OBJECT

public:
    QNetIO::Wallet *create(const QString &key);
    QStringList keys() const;
    int priority(const QString &key) const;
};

#endif
