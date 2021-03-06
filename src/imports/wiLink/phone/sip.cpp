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

#include <QByteArrayMatcher>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QPair>
#include <QUdpSocket>
#include <QThread>
#include <QTimer>

#include "QXmppRtpChannel.h"
#include "QXmppStun.h"
#include "QXmppUtils.h"

#include "sip_p.h"

static const int RTP_COMPONENT = 1;
static const int RTCP_COMPONENT = 2;

Q_DECLARE_METATYPE(SipCall::State)

#define SIP_DEBUG_SIP
#define SIP_DEBUG_STUN
#define SIP_USE_ICE

#define EXPIRE_SECONDS 120

#define STUN_RETRY_MS   500
#define STUN_EXPIRE_MS  30000

#define SIP_T1_TIMER 500
#define SIP_T2_TIMER 4000

enum StunStep {
    StunConnectivity = 0,
    StunChangeServer,
};

QMap<QByteArray, QByteArray> parseDigestMessage(const QByteArray &ba)
{
    QMap<QByteArray, QByteArray> map;
    int startIndex = 0;
    int pos = 0;
    while ((pos = ba.indexOf("=", startIndex)) >= 0)
    {
        // key get name and skip equals
        const QByteArray key = ba.mid(startIndex, pos - startIndex).trimmed();
        pos++;

        // check whether string is quoted
        if (ba.at(pos) == '"')
        {
            // skip opening quote
            pos++;
            int endPos = ba.indexOf('"', pos);
            // skip quoted quotes
            while (endPos >= 0 && ba.at(endPos - 1) == '\\')
                endPos = ba.indexOf('"', endPos + 1);
            if (endPos < 0)
            {
                qWarning("Unfinished quoted string");
                return map;
            }
            // unquote
            QByteArray value = ba.mid(pos, endPos - pos);
            value.replace("\\\"", "\"");
            value.replace("\\\\", "\\");
            map[key] = value;
            // skip closing quote and comma
            startIndex = endPos + 2;
        } else {
            // non-quoted string
            int endPos = ba.indexOf(',', pos);
            if (endPos < 0)
                endPos = ba.size();
            map[key] = ba.mid(pos, endPos - pos);
            // skip comma
            startIndex = endPos + 1;
        }
    }
    return map;
}

QByteArray serializeDigestMessage(const QMap<QByteArray, QByteArray> &map)
{
    QByteArray ba;
    foreach (const QByteArray &key, map.keys()) {
        if (!ba.isEmpty())
            ba.append(',');
        ba.append(key + "=");
        QByteArray value = map[key];
        value.replace("\\", "\\\\");
        value.replace("\"", "\\\"");
        ba.append("\"" + value + "\"");
    }
    return ba;
}

static QString sipAddressToUri(const QString &address)
{
    QRegExp rx("(.*)<(sip:([^>]+))>(;.+)?");
    if (!rx.exactMatch(address)) {
        qWarning("Bad address %s", qPrintable(address));
        return QString();
    }
    return rx.cap(2);
}

SipCallContext::SipCallContext()
    : cseq(1)
{
    id = QXmppUtils::generateStanzaHash().toLatin1();
    tag = QXmppUtils::generateStanzaHash(8).toLatin1();
}

bool SipCallContext::handleAuthentication(const SipMessage &reply)
{
    bool isProxy = reply.statusCode() == 407;
    QMap<QByteArray, QByteArray> *lastChallenge = isProxy ? &proxyChallenge : &challenge;

    const QByteArray auth = reply.headerField(isProxy ? "Proxy-Authenticate" : "WWW-Authenticate");
    int spacePos = auth.indexOf(' ');
    if (spacePos < 0 || auth.left(spacePos) != "Digest") {
        qWarning("Unsupported authentication method");
        return false;
    }


    QMap<QByteArray, QByteArray> challenge = parseDigestMessage(auth.mid(spacePos + 1));
    if (lastChallenge->value("realm") == challenge.value("realm") &&
        lastChallenge->value("nonce") == challenge.value("nonce")) {
        qWarning("Authentication failed");
        return false;
    }
    *lastChallenge = challenge;
    return true;
}

SipCallPrivate::SipCallPrivate(SipCall *qq)
    : state(SipCall::ConnectingState)
    , activeTime("0 0")
    , invitePending(false)
    , inviteQueued(false)
    , q(qq)
{
}

void SipCallPrivate::handleReply(const SipMessage &reply)
{
    // store information
    if (!reply.headerField("To").isEmpty())
        remoteRecipient = reply.headerField("To");
    if (!reply.headerField("Contact").isEmpty()) {
        const QString contact = QString::fromUtf8(reply.headerField("Contact"));
        remoteUri = sipAddressToUri(contact).toUtf8();
    }
    const QList<QByteArray> recordRoutes = reply.headerFieldValues("Record-Route");
    if (!recordRoutes.isEmpty())
        remoteRoute = recordRoutes;

    // find transaction
    const QMap<QByteArray, QByteArray> viaParams = SipMessage::valueParameters(reply.headerField("Via"));
    foreach (SipTransaction *transaction, transactions) {
        if (transaction->branch() == viaParams.value("branch")) {
            transaction->messageReceived(reply);
            return;
        }
    }

    // if the command was not an INVITE, stop here
    const QByteArray command = reply.headerField("CSeq").split(' ').last();
    if (command != "INVITE")
        return;

    // send ack for final responses
    if  (reply.statusCode() >= 200) {
        invitePending = false;

        SipMessage request = client->d->buildRequest("ACK", remoteUri, this, inviteRequest.sequenceNumber());
        for (int i = remoteRoute.size() - 1; i >= 0; --i)
            request.addHeaderField("Route", remoteRoute[i]);
        request.setHeaderField("To", remoteRecipient);
        request.setHeaderField("Via", inviteRequest.headerField("Via"));
        request.removeHeaderField("Contact");
        client->sendMessage(request);
    }

    // handle authentication
    if (reply.statusCode() == 407) {
        if (handleAuthentication(reply)) {
            SipMessage request = client->d->buildRetry(inviteRequest, this);
            client->sendMessage(request);
            invitePending = true;
            inviteRequest = request;
            return;
        }
    }

    // handle invite status
    if (reply.statusCode() == 180)
    {
        emit q->ringing();
    }
    else if (reply.statusCode() == 200)
    {
        timeoutTimer->stop();
        if (reply.headerField("Content-Type") == "application/sdp" &&
            handleSdp(QString::fromUtf8(reply.body())))
        {
            q->debug(QString("SIP call %1 established").arg(QString::fromUtf8(id)));
            setState(SipCall::ActiveState);
        } else {
            q->warning(QString("SIP call %1 does not have a valid SDP descriptor").arg(QString::fromUtf8(id)));
            errorString = QLatin1String("Invalid SDP descriptor");
            q->hangup();
        }
    }
    else if (reply.statusCode() >= 300)
    {
        q->warning(QString("SIP call %1 failed").arg(
            QString::fromUtf8(id)));
        timeoutTimer->stop();
        errorString = QString("%1: %2").arg(QString::number(reply.statusCode()), reply.reasonPhrase());
        setState(SipCall::FinishedState);
    }
}

