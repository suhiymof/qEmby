#ifndef SERVERPROFILE_H
#define SERVERPROFILE_H

#include "proxyconfig.h"
#include <QString>
#include <QUuid>
#include <config/config_keys.h>
#include <config/configstore.h>

struct ServerProfile {
    enum ServerType { Emby, Jellyfin };

    QString id = QUuid::createUuid().toString();
    QString name;
    QString url;
    ServerType type = Emby;
    bool ignoreSslVerification = false;

    QString userId;
    QString userName;
    QString accessToken;
    QString deviceId;
    bool isAdmin = false;
    bool canDownloadMedia = false;

    QString iconBase64;




    bool useGlobalProxy = false;
    ProxyConfig proxy;

    // Optional per-server User-Agent override. Some Emby servers run strict
    // UA whitelists and hang connections from unrecognized clients; setting
    // this makes qEmby present the given UA (and a matching X-Emby-Authorization
    // identity) for API and streaming requests to this server.
    QString customUserAgent;

    bool isValid() const { return !accessToken.isEmpty(); }

    // Built-in default client identity: a widely used third-party player.
    // Qt's implicit default varies by network backend and libmpv announces
    // itself as "libmpv", which some servers treat as unknown clients.
    static QString defaultUserAgent() {
        return QStringLiteral(
            "RodelPlayer/2.2607.7.0 (Windows NT 10.0.26100; x64)");
    }

    // Resolution order: per-server customUserAgent -> global
    // ConfigKeys::CustomUserAgent -> built-in default.
    QString effectiveUserAgent() const {
        const QString perServer = customUserAgent.trimmed();
        if (!perServer.isEmpty()) {
            return perServer;
        }
        const QString global = ConfigStore::instance()
                ->get<QString>(ConfigKeys::CustomUserAgent, QString());
        const QString trimmed = global.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
        return defaultUserAgent();
    }
};
#endif
