#ifndef DANMAKUSETTINGS_H
#define DANMAKUSETTINGS_H

#include "../../models/danmaku/danmakumodels.h"

#include <QList>
#include <QString>

class QEMBYCORE_EXPORT DanmakuSettings final
{
public:
    static DanmakuServerDefinition builtInOfficialDandanplayServer();
    static DanmakuServerDefinition builtInBilibiliServer();
    static QList<DanmakuServerDefinition> loadServers(QString serverId);
    static DanmakuServerDefinition selectedServer(QString serverId);
    static QString selectedServerId(QString serverId);
    static void saveServers(QString serverId,
                            QList<DanmakuServerDefinition> servers,
                            QString selectedServerId);
    // True if the user has explicitly configured at least one danmaku
    // server for the given Emby server. False for a server whose
    // danmaku\servers JSON is empty — loadServers would otherwise fall
    // back to a single enabled dandanplay builtIn (legacyServerDefinition
    // sets enabled=true) and pollute cross-server search/auto-match
    // with results from a server the user never opted in to.
    static bool hasConfiguredServers(QString serverId);
};

#endif 