void SipCallPrivate::handleRequest(const SipMessage &request)
{
    // store information
    if (!request.headerField("From").isEmpty())
        remoteRecipient = request.headerField("From");
    if (!request.headerField("Contact").isEmpty()) {
        const QString contact = QString::fromUtf8(request.headerField("Contact"));
        remoteUri = sipAddressToUri(contact).toUtf8();
    }
    const QList<QByteArray> recordRoutes = request.headerFieldValues("Record-Route");
    if (!recordRoutes.isEmpty())
        remoteRoute = recordRoutes;

    // respond
    SipMessage response = client->d->buildResponse(request);
    if (request.method() == "ACK") {
        if (audioChannel->isOpen())
            setState(SipCall::ActiveState);
        else
            setState(SipCall::FinishedState);
     } else if (request.method() == "BYE") {
        response.setStatusCode(200);
        response.setReasonPhrase("OK");
        client->sendMessage(response);
        setState(SipCall::FinishedState);
    } else if (request.method() == "CANCEL") {
        response.setStatusCode(200);
        response.setReasonPhrase("OK");
        client->sendMessage(response);
        setState(SipCall::FinishedState);
    } else if (request.method() == "INVITE") {

        if (request.headerField("Content-Type") == "application/sdp" &&
            handleSdp(QString::fromUtf8(request.body())))
        {
            inviteRequest = request;
            response.setStatusCode(180);
            response.setReasonPhrase("Ringing");
        } else {
            response.setStatusCode(400);
            response.setReasonPhrase("Bad request");
        }
        client->sendMessage(response);
    } else {
        response.setStatusCode(405);
        response.setReasonPhrase("Method not allowed");
        client->sendMessage(response);
    }
}

static QString addressToSdp(const QHostAddress &host)
{
    return QString("IN %1 %2").arg(
        host.protocol() == QAbstractSocket::IPv6Protocol ? "IP6" : "IP4",
        host.toString());
}

QString SipCallPrivate::buildSdp() const
{
    QXmppJingleIq::Content content;

    // media
    content.setDescriptionMedia("audio");
    content.setDescriptionSsrc(audioChannel->localSsrc());
    content.setPayloadTypes(audioChannel->localPayloadTypes());

    // transport
    content.setTransportCandidates(iceConnection->localCandidates());
    content.setTransportUser(iceConnection->localUser());
    content.setTransportPassword(iceConnection->localPassword());

    const quint32 ntpSeconds = QDateTime(QDate(1900, 1, 1)).secsTo(QDateTime::currentDateTime());
    QString sdpStr;
    sdpStr += "v=0\r\n";
    sdpStr += QString("o=- %1 %2 %3\r\n").arg(
        QString::number(ntpSeconds),
        QString::number(ntpSeconds),
        addressToSdp(client->d->localAddress));
    sdpStr += "s=-\r\n";
    sdpStr += QString("t=%1\r\n").arg(activeTime);
    sdpStr += content.toSdp();

    return sdpStr;
}

bool SipCallPrivate::handleSdp(const QString &sdpStr)
{
    QXmppJingleIq::Content content;
    if (!content.parseSdp(sdpStr))
        return false;

    // parse descriptor
    quint16 remoteRtpPort = 0;
    QHostAddress remoteRtpAddress;
    QString line;
    foreach (line, sdpStr.split('\n')) {
        if (line.endsWith('\r'))
            line.resize(line.size() - 1);
        if (line.startsWith("c=")) {
            // determine remote host
            const QString val = line.mid(2);
            if (val.startsWith("IN IP4 ") || val.startsWith("IN IP6 ")) {
                remoteRtpAddress = QHostAddress(val.mid(7));
            }
        } else if (line.startsWith("m=")) {
            QStringList bits = line.mid(2).split(' ');
            if (bits.size() < 3 || bits[0] != "audio" || bits[2] != "RTP/AVP")
                continue;

            // determine remote port
            remoteRtpPort = bits[1].toUInt();
        } else if (line.startsWith("t=")) {
            // active time
            const QString val = line.mid(2);
            if (direction == SipCall::IncomingDirection)
                activeTime = val;
            else if (val != activeTime)
                q->warning(QString("Answerer replied with a different active time %1").arg(val));
        }
    }

    iceConnection->setRemoteUser(content.transportUser());
    iceConnection->setRemotePassword(content.transportPassword());
    foreach (const QXmppJingleCandidate &candidate, content.transportCandidates())
        iceConnection->addRemoteCandidate(candidate);

    // add RTP remote candidate
    QXmppJingleCandidate remoteCandidate;
    remoteCandidate.setComponent(RTP_COMPONENT);
    remoteCandidate.setProtocol("udp");
    remoteCandidate.setType(QXmppJingleCandidate::HostType);
    remoteCandidate.setHost(remoteRtpAddress);
    remoteCandidate.setPort(remoteRtpPort);
    iceConnection->addRemoteCandidate(remoteCandidate);

    // add RTCP remote candidate
    remoteCandidate.setComponent(RTCP_COMPONENT);
    remoteCandidate.setPort(remoteRtpPort + 1);
    iceConnection->addRemoteCandidate(remoteCandidate);

    // assign remote payload types
    audioChannel->setRemotePayloadTypes(content.payloadTypes());
    if (!audioChannel->isOpen()) {
        q->warning("Could not assign codec to RTP channel");
        return false;
    }

    return true;
}

