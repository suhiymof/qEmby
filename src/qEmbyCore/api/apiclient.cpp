#include "apiclient.h"

#include <QSysInfo>
#include <utility>

ApiClient::ApiClient(const ServerProfile& profile, NetworkManager* nm, QObject* parent)
    : QObject(parent), m_profile(profile), m_network(nm) {}

QMap<QString, QString> ApiClient::getAuthHeaders() const {
    QMap<QString, QString> headers;

    const QString userAgent = m_profile.effectiveUserAgent();
    if (!userAgent.isEmpty()) {
        // UA spoofing requested: derive a matching X-Emby-Authorization
        // identity from the UA string so the Emby dashboard session entry
        // does not contradict the UA (e.g. UA="RodelPlayer/..." while the
        // auth header still says Client="qEmby"). Mirrors the header shape
        // real third-party players send, e.g.
        //   Emby Client="RodelPlayer", Device="HOSTNAME",
        //   DeviceId="...", Version="2.2607.7.0"
        // UA shape: "RodelPlayer/2.2607.7.0 (Windows NT 10.0.26100; x64)"
        const QString clientName = userAgent.section('/', 0, 0).trimmed();
        const QString version = userAgent.section('/', 1).section(' ', 0, 0).trimmed();
        const QString device = QSysInfo::machineHostName();

        QString auth = QString("Emby Client=\"%1\", Device=\"%2\", "
                               "DeviceId=\"%3\", Version=\"%4\"")
                           .arg(clientName.isEmpty() ? QStringLiteral("qEmby") : clientName,
                                device,
                                m_profile.deviceId,
                                version.isEmpty() ? QStringLiteral("1.0") : version);
        if (!m_profile.accessToken.isEmpty()) {
            auth += QString(", Token=\"%1\"").arg(m_profile.accessToken);
        }
        headers.insert("X-Emby-Authorization", auth);
        headers.insert("User-Agent", userAgent);
        return headers;
    }

    QString auth = QString("MediaBrowser Client=\"qEmby\", Device=\"Desktop\", "
                           "DeviceId=\"%1\", Version=\"0.1\"")
                       .arg(m_profile.deviceId);

    if (!m_profile.accessToken.isEmpty()) {
        auth += QString(", Token=\"%1\"").arg(m_profile.accessToken);
    }

    headers.insert("X-Emby-Authorization", auth);
    return headers;
}

NetworkRequestOptions ApiClient::requestOptions() const {
    NetworkRequestOptions options;
    options.ignoreSslErrors = m_profile.ignoreSslVerification;
    return options;
}

QCoro::Task<QJsonObject> ApiClient::get(const QString& path) {
    QString fullUrl = m_profile.url + path;
    co_return co_await m_network->get(fullUrl, getAuthHeaders(),
                                      requestOptions());
}

QCoro::Task<QString> ApiClient::getText(const QString& path) {
    QString fullUrl = m_profile.url + path;
    co_return co_await m_network->getText(fullUrl, getAuthHeaders(),
                                          requestOptions());
}

QCoro::Task<QJsonObject> ApiClient::post(const QString& path, const QJsonObject& payload) {
    QString fullUrl = m_profile.url + path;
    co_return co_await m_network->post(fullUrl, getAuthHeaders(), payload,
                                       requestOptions());
}

QCoro::Task<QJsonObject> ApiClient::postArray(const QString& path, const QJsonArray& payload) {
    QString fullUrl = m_profile.url + path;
    co_return co_await m_network->postArray(fullUrl, getAuthHeaders(), payload,
                                            requestOptions());
}

QCoro::Task<QJsonObject> ApiClient::postBytes(const QString& path,
                                              QByteArray payload,
                                              QString contentType) {
    QString fullUrl = m_profile.url + path;
    co_return co_await m_network->postBytes(fullUrl, getAuthHeaders(),
                                            std::move(payload),
                                            std::move(contentType),
                                            requestOptions());
}

QCoro::Task<QJsonObject> ApiClient::postForm(const QString& path,
                                             const QUrlQuery& formData) {
    QString fullUrl = m_profile.url + path;
    co_return co_await m_network->postForm(fullUrl, getAuthHeaders(), formData,
                                           requestOptions());
}

QCoro::Task<QJsonObject> ApiClient::deleteResource(const QString& path) {
    QString fullUrl = m_profile.url + path;
    co_return co_await m_network->deleteResource(fullUrl, getAuthHeaders(),
                                                 requestOptions());
}
