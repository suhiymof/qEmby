#ifndef BILIBILIAUTHSERVICE_H
#define BILIBILIAUTHSERVICE_H

#include "../../qEmbyCore_global.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <qcorotask.h>

class QNetworkAccessManager;

// Bilibili QR login for the danmaku source. The account cookie (SESSDATA /
// bili_jct) gates the danmaku provider; the device cookie (buvid3) keeps the
// web API from hitting risk control. Modeled on TraktService: a process-wide
// singleton backed by its own QNetworkAccessManager.
class QEMBYCORE_EXPORT BiliBiliAuthService : public QObject
{
    Q_OBJECT
public:
    static BiliBiliAuthService *instance();

    BiliBiliAuthService(const BiliBiliAuthService &) = delete;
    BiliBiliAuthService &operator=(const BiliBiliAuthService &) = delete;

    bool isLoggedIn() const;
    QString userName() const;

    // Combined Cookie header for api.bilibili.com / comment.bilibili.com.
    QString cookieHeader() const;
    // Lazily ensures a buvid3 device cookie is available (stored on first use).
    QCoro::Task<void> ensureBuvid3();

    // QR login step 1: returns the URL to display as a QR code.
    QCoro::Task<QString> generateLoginUrl();
    // QR login step 2: poll once. Returns the Bilibili status code:
    //   0 = success (cookies stored), 86101 = not scanned yet,
    //   86090 = scanned but not confirmed, 86038 = expired,
    //   negative = network/parse failure.
    QCoro::Task<int> pollLogin();

    void signOut();

private:
    explicit BiliBiliAuthService(QObject *parent = nullptr);

    struct RawReply {
        int status = 0;
        QByteArray body;
        QMap<QString, QString> cookies;
    };
    QCoro::Task<RawReply> getRaw(const QString &url);

    QString m_qrcodeKey;
    QNetworkAccessManager *m_nam = nullptr;
};

#endif // BILIBILIAUTHSERVICE_H