void SipCallPrivate::sendInvite()
{
    const QString sdp = buildSdp();

    SipMessage request = client->d->buildRequest("INVITE", remoteUri, this, cseq++);
    request.setHeaderField("To", remoteRecipient);
    request.setHeaderField("Content-Type", "application/sdp");
    request.setBody(sdp.toUtf8());
    client->sendMessage(request);
    invitePending = true;
    inviteRequest = request;

    timeoutTimer->start(64 * SIP_T1_TIMER);
}

void SipCallPrivate::setState(SipCall::State newState)
{
    if (state != newState)
    {
        state = newState;
        emit q->stateChanged(state);

        if (state == SipCall::ActiveState) {
            startStamp = QDateTime::currentDateTime();
            durationTimer->start();
            emit q->connected();
        } else if (state == SipCall::FinishedState) {
            q->debug(QString("SIP call %1 finished").arg(QString::fromUtf8(id)));
            finishStamp = QDateTime::currentDateTime();
            durationTimer->stop();
            emit q->durationChanged();
            emit q->finished();
        }
    }
}

SipCall::SipCall(const QString &recipient, SipCall::Direction direction, SipClient *parent)
    : QXmppLoggable(parent)
{
    bool check;
    Q_UNUSED(check);

    qRegisterMetaType<SipCall::State>();

    d = new SipCallPrivate(this);
    d->client = parent;
    d->direction = direction;
    d->inviteQueued = (direction == SipCall::OutgoingDirection);
    d->remoteRecipient = recipient.toUtf8();
    d->remoteUri = sipAddressToUri(recipient).toUtf8();

    d->audioChannel = new QXmppRtpAudioChannel(this);

    // bind sockets
    d->iceConnection = new QXmppIceConnection(this);
    d->iceConnection->addComponent(RTP_COMPONENT);
    d->iceConnection->addComponent(RTCP_COMPONENT);
    d->iceConnection->setIceControlling(d->direction == SipCall::OutgoingDirection);
    d->iceConnection->setStunServer(d->client->d->stunServerAddress,
                                    d->client->d->stunServerPort);
    check = connect(d->iceConnection, SIGNAL(gatheringStateChanged()),
                    this, SLOT(gatheringStateChanged()));
    Q_ASSERT(check);

    // setup RTP transport
    QXmppIceComponent *rtpComponent = d->iceConnection->component(RTP_COMPONENT);
    check = connect(rtpComponent, SIGNAL(datagramReceived(QByteArray)),
                    d->audioChannel, SLOT(datagramReceived(QByteArray)));
    Q_ASSERT(check);

    check = connect(d->audioChannel, SIGNAL(sendDatagram(QByteArray)),
                    rtpComponent, SLOT(sendDatagram(QByteArray)));
    Q_ASSERT(check);

    // Timer B
    d->timeoutTimer = new QTimer(this);
    d->timeoutTimer->setSingleShot(true);
    check = connect(d->timeoutTimer, SIGNAL(timeout()),
                    this, SLOT(handleTimeout()));
    Q_ASSERT(check);

    // Duration timer
    d->durationTimer = new QTimer(this);
    d->durationTimer->setInterval(1000);
    check = connect(d->durationTimer, SIGNAL(timeout()),
                    this, SIGNAL(durationChanged()));

    // start ICE
    if (!d->iceConnection->bind(QList<QHostAddress>() << d->client->d->localAddress))
        warning("Could not start listening for RTP");
}

SipCall::~SipCall()
{
    delete d;
}

/// Call this method if you wish to accept an incoming call.
///

void SipCall::accept()
{
    if (d->direction == SipCall::IncomingDirection && d->state == SipCall::ConnectingState) {
#if 0
        QByteArray rtcp;
        QDataStream stream(&rtcp, QIODevice::WriteOnly);

        // receiver report
        stream << quint8(0x80);
        stream << quint8(0xc9); // receiver report
        stream << quint16(1);   // length
        stream << d->audioChannel->synchronizationSource();

        // source description
        stream << quint8(0x81);
        stream << quint8(0xca); // source description
        stream << quint16(2);   // length
        stream << d->audioChannel->synchronizationSource();
        stream << quint8(1); // cname
        stream << quint8(0);
        stream << quint8(0); // end
        stream << quint8(0);
        d->rtcpSocket->writeDatagram(rtcp, d->remoteHost, d->remotePort+1);
#endif

        const QString sdp = d->buildSdp();

        SipMessage response = d->client->d->buildResponse(d->inviteRequest);
        response.setStatusCode(200);
        response.setReasonPhrase("OK");
        response.setHeaderField("Allow", "INVITE, ACK, CANCEL, OPTIONS, BYE");
        response.setHeaderField("Supported", "replaces");
        response.setHeaderField("Content-Type", "application/sdp");
        response.setBody(sdp.toUtf8());
        d->client->sendMessage(response);

        // notify user
        d->client->callStarted(this);
    }
}

QXmppRtpAudioChannel *SipCall::audioChannel() const
{
    return d->audioChannel;
}

QXmppIceConnection *SipCall::audioConnection() const
{
    return d->iceConnection;
}

/// Returns the call's direction.
///

SipCall::Direction SipCall::direction() const
{
    return d->direction;
}

/// Returns the call's duration in seconds.
///

int SipCall::duration() const
{
    if (d->startStamp.isValid()) {
        if (d->finishStamp.isValid())
            return d->startStamp.secsTo(d->finishStamp);
        else
            return d->startStamp.secsTo(QDateTime::currentDateTime());
    }
    return 0;
}

/// Returns the call's error string.
///

QString SipCall::errorString() const
{
    return d->errorString;
}

QByteArray SipCall::id() const
{
    return d->id;
}

void SipCall::gatheringStateChanged()
{
    // send INVITE if required
    if (d->inviteQueued &&
        d->iceConnection->gatheringState() == QXmppIceConnection::CompleteGatheringState) {
        d->sendInvite();
        d->inviteQueued = false;
    }
}

QString SipCall::recipient() const
{
    return QString::fromUtf8(d->remoteRecipient);
}

SipCall::State SipCall::state() const
{
    return d->state;
}

void SipCall::destroyLater()
{
    deleteLater();
}

