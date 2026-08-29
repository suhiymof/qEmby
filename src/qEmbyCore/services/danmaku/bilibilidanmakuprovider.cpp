#include "bilibilidanmakuprovider.h"

#include "bilibiliauthservice.h"

#include <QCryptographicHash>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

constexpr int kRequestTimeoutMs = 20000;
constexpr int kMaxSearchCandidates = 5;

constexpr char kUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
constexpr char kReferer[] = "https://www.bilibili.com/";

// WBI mixin key index table (stable across Bilibili web revisions).
constexpr int kWbiMixinKeyEncTab[64] = {
    46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35,
    27, 43, 5, 49, 33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13,
    37, 48, 7, 16, 24, 55, 40, 61, 26, 17, 0, 1, 60, 51, 30, 4,
    22, 25, 54, 21, 56, 59, 6, 63, 57, 62, 11, 36, 20, 34, 44, 52};

NetworkRequestOptions requestOptions()
{
    NetworkRequestOptions options;
    options.timeoutMs = kRequestTimeoutMs;
    return options;
}

QMap<QString, QString> requestHeaders(const QString &cookie)
{
    QMap<QString, QString> headers;
    headers.insert(QStringLiteral("User-Agent"), QString::fromLatin1(kUserAgent));
    headers.insert(QStringLiteral("Referer"), QString::fromLatin1(kReferer));
    if (!cookie.trimmed().isEmpty()) {
        headers.insert(QStringLiteral("Cookie"), cookie.trimmed());
    }
    return headers;
}

QString stripHtmlTags(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    return text.trimmed();
}

QString normalizedTitle(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return value;
}

double titleScore(const QString &lhs, const QString &rhs)
{
    const QString left = normalizedTitle(lhs);
    const QString right = normalizedTitle(rhs);
    if (left.isEmpty() || right.isEmpty()) {
        return 0.0;
    }
    if (left == right) {
        return 1.0;
    }
    if (left.contains(right) || right.contains(left)) {
        return 0.82;
    }
    if (left.size() < 2 || right.size() < 2) {
        return 0.0;
    }
    QSet<QString> leftPairs;
    QSet<QString> rightPairs;
    for (int i = 0; i + 1 < left.size(); ++i) {
        leftPairs.insert(left.mid(i, 2));
    }
    for (int i = 0; i + 1 < right.size(); ++i) {
        rightPairs.insert(right.mid(i, 2));
    }
    int common = 0;
    for (const QString &pair : std::as_const(leftPairs)) {
        if (rightPairs.contains(pair)) {
            ++common;
        }
    }
    return (2.0 * common) / (leftPairs.size() + rightPairs.size());
}

