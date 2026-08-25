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

    // Resolution order: per-server customUserAgent -> global
    // ConfigKeys::CustomUserAgent -> empty (keep qEmby defaults).
    QString effectiveUserAgent() const {
        if (!customUserAgent.trimmed().isEmpty()) {
            return customUserAgent.trimmed();
        }
        const QString global = ConfigStore::instance()
                ->get<QString>(ConfigKeys::CustomUserAgent, QString());
        return global.trimmed();
    }
};
#endif