void SipCall::transactionFinished()
{
    SipTransaction *transaction = qobject_cast<SipTransaction*>(sender());
    if (!transaction || !d->transactions.removeAll(transaction))
        return;
    transaction->deleteLater();

    const QByteArray method = transaction->request().method();
    if (method == "BYE") {
        if (d->invitePending) {
            SipMessage request = d->client->d->buildRequest("CANCEL", d->inviteRequest.uri(), d, d->inviteRequest.sequenceNumber());
            request.setHeaderField("To", d->inviteRequest.headerField("To"));
            request.setHeaderField("Via", d->inviteRequest.headerField("Via"));
            request.removeHeaderField("Contact");
            d->transactions << new SipTransaction(request, d->client, this);
        } else {
            d->setState(SipCall::FinishedState);
        }
    }
    else if (method == "CANCEL") {
        d->setState(SipCall::FinishedState);
    }
}

void SipCall::handleTimeout()
{
    warning(QString("SIP call %1 timed out").arg(QString::fromUtf8(d->id)));
    d->errorString = QLatin1String("Outgoing call timed out");
    d->setState(SipCall::FinishedState);
}

/// Hangs up the call.
///

void SipCall::hangup()
{
    if (d->state == SipCall::DisconnectingState ||
        d->state == SipCall::FinishedState)
        return;

    debug(QString("SIP call %1 hangup").arg(
            QString::fromUtf8(d->id)));
    d->setState(SipCall::DisconnectingState);
    d->iceConnection->close();
    d->timeoutTimer->stop();

    SipMessage request = d->client->d->buildRequest("BYE", d->remoteUri, d, d->cseq++);
    request.setHeaderField("To", d->remoteRecipient);
    for (int i = d->remoteRoute.size() - 1; i >= 0; --i)
        request.addHeaderField("Route", d->remoteRoute[i]);
    d->transactions << new SipTransaction(request, d->client, this);
}

SipClientPrivate::SipClientPrivate(SipClient *qq)
    : logger(0)
    , state(SipClient::DisconnectedState)
    , stunCookie(0)
    , stunDone(false)
    , stunReflexivePort(0)
    , stunServerPort(0)
    , q(qq)
{
}

QByteArray SipClientPrivate::authorization(const SipMessage &request, const QMap<QByteArray, QByteArray> &input) const
{
    // determine realm
    const QByteArray realm = input.value("realm");

    // determine quality of protection
    const QList<QByteArray> qops = input.value("qop").split(',');
    QByteArray qop;
    if (qops.contains("auth"))
        qop = "auth";
    const QByteArray nonce = input.value("nonce");
    const QByteArray nc = "00000001";
    const QByteArray cnonce = QXmppUtils::generateRandomBytes(32).toBase64();
    const QByteArray A1 = username.toUtf8() + ':' + realm + ':' + password.toUtf8();
    const QByteArray A2 = request.method() + ':' + request.uri();

    QByteArray HA1 = QCryptographicHash::hash(A1, QCryptographicHash::Md5).toHex();
    QByteArray HA2 = QCryptographicHash::hash(A2, QCryptographicHash::Md5).toHex();
    QByteArray KD;
    if (qop == "auth")
        KD = HA1 + ':' + nonce + ':' + nc + ':' + cnonce + ':' + qop + ':' + HA2;
    else
        KD = HA1 + ':' + nonce + ':' + HA2;

    QMap<QByteArray, QByteArray> response;
    response["username"] = username.toUtf8();
    if (!realm.isEmpty())
        response["realm"] = realm;
    response["nonce"] = nonce;
    response["uri"] = request.uri();
    response["response"] = QCryptographicHash::hash(KD, QCryptographicHash::Md5).toHex();
    if (qop == "auth") {
        response["qop"] = qop;
        response["cnonce"] = cnonce;
        response["nc"] = nc;
    }
    response["algorithm"] = "MD5";
    if (input.contains("opaque"))
        response["opaque"] = input.value("opaque");

    return QByteArray("Digest ") + serializeDigestMessage(response);
}

SipMessage SipClientPrivate::buildRequest(const QByteArray &method, const QByteArray &uri, SipCallContext *ctx, int seqNum)
{
    QString addr;
    if (!displayName.isEmpty())
        addr += QString("\"%1\"").arg(displayName);
    addr += QString("<sip:%1@%2>").arg(username, domain);

    const QString branch = "z9hG4bK-" + QXmppUtils::generateStanzaHash();
    const QString host = QString("%1:%2").arg(
        localAddress.toString(),
        QString::number(socket->localPort()));
    const QString via = QString("SIP/2.0/UDP %1;branch=%2;rport").arg(
        host, branch);

    SipMessage packet;
    packet.setMethod(method);
    packet.setUri(uri);
    packet.setHeaderField("Via", via.toUtf8());
    packet.setHeaderField("Max-Forwards", "70");
    packet.setHeaderField("Call-ID", ctx->id);
    packet.setHeaderField("CSeq", QByteArray::number(seqNum) + ' ' + method);
    setContact(packet);
    packet.setHeaderField("To", addr.toUtf8());
    packet.setHeaderField("From", addr.toUtf8() + ";tag=" + ctx->tag);

    // authentication
    if (!ctx->challenge.isEmpty())
        packet.setHeaderField("Authorization", authorization(packet, ctx->challenge));
    if (!ctx->proxyChallenge.isEmpty())
        packet.setHeaderField("Proxy-Authorization", authorization(packet, ctx->proxyChallenge));

    packet.setHeaderField("User-Agent", QString("%1/%2").arg(qApp->applicationName(), qApp->applicationVersion()).toUtf8());
    if (method != "ACK" && method != "CANCEL")
        packet.setHeaderField("Allow", "INVITE, ACK, CANCEL, OPTIONS, BYE");
    return packet;
}

SipMessage SipClientPrivate::buildResponse(const SipMessage &request)
{
    SipMessage response;
    foreach (const QByteArray &via, request.headerFieldValues("Via"))
        response.addHeaderField("Via", via);
    response.setHeaderField("From", request.headerField("From"));
    response.setHeaderField("To", request.headerField("To"));
    response.setHeaderField("Call-ID", request.headerField("Call-ID"));
    response.setHeaderField("CSeq", request.headerField("CSeq"));
    foreach (const QByteArray &route, request.headerFieldValues("Record-Route"))
        response.addHeaderField("Record-Route", route);
    setContact(response);
    response.setHeaderField("User-Agent", QString("%1/%2").arg(qApp->applicationName(), qApp->applicationVersion()).toUtf8());
    return response;
}