int extractEpisodeNumber(const QString &text)
{
    static const QList<QRegularExpression> patterns = {
        QRegularExpression(
            QStringLiteral(R"(第\s*0*(\d{1,4})\s*[话話集期回])")),
        QRegularExpression(
            QStringLiteral(R"((?:^|[^A-Za-z0-9])(?:EP?|Episode)\s*[._-]?\s*0*(\d{1,4})(?:[^A-Za-z0-9]|$))"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(R"((?:^|[^A-Za-z0-9])0*(\d{1,4})\s*[话話集期回](?:[^A-Za-z0-9]|$))"))};
    for (const QRegularExpression &pattern : patterns) {
        const QRegularExpressionMatch match = pattern.match(text);
        if (match.hasMatch()) {
            return match.captured(1).toInt();
        }
    }
    return 0;
}

QString mixinKey(const QString &imgKey, const QString &subKey)
{
    const QString raw = imgKey + subKey;
    QString key;
    key.reserve(32);
    for (int idx : kWbiMixinKeyEncTab) {
        if (idx < raw.size()) {
            key.append(raw.at(idx));
        }
    }
    return key.left(32);
}

// Builds a w_rid signature for the given params (see Bilibili WBI signing).
QMap<QString, QString> wbiSign(QMap<QString, QString> params,
                               const QString &imgKey, const QString &subKey)
{
    params.insert(QStringLiteral("wts"),
                  QString::number(QDateTime::currentSecsSinceEpoch()));
    QStringList keys = params.keys();
    std::sort(keys.begin(), keys.end());

    QUrlQuery query;
    for (const QString &key : std::as_const(keys)) {
        QString value = params.value(key);
        value.remove(QRegularExpression(QStringLiteral("[!'()*]")));
        query.addQueryItem(key, value);
    }
    QString encoded = query.toString(QUrl::FullyEncoded);
    const QString toHash = encoded + mixinKey(imgKey, subKey);
    const QString wRid = QString::fromLatin1(
        QCryptographicHash::hash(toHash.toUtf8(), QCryptographicHash::Md5).toHex());
    params.insert(QStringLiteral("w_rid"), wRid);
    return params;
}

struct WbiKeys {
    QString imgKey;
    QString subKey;
};

QCoro::Task<WbiKeys> fetchWbiKeys(NetworkManager *networkManager,
                                  const QString &cookie)
{
    WbiKeys keys;
    const QJsonObject response = co_await networkManager->get(
        QStringLiteral("https://api.bilibili.com/x/web-interface/nav"),
        requestHeaders(cookie), requestOptions());
    const QJsonObject wbi = response.value(QStringLiteral("data"))
                                .toObject()
                                .value(QStringLiteral("wbi_img"))
                                .toObject();
    const auto keyFromUrl = [](const QString &url) {
        return url.section('/', -1).section('.', 0, 0);
    };
    keys.imgKey = keyFromUrl(wbi.value(QStringLiteral("img_url")).toString());
    keys.subKey = keyFromUrl(wbi.value(QStringLiteral("sub_url")).toString());
    co_return keys;
}

// Inflates the raw-deflate payload Bilibili serves for comment.xml. The bytes
// are wrapped into a full zlib stream (constant header + adler32) so that
// qUncompress can process it.
QByteArray rawDeflateInflate(const QByteArray &raw)
{
    QByteArray stream;
    stream.reserve(raw.size() + 6);
    stream.append('\x78');
    stream.append('\x9c');
    stream.append(raw);

    quint32 a = 1;
    quint32 b = 0;
    for (const char c : raw) {
        a = (a + static_cast<quint8>(c)) % 65521;
        b = (b + a) % 65521;
    }
    const quint32 adler = (b << 16) | a;
    stream.append(static_cast<char>((adler >> 24) & 0xff));
    stream.append(static_cast<char>((adler >> 16) & 0xff));
    stream.append(static_cast<char>((adler >> 8) & 0xff));
    stream.append(static_cast<char>(adler & 0xff));
    return qUncompress(stream);
}

QList<DanmakuComment> parseDanmakuXml(const QByteArray &xml)
{
    QList<DanmakuComment> comments;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("d")) {
            const QString p =
                reader.attributes().value(QStringLiteral("p")).toString();
            const QStringList parts = p.split(',', Qt::KeepEmptyParts);
            if (parts.size() < 4) {
                continue;
            }
            const int mode = parts.at(1).toInt();
            // 1=scroll, 4=bottom, 5=top. Drop reverse (6) and advanced (7/8)
            // which the ASS composer cannot render.
            if (mode != 1 && mode != 4 && mode != 5) {
                continue;
            }
            DanmakuComment comment;
            comment.timeMs =
                static_cast<qint64>(parts.at(0).toDouble() * 1000.0);
            comment.mode = mode;
            comment.fontLevel = parts.at(2).toInt();
            comment.color = QColor::fromRgb(parts.at(3).toUInt());
            comment.text = reader.readElementText();
            if (comment.isValid()) {
                comments.append(comment);
            }
        }
    }
    return comments;
}

} // namespace

BiliBiliDanmakuProvider::BiliBiliDanmakuProvider(NetworkManager *networkManager)
    : m_networkManager(networkManager)
{
}

