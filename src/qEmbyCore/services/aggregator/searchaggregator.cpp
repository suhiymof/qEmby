#include "searchaggregator.h"
#include "../../api/apiclient.h"
#include "../manager/servermanager.h"
#include <QCoroCore>
#include <QCoroNetwork>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QSharedPointer>
#include <atomic>

namespace {
// 单个服务器 API 超时（毫秒）：超时视为该服务器失败，不阻塞其他。
constexpr int kPerServerTimeoutMs = 5000;

// 与 MediaService 卡片 tooltip 字段保持一致，保证年份/集数/可下载等齐全。
QString mediaCardFields()
{
    return QStringLiteral(
        "ProductionYear,RecursiveItemCount,CanDownload,"
        "Overview,Genres,CommunityRating,CriticRating,"
        "OfficialRating,RunTimeTicks,People,Studios,"
        "ProviderIds,ChildCount,ParentId,Path,MediaStreams");
}
} // namespace

// 共享的 fan-out 完成状态：fanOut() 返回后由协程完成回调继续使用，
// 用 QSharedPointer 避免悬垂引用。
struct FanOutState {
    std::atomic<int> completed{0};
    std::atomic<int> succeeded{0};
    int total = 0;
    int generation = 0;
    SearchAggregator::CompleteCallback onComplete;
};

SearchAggregator::SearchAggregator(ServerManager* serverManager,
                                   NetworkManager* network,
                                   QObject* parent)
    : QObject(parent)
    , m_serverManager(serverManager)
    , m_network(network)
{
}

void SearchAggregator::cancel()
{
    ++m_generation;
}

void SearchAggregator::searchAll(const QString& query,
                                 const QString& includeItemTypes,
                                 int perServerLimit,
                                 ServerResultCallback onServerResult,
                                 CompleteCallback onComplete)
{
    QString pathTemplate =
        QStringLiteral("/Users/%1/Items?SearchTerm=%2&Recursive=true&Fields=%3")
            .arg(QStringLiteral("{uid}"),
                 QString::fromUtf8(QUrl::toPercentEncoding(query)),
                 mediaCardFields());
    if (!includeItemTypes.isEmpty())
        pathTemplate += QStringLiteral("&IncludeItemTypes=%1").arg(includeItemTypes);
    if (perServerLimit > 0)
        pathTemplate += QStringLiteral("&Limit=%1").arg(perServerLimit);
    fanOut(pathTemplate, std::move(onServerResult), std::move(onComplete));
}

void SearchAggregator::getResumeItemsAllServers(
    int perServerLimit, ServerResultCallback onServerResult,
    CompleteCallback onComplete)
{
    QString pathTemplate =
        QStringLiteral("/Users/%1/Items?Filters=IsResumable&Recursive=true&Fields=%2")
            .arg(QStringLiteral("{uid}"), mediaCardFields());
    if (perServerLimit > 0)
        pathTemplate += QStringLiteral("&Limit=%1").arg(perServerLimit);
    fanOut(pathTemplate, std::move(onServerResult), std::move(onComplete));
}

void SearchAggregator::getFavoritesAllServers(
    const QString& includeItemTypes, int perServerLimit,
    ServerResultCallback onServerResult, CompleteCallback onComplete)
{
    QString pathTemplate =
        QStringLiteral("/Users/%1/Items?Filters=IsFavorite&Recursive=true&Fields=%2")
            .arg(QStringLiteral("{uid}"), mediaCardFields());
    if (!includeItemTypes.isEmpty())
        pathTemplate += QStringLiteral("&IncludeItemTypes=%1").arg(includeItemTypes);
    if (perServerLimit > 0)
        pathTemplate += QStringLiteral("&Limit=%1").arg(perServerLimit);
    fanOut(pathTemplate, std::move(onServerResult), std::move(onComplete));
}

void SearchAggregator::fanOut(const QString& pathTemplate,
                              ServerResultCallback onServerResult,
                              CompleteCallback onComplete)
{
    if (!m_serverManager) {
        if (onComplete) onComplete(0, 0);
        return;
    }

    const QList<ServerProfile> servers = m_serverManager->servers();
    if (servers.isEmpty()) {
        if (onComplete) onComplete(0, 0);
        return;
    }

    const int gen = ++m_generation;
    auto state = QSharedPointer<FanOutState>::create();
    state->total = servers.size();
    state->generation = gen;
    state->onComplete = std::move(onComplete);

    for (const ServerProfile& profile : servers) {
        if (!profile.isValid()) {
            ++state->completed;
            continue;
        }

        // 每个服务器一个协程：co_await 期间不阻塞 UI 线程，13+ 个协程
        // 在网络等待时交错执行 → 天然并发。
        auto task = [this, profile, pathTemplate, gen, onServerResult,
                     state]() -> QCoro::Task<void> {
            // 独立临时 ApiClient——不依赖/不切换 active server。
            ApiClient client(profile, m_network);

            QList<MediaItem> items;
            try {
                const QString uid = profile.userId;
                QString path = pathTemplate;
                path.replace(QStringLiteral("{uid}"), uid);
                const QJsonObject response =
                    co_await client.get(path, kPerServerTimeoutMs);
                const QJsonArray arr =
                    response.value(QStringLiteral("Items")).toArray();
                items.reserve(arr.size());
                for (const QJsonValue& v : arr)
                    items.append(MediaItem::fromJson(v.toObject()));
                ++state->succeeded;
            } catch (...) {
                // 单服务器失败/超时不阻塞其他（失败隔离）。
                items.clear();
            }

            // 流式回调：结果立即上抛（若本 generation 仍有效）。
            if (onServerResult && gen == this->m_generation) {
                onServerResult(profile, items);
            }

            if (++state->completed == state->total) {
                if (state->onComplete && gen == this->m_generation) {
                    state->onComplete(state->total, state->succeeded.load());
                }
            }
            co_return;
        }();

        // 保持协程存活（fire-and-forget；对象析构自动取消）。
        QCoro::connect(std::move(task), this, []() {});
    }
}