SipMessage SipClientPrivate::buildRetry(const SipMessage &original, SipCallContext *ctx)
{
    SipMessage request = original;

    request.setHeaderField("CSeq", QByteArray::number(ctx->cseq++) + ' ' + request.method());
    if (!ctx->challenge.isEmpty())
        request.setHeaderField("Authorization", authorization(request, ctx->challenge));
    if (!ctx->proxyChallenge.isEmpty())
        request.setHeaderField("Proxy-Authorization", authorization(request, ctx->proxyChallenge));
    setContact(request);

    return request;
}

void SipClientPrivate::handleReply(const SipMessage &reply)
{
    // find transaction
    const QMap<QByteArray, QByteArray> viaParams = SipMessage::valueParameters(reply.headerField("Via"));
    foreach (SipTransaction *transaction, transactions) {
        if (transaction->branch() == viaParams.value("branch")) {
            transaction->messageReceived(reply);
            return;
        }
    }
}

void SipClientPrivate::setContact(SipMessage &request)
{
    request.setHeaderField("Contact", QString("<sip:%1@%2:%3>").arg(
        username,
        localAddress.toString(),
        QString::number(socket->localPort())).toUtf8());
}

void SipClientPrivate::setState(SipClient::State newState)
{
    if (state != newState)
    {
        state = newState;
        emit q->stateChanged(state);

        if (state == SipClient::ConnectedState)
            emit q->connected();
        else if (state == SipClient::DisconnectedState)
            emit q->disconnected();
    }
}

SipClient::SipClient(QObject *parent)
    : QXmppLoggable(parent)
{
    bool check;
    Q_UNUSED(check);

    qRegisterMetaType<SipClient::State>("SipClient::State");
    qRegisterMetaType<SipMessage>("SipMessage");

    d = new SipClientPrivate(this);
    d->socket = new QUdpSocket(this);
    check = connect(d->socket, SIGNAL(readyRead()),
                    this, SLOT(datagramReceived()));
    Q_ASSERT(check);

    d->connectTimer = new QTimer(this);
    check = connect(d->connectTimer, SIGNAL(timeout()),
                    this, SLOT(connectToServer()));
    Q_ASSERT(check);

    d->stunTimer = new QTimer(this);
    d->stunTimer->setSingleShot(true);
    check = connect(d->stunTimer, SIGNAL(timeout()),
                    this, SLOT(sendStun()));
    Q_ASSERT(check);

    // DNS lookups
    check = connect(&d->sipDns, SIGNAL(finished()),
                    this, SLOT(_q_sipDnsLookupFinished()));
    Q_ASSERT(check);

    check = connect(&d->stunDns, SIGNAL(finished()),
                    this, SLOT(_q_stunDnsLookupFinished()));
    Q_ASSERT(check);
}

SipClient::~SipClient()
{
    if (d->state == ConnectedState)
        disconnectFromServer();
    delete d;
}

int SipClient::activeCalls() const
{
    return d->calls.size();
}

SipCall *SipClient::call(const QString &recipient)
{
    if (d->state != ConnectedState) {
        warning("Cannot dial call, not connected to server");
        return 0;
    }

    // construct call
    SipCall *call = new SipCall(recipient, SipCall::OutgoingDirection, this);
    info(QString("SIP call %1 to %2").arg(call->id(), recipient));

    // register call
    connect(call, SIGNAL(destroyed(QObject*)),
            this, SLOT(callDestroyed(QObject*)));
    d->calls << call;

    emit activeCallsChanged(d->calls.size());
    emit callStarted(call);

    return call;
}

void SipClient::callDestroyed(QObject *object)
{
    d->calls.removeAll(static_cast<SipCall*>(object));
    emit activeCallsChanged(d->calls.size());
}

void SipClient::connectToServer()
{
    // schedule retry
    d->connectTimer->start(60000);

    // listen for SIP
    if (d->socket->state() == QAbstractSocket::UnconnectedState) {
        if (!d->socket->bind()) {
            warning("Could not start listening for SIP");
            return;
        }
        debug(QString("Listening for SIP on port %1").arg(
            QString::number(d->socket->localPort())));
    }

    // perform DNS SRV lookups
    debug(QString("Looking up STUN server for domain %1").arg(d->domain));
    d->stunDns.setType(QDnsLookup::SRV);
    d->stunDns.setName("_stun._udp." + d->domain);
    d->stunDns.lookup();

    debug(QString("Looking up SIP server for domain %1").arg(d->domain));
    d->sipDns.setType(QDnsLookup::SRV);
    d->sipDns.setName("_sip._udp." + d->domain);
    d->sipDns.lookup();
}

void SipClient::datagramReceived()
{
    if (!d->socket->hasPendingDatagrams())
        return;

    // receive datagram
    const qint64 size = d->socket->pendingDatagramSize();
    QByteArray buffer(size, 0);
    QHostAddress remoteHost;
    quint16 remotePort;
    d->socket->readDatagram(buffer.data(), buffer.size(), &remoteHost, &remotePort);

    // check whether it's a STUN packet
    quint32 messageCookie;
    QByteArray messageId;
    quint16 messageType = QXmppStunMessage::peekType(buffer, messageCookie, messageId);
    if (messageType && messageCookie == d->stunCookie && messageId == d->stunId)
    {
        QXmppStunMessage message;
        if (!message.decode(buffer))
            return;

#ifdef SIP_DEBUG_STUN
        logReceived(QString("STUN packet from %1 port %2\n%3").arg(
            remoteHost.toString(),
            QString::number(remotePort),
            message.toString()));
#endif

        const QHostAddress oldReflexiveAddress = d->stunReflexiveAddress;
        const quint16 oldReflexivePort = d->stunReflexivePort;

        // store reflexive address
        if (!message.xorMappedHost.isNull() && message.xorMappedPort != 0) {
            d->stunReflexiveAddress = message.xorMappedHost;
            d->stunReflexivePort = message.xorMappedPort;
        } else if (!message.mappedHost.isNull() && message.mappedPort != 0) {
            d->stunReflexiveAddress = message.mappedHost;
            d->stunReflexivePort = message.mappedPort;
        }

        // check whether the reflexive address has changed
        bool doRegister = false;
        if (d->stunReflexiveAddress != oldReflexiveAddress || d->stunReflexivePort != oldReflexivePort) {
            debug(QString("STUN reflexive address changed to %1 port %2").arg(
                d->stunReflexiveAddress.toString(),
                QString::number(d->stunReflexivePort)));

            // update local address
            const QList<QHostAddress> addresses = QXmppIceComponent::discoverAddresses();
            foreach (const QHostAddress &address, addresses) {
                if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                    d->localAddress = address;
                    break;
                }
            }

            // clear credentials
            d->challenge.clear();
            d->proxyChallenge.clear();

            doRegister = true;
        }
        d->stunDone = true;

        if (doRegister)
            registerWithServer();

        d->stunTimer->start(STUN_EXPIRE_MS);
        return;
    }