QCoro::Task<QList<DanmakuMatchCandidate>> BiliBiliDanmakuProvider::searchCandidates(
    DanmakuMediaContext context,
    DanmakuProviderConfig config,
    QString manualKeyword) const
{
    QList<DanmakuMatchCandidate> candidates;
    BiliBiliAuthService *auth = BiliBiliAuthService::instance();
    if (!m_networkManager || !auth->isLoggedIn()) {
        co_return candidates;
    }

    co_await auth->ensureBuvid3();
    const QString cookie = auth->cookieHeader();

    const QString querySubject =
        manualKeyword.trimmed().isEmpty()
            ? (context.isEpisode() ? context.seriesName.trimmed()
                                   : context.title.trimmed())
            : manualKeyword.trimmed();
    if (querySubject.isEmpty()) {
        co_return candidates;
    }

    const WbiKeys wbi = co_await fetchWbiKeys(m_networkManager, cookie);
    if (wbi.imgKey.isEmpty() || wbi.subKey.isEmpty()) {
        co_return candidates;
    }

    const QMap<QString, QString> searchParams = wbiSign(
        {{QStringLiteral("search_type"), QStringLiteral("video")},
         {QStringLiteral("keyword"), querySubject},
         {QStringLiteral("page"), QStringLiteral("1")},
         {QStringLiteral("page_size"), QStringLiteral("20")}},
        wbi.imgKey, wbi.subKey);

    QUrl searchUrl(QStringLiteral("https://api.bilibili.com/x/web-interface/wbi/search/type"));
    QUrlQuery query;
    for (auto it = searchParams.cbegin(); it != searchParams.cend(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }
    searchUrl.setQuery(query);

    const QJsonObject response = co_await m_networkManager->get(
        searchUrl.toString(), requestHeaders(cookie), requestOptions());
    const QJsonArray results =
        response.value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("result"))
            .toArray();

    int resolved = 0;
    for (const QJsonValue &resultValue : results) {
        if (resolved >= kMaxSearchCandidates) {
            break;
        }
        const QJsonObject result = resultValue.toObject();
        const QString bvid = result.value(QStringLiteral("bvid")).toString().trimmed();
        const QString resultTitle = stripHtmlTags(
            result.value(QStringLiteral("title")).toString());
        if (bvid.isEmpty() || resultTitle.isEmpty()) {
            continue;
        }

        // bvid -> cid list (multi-part videos become multiple candidates).
        QUrl pagelistUrl(QStringLiteral("https://api.bilibili.com/x/player/pagelist"));
        QUrlQuery pagelistQuery;
        pagelistQuery.addQueryItem(QStringLiteral("bvid"), bvid);
        pagelistUrl.setQuery(pagelistQuery);

        QJsonObject pagelist;
        try {
            pagelist = co_await m_networkManager->get(
                pagelistUrl.toString(), requestHeaders(cookie), requestOptions());
        } catch (const std::exception &e) {
            qWarning().noquote() << "[BiliBili] pagelist failed"
                                 << "| bvid:" << bvid << "| error:" << e.what();
            continue;
        }
        const QJsonArray pages = pagelist.value(QStringLiteral("data")).toArray();
        for (const QJsonValue &pageValue : pages) {
            const QJsonObject page = pageValue.toObject();
            const QString cid = page.value(QStringLiteral("cid")).toVariant().toString().trimmed();
            if (cid.isEmpty()) {
                continue;
            }
            const QString partTitle =
                page.value(QStringLiteral("part")).toString().trimmed();

            DanmakuMatchCandidate candidate;
            candidate.provider = QStringLiteral("bilibili");
            candidate.targetId = cid;
            candidate.title = partTitle.isEmpty() ? resultTitle : partTitle;
            candidate.subtitle = resultTitle;
            candidate.episodeNumber = extractEpisodeNumber(candidate.title);
            candidate.durationMs = static_cast<qint64>(
                page.value(QStringLiteral("duration")).toVariant().toDouble() * 1000.0);
            candidate.commentCount =
                result.value(QStringLiteral("danmaku")).toInt();
            candidate.matchReason = QStringLiteral("search");
            candidate.score = titleScore(querySubject, resultTitle) * 60.0;
            if (context.isEpisode() && context.episodeNumber > 0 &&
                candidate.episodeNumber == context.episodeNumber) {
                candidate.score += 24.0;
            }
            if (candidate.isValid()) {
                candidates.append(candidate);
            }
        }
        ++resolved;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const DanmakuMatchCandidate &left,
                 const DanmakuMatchCandidate &right) {
                  return left.score > right.score;
              });
    co_return candidates;
}

QCoro::Task<QList<DanmakuComment>> BiliBiliDanmakuProvider::fetchComments(
    DanmakuMatchCandidate candidate,
    DanmakuProviderConfig config) const
{
    QList<DanmakuComment> comments;
    BiliBiliAuthService *auth = BiliBiliAuthService::instance();
    if (!m_networkManager || !candidate.isValid() || !auth->isLoggedIn()) {
        co_return comments;
    }

    co_await auth->ensureBuvid3();
    const QString cookie = auth->cookieHeader();

    const QByteArray raw = co_await m_networkManager->getBytes(
        QStringLiteral("https://comment.bilibili.com/%1.xml")
            .arg(candidate.targetId),
        requestHeaders(cookie), requestOptions());

    QByteArray xml = raw;
    // Bilibili serves raw-deflate; inflate it unless QNAM already did.
    if (!raw.trimmed().startsWith('<')) {
        xml = rawDeflateInflate(raw);
    }
    if (xml.isEmpty()) {
        co_return comments;
    }

    comments = parseDanmakuXml(xml);
    qDebug().noquote() << "[BiliBili] Comments fetched"
                       << "| targetId:" << candidate.targetId
                       << "| count:" << comments.size();
    co_return comments;
}