#ifdef SIP_DEBUG_SIP
    logReceived(QString("SIP packet from %1\n%2").arg(remoteHost.toString(), QString::fromUtf8(buffer)));
#endif

    // parse SIP message
    SipMessage reply(buffer);

    // find corresponding call
    SipCall *currentCall = 0;
    const QByteArray callId = reply.headerField("Call-ID");
    if (callId != d->id)
    {
        foreach (SipCall *potentialCall, d->calls)
        {
            if (potentialCall->id() == callId)
            {
                currentCall = potentialCall;
                break;
            }
        }
    }

    // check whether it's a request or a response
    if (reply.isRequest()) {
        if (currentCall) {
            currentCall->d->handleRequest(reply);
        } else if (!currentCall && reply.method() == "INVITE") {
            const QByteArray from = reply.headerField("From");
            const QByteArray to = reply.headerField("To");
            info(QString("SIP call from %1").arg(QString::fromUtf8(from)));

            // construct call
            currentCall = new SipCall(from, SipCall::IncomingDirection, this);
            currentCall->d->id = reply.headerField("Call-ID");

            QMap<QByteArray, QByteArray> params = SipMessage::valueParameters(to);
            if (params.contains("tag")) {
                currentCall->d->tag = params.value("tag");
            } else {
                reply.setHeaderField("To", to + ";tag=" + currentCall->d->tag);
            }

            // register call
            connect(currentCall, SIGNAL(destroyed(QObject*)),
                    this, SLOT(callDestroyed(QObject*)));
            d->calls << currentCall;
            emit activeCallsChanged(d->calls.size());

            currentCall->d->handleRequest(reply);
            emit callReceived(currentCall);
        }
    } else if (reply.isReply()) {
        if (currentCall)
            currentCall->d->handleReply(reply);
        else if (callId == d->id)
            d->handleReply(reply);
    } else {
        //warning("SIP packet is neither request nor reply");
    }
}

void SipClient::disconnectFromServer()
{
    // stop timers
    d->connectTimer->stop();
    d->stunTimer->stop();
    d->stunDone = false;

    // terminate calls
    foreach (SipCall *call, d->calls)
        call->hangup();

    // unregister
    if (d->state == SipClient::ConnectedState) {
        debug(QString("Disconnecting from SIP server %1:%2").arg(d->serverAddress.toString(), QString::number(d->serverPort)));
        const QByteArray uri = QString("sip:%1").arg(d->domain).toUtf8();
        SipMessage request = d->buildRequest("REGISTER", uri, d, d->cseq++);
        request.setHeaderField("Contact", request.headerField("Contact") + ";expires=0");
        d->transactions << new SipTransaction(request, this, this);

        d->setState(DisconnectingState);
    } else {
        d->setState(DisconnectedState);
    }

}

void SipClient::registerWithServer()
{
    if (d->serverAddress.isNull() || !d->serverPort)
        return;

    // register
    debug(QString("Connecting to SIP server %1:%2").arg(d->serverAddress.toString(), QString::number(d->serverPort)));

    const QByteArray uri = QString("sip:%1").arg(d->domain).toUtf8();
    SipMessage request = d->buildRequest("REGISTER", uri, d, d->cseq++);
    request.setHeaderField("Expires", QByteArray::number(EXPIRE_SECONDS));
    d->transactions << new SipTransaction(request, this, this);

    d->setState(ConnectingState);
}

/** Send a SIP message.
 *
 * @param message
 */
void SipClient::sendMessage(const SipMessage &message)
{
#ifdef SIP_DEBUG_SIP
    logSent(QString("SIP packet to %1:%2\n%3").arg(
            d->serverAddress.toString(),
            QString::number(d->serverPort),
            QString::fromUtf8(message.toByteArray())));
#endif
    d->socket->writeDatagram(message.toByteArray(), d->serverAddress, d->serverPort);
}

/** Send a STUN binding request.
 */
void SipClient::sendStun()
{
    QXmppStunMessage request;
    request.setType(QXmppStunMessage::Binding | QXmppStunMessage::Request);

    d->stunCookie = request.cookie();
    d->stunId = request.id();

#ifdef SIP_DEBUG_STUN
    logSent(QString("STUN packet to %1 port %2\n%3").arg(d->stunServerAddress.toString(),
            QString::number(d->stunServerPort), request.toString()));
#endif
    d->socket->writeDatagram(request.encode(QByteArray(), false), d->stunServerAddress, d->stunServerPort);
    d->stunTimer->start(STUN_RETRY_MS);
}

void SipClient::_q_sipDnsLookupFinished()
{
    QString serverName;

    if (d->sipDns.error() == QDnsLookup::NoError &&
        !d->sipDns.serviceRecords().isEmpty()) {
        serverName = d->sipDns.serviceRecords().first().target();
        d->serverPort = d->sipDns.serviceRecords().first().port();
    } else {
        serverName = "sip." + d->domain;
        d->serverPort = 5060;
    }

    // lookup SIP host name
    QHostInfo::lookupHost(serverName, this, SLOT(_q_sipHostInfoFinished(QHostInfo)));
}

void SipClient::_q_sipHostInfoFinished(const QHostInfo &hostInfo)
{
    if (hostInfo.addresses().isEmpty()) {
        warning(QString("Could not lookup SIP server %1").arg(hostInfo.hostName()));
        return;
    }
    d->serverAddress = hostInfo.addresses().first();

    if (d->stunDone)
        registerWithServer();
}


void SipClient::_q_stunDnsLookupFinished()
{
    QString serverName;

    if (d->stunDns.error() == QDnsLookup::NoError &&
        !d->stunDns.serviceRecords().isEmpty()) {
        serverName = d->stunDns.serviceRecords().first().target();
        d->stunServerPort = d->stunDns.serviceRecords().first().port();
    } else {
        serverName = "stun." + d->domain;
        d->stunServerPort = 3478;
    }

    // lookup STUN host name
    QHostInfo::lookupHost(serverName, this, SLOT(_q_stunHostInfoFinished(QHostInfo)));
}

void SipClient::_q_stunHostInfoFinished(const QHostInfo &hostInfo)
{
    if (hostInfo.addresses().isEmpty()) {
        warning(QString("Could not lookup STUN server %1").arg(hostInfo.hostName()));
        return;
    }
    d->stunServerAddress = hostInfo.addresses().first();

    // send STUN binding request
    sendStun();
}

SipClient::State SipClient::state() const
{
    return d->state;
}

void SipClient::transactionFinished()
{
    SipTransaction *transaction = qobject_cast<SipTransaction*>(sender());
    if (!transaction || !d->transactions.removeAll(transaction))
        return;
    transaction->deleteLater();

    // handle authentication
    const SipMessage reply = transaction->response();
    if (reply.statusCode() == 401 &&
        d->handleAuthentication(reply))
    {
        SipMessage request = d->buildRetry(transaction->request(), d);
        d->transactions << new SipTransaction(request, this, this);
        return;
    }

    const QByteArray method = transaction->request().method();
    if (method == "REGISTER") {
        if (reply.statusCode() == 200) {
            if (d->state == SipClient::DisconnectingState) {
                d->setState(SipClient::DisconnectedState);
            } else {
                d->connectTimer->stop();
                d->setState(SipClient::ConnectedState);

                QList<QByteArray> contacts = reply.headerFieldValues("Contact");
                const QByteArray expectedContact = transaction->request().headerField("Contact");
                int expireSeconds = 0;
                foreach (const QByteArray &contact, contacts) {
                    if (contact.startsWith(expectedContact)) {
                        QMap<QByteArray, QByteArray> params = SipMessage::valueParameters(contact);
                        expireSeconds = params.value("expires").toInt();
                        if (expireSeconds > 0)
                            break;
                    }
                }
                // if we could not find an "expires" parameter for our contact, fall back to "Expires" header
                if (expireSeconds <= 0)
                    expireSeconds = reply.headerField("Expires").toInt();

                // schedule next register
                const int marginSeconds = 10;
                if (expireSeconds > marginSeconds) {
                    debug(QString("Re-registering in %1 seconds").arg(expireSeconds - marginSeconds));
                    QTimer::singleShot((expireSeconds - marginSeconds) * 1000, this, SLOT(registerWithServer()));
                } else {
                    warning(QString("Could not schedule next register, expires is too short: %1 seconds").arg(expireSeconds));
                }

            }
        } else {
            warning("Register failed");
            if (d->state != SipClient::DisconnectingState)
                d->connectTimer->start();
            d->setState(SipClient::DisconnectedState);
        }
    }
    else if (method == "SUBSCRIBE") {
        if (reply.statusCode() != 200) {
            warning("Subscribe failed");
        }
    }
}

QString SipClient::displayName() const
{
    return d->displayName;
}

void SipClient::setDisplayName(const QString &displayName)
{
    d->displayName = displayName;
}

QString SipClient::domain() const
{
    return d->domain;
}

void SipClient::setDomain(const QString &domain)
{
    if (domain != d->domain) {
        d->domain = domain;
        emit domainChanged(d->domain);
    }
}

QHostAddress SipClient::localAddress() const
{
    return d->localAddress;
}

QXmppLogger *SipClient::logger() const
{
    return d->logger;
}

void SipClient::setLogger(QXmppLogger *logger)
{
    if (logger != d->logger) {
        if (d->logger) {
            disconnect(this, SIGNAL(logMessage(QXmppLogger::MessageType,QString)),
                       d->logger, SLOT(log(QXmppLogger::MessageType,QString)));
        }

        d->logger = logger;
        if (d->logger) {
            connect(this, SIGNAL(logMessage(QXmppLogger::MessageType,QString)),
                    d->logger, SLOT(log(QXmppLogger::MessageType,QString)));
        }

        emit loggerChanged(d->logger);
    }
}

QString SipClient::password() const
{
    return d->password;
}

void SipClient::setPassword(const QString &password)
{
    d->password = password;
}

QString SipClient::username() const
{
    return d->username;
}

void SipClient::setUsername(const QString &username)
{
    d->username = username;
}

SipMessage::SipMessage(const QByteArray &bytes)
    : m_statusCode(0)
{
    const QByteArrayMatcher crlf("\r\n");
    const QByteArrayMatcher colon(":");

    int j = crlf.indexIn(bytes);
    if (j < 0)
        return;

    // parse status
    const QByteArray status = bytes.left(j);
    if (status.size() >= 10 && status.startsWith("SIP/2.0 ")) {
        m_statusCode = status.mid(8, 3).toInt();
        m_reasonPhrase = QString::fromUtf8(status.mid(12));
    }
    else if (status.size() > 10 && status.endsWith(" SIP/2.0")) {
        int n = status.indexOf(' ');
        m_method = status.left(n);
        m_uri = status.mid(n + 1, j - n - 9);
    } else {
        j = -1;
    }

    // parse headers
    const QByteArray header = bytes.mid(j+1);
    int i = 0;
    while (i < header.count()) {
        int n = crlf.indexIn(header, i);

        if (n < 0) {
            // something is wrong
            qWarning("Missing end of line in SIP header");
            return;
        }
        else if (n == i) {
            // end of header
            i = n + 2;
            break;
        }

        // parse header field
        int j = colon.indexIn(header, i);
        if (j == -1)
            break;
        QByteArray field = header.mid(i, j - i).trimmed();
        j++;
        QByteArray value = header.mid(j, n - j).trimmed();

        // expand shortcuts
        if (field == "c")
            field = "Content-Type";
        else if (field == "f")
            field = "From";
        else if (field == "i")
            field = "Call-ID";
        else if (field == "k")
            field = "Supported";
        else if (field == "l")
            field = "Content-Length";
        else if (field == "m")
            field = "Contact";
        else if (field == "t")
            field = "To";
        else if (field == "v")
            field = "Via";
        m_fields.append(qMakePair(field, value));

        i = n + 2;
    }
    if (i >= 0)
        m_body = header.mid(i);
}

/** Returns the concatenated values for a header.
 *
 * @param name
 */
QByteArray SipMessage::headerField(const QByteArray &name) const
{
    QList<QByteArray> allValues = headerFieldValues(name);

    QByteArray result;
    bool first = true;
    foreach (const QByteArray &value, allValues) {
        if (!first)
            result += ", ";
        first = false;
        result += value;
    }
    return result;
}

/** Returns the values for a header.
 *
 * @param name
 */
QList<QByteArray> SipMessage::headerFieldValues(const QByteArray &name) const
{
    QList<QByteArray> result;
    QList<QPair<QByteArray, QByteArray> >::ConstIterator it = m_fields.constBegin(),
                                                        end = m_fields.constEnd();
    for ( ; it != end; ++it) {
        if (qstricmp(name.constData(), it->first) == 0) {
            QList<QByteArray> bits = it->second.split(',');
            foreach (const QByteArray &bit, bits)
                result += bit.trimmed();
        }
    }

    return result;
}

/** Returns the parameters for a field value.
 *
 * @param value
 */
QMap<QByteArray, QByteArray> SipMessage::valueParameters(const QByteArray &value)
{
    QMap<QByteArray, QByteArray> params;
    // FIXME: this is a very, very naive implementation
    QList<QByteArray> bits = value.split(';');
    if (bits.size() > 1)
    {
        bits.removeFirst();
        foreach (const QByteArray &bit, bits)
        {
            int i = bit.indexOf('=');
            if (i >= 0)
                params[bit.left(i)] = bit.mid(i+1);
        }
    }
    return params;
}

void SipMessage::addHeaderField(const QByteArray &name, const QByteArray &data)
{
    m_fields.append(qMakePair(name, data));
}

void SipMessage::removeHeaderField(const QByteArray &name)
{
    QList<QPair<QByteArray, QByteArray> >::Iterator it = m_fields.begin();
    while (it != m_fields.end()) {
        if (qstricmp(name.constData(), it->first) == 0)
            it = m_fields.erase(it);
        else
            ++it;
    }
}

void SipMessage::setHeaderField(const QByteArray &name, const QByteArray &data)
{
    removeHeaderField(name);
    m_fields.append(qMakePair(name, data));
}

bool SipMessage::isReply() const
{
    return m_statusCode != 0;
}

bool SipMessage::isRequest() const
{
    return !m_method.isEmpty() && !m_uri.isEmpty();
}

QByteArray SipMessage::body() const
{
    return m_body;
}

void SipMessage::setBody(const QByteArray &body)
{
    m_body = body;
}

QByteArray SipMessage::method() const
{
    return m_method;
}

void SipMessage::setMethod(const QByteArray &method)
{
    m_method = method;
}

QByteArray SipMessage::uri() const
{
    return m_uri;
}

void SipMessage::setUri(const QByteArray &uri)
{
    m_uri = uri;
}

QString SipMessage::reasonPhrase() const
{
    return m_reasonPhrase;
}

void SipMessage::setReasonPhrase(const QString &reasonPhrase)
{
    m_reasonPhrase = reasonPhrase;
}

quint32 SipMessage::sequenceNumber() const
{
    return headerField("CSeq").split(' ').first().toUInt();
}

int SipMessage::statusCode() const
{
    return m_statusCode;
}

void SipMessage::setStatusCode(int statusCode)
{
    m_statusCode = statusCode;
}

QByteArray SipMessage::toByteArray() const
{
    QByteArray ba;

    if (!m_method.isEmpty()) {
        ba += m_method;
        ba += ' ';
        ba += m_uri;
        ba += " SIP/2.0\r\n";
    } else {
        ba += "SIP/2.0 ";
        ba += QByteArray::number(m_statusCode);
        ba += ' ';
        ba += m_reasonPhrase.toUtf8();
        ba += "\r\n";
    }

    bool hasLength = false;
    QList<QPair<QByteArray, QByteArray> >::ConstIterator it = m_fields.constBegin(),
                                                        end = m_fields.constEnd();
    for ( ; it != end; ++it) {
        if (qstricmp("Content-Length", it->first) == 0)
            hasLength = true;
        ba += it->first;
        ba += ": ";
        ba += it->second;
        ba += "\r\n";
    }
    if (!hasLength)
        ba += "Content-Length: " + QByteArray::number(m_body.size()) + "\r\n";

    ba += "\r\n";
    return ba + m_body;
}

SipTransaction::SipTransaction(const SipMessage &request, SipClient *client, QObject *parent)
    : QXmppLoggable(parent),
    m_request(request),
    m_state(Trying)
{
    bool check;
    Q_UNUSED(check);

    check = connect(this, SIGNAL(sendMessage(SipMessage)),
                    client, SLOT(sendMessage(SipMessage)));
    Q_ASSERT(check);

    check = connect(this, SIGNAL(finished()),
                    parent, SLOT(transactionFinished()));
    Q_ASSERT(check);

    // Timer F
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    check = connect(m_timeoutTimer, SIGNAL(timeout()),
                    this, SLOT(timeout()));
    Q_ASSERT(check);
    m_timeoutTimer->start(64 * SIP_T1_TIMER);

    // Timer E
    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    check = connect(m_retryTimer, SIGNAL(timeout()),
                    this, SLOT(retry()));
    Q_ASSERT(check);

    // Send packet immediately
    client->sendMessage(m_request);
    m_retryTimer->start(SIP_T1_TIMER);
}

QByteArray SipTransaction::branch() const
{
    const QMap<QByteArray, QByteArray> params = SipMessage::valueParameters(m_request.headerField("Via"));
    return params.value("branch");
}

void SipTransaction::messageReceived(const SipMessage &message)
{
    if (message.statusCode() < 200) {
        if (m_state == Trying) {
            m_retryTimer->start(SIP_T2_TIMER);
            m_state = Proceeding;
        }
    } else {
        if (m_state == Trying || m_state == Proceeding) {
            m_retryTimer->stop();
            m_timeoutTimer->stop();
            m_response = message;
            m_state = Completed;
            emit finished();
        }
    }
}

SipMessage SipTransaction::request() const
{
    return m_request;
}

SipMessage SipTransaction::response() const
{
    return m_response;
}

SipTransaction::State SipTransaction::state() const
{
    return m_state;
}

void SipTransaction::retry()
{
    emit sendMessage(m_request);

    // schedule next retry
    m_retryTimer->start(qMin(2 * m_retryTimer->interval(), SIP_T2_TIMER));
}

void SipTransaction::timeout()
{
    warning(QString("%1 transaction timed out").arg(QString::fromUtf8(m_request.method())));
    m_retryTimer->stop();
    m_state = Terminated;
    emit finished();
}
