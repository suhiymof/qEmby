#include "aggregatedsearchview.h"
#include <services/aggregator/searchaggregator.h>
#include <services/manager/servermanager.h>

namespace {
constexpr int kPerServerSearchLimit = 50;
}

AggregatedSearchView::AggregatedSearchView(QEmbyCore* core, QWidget* parent)
    : AggregatedViewBase(core, parent)
{
    // 类别 tab：全部 / 影视 / 剧集 / 演员。
    addCategoryTab(tr("All"), /*checked=*/true);
    addCategoryTab(tr("Movies"));
    addCategoryTab(tr("Series"));
    addCategoryTab(tr("People"));
}

void AggregatedSearchView::search(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return;
    m_query = trimmed;
    startLoad();
}

void AggregatedSearchView::startLoad()
{
    if (m_query.isEmpty() || !m_aggregator) {
        clearSections();
        return;
    }

    clearSections();
    createSkeletonSections();

    // 流式：每个服务器结果到达 → fillSection（先返回先显示）。
    m_aggregator->searchAll(
        m_query, m_includeTypes, kPerServerSearchLimit,
        [this](const ServerProfile& profile, QList<MediaItem> items) {
            fillSection(profile, items);
        },
        [this](int /*attempted*/, int /*succeeded*/) {
            setAllSectionsLoaded();
        });
}

void AggregatedSearchView::onCategoryTabClicked(const QString& label)
{
    AggregatedViewBase::onCategoryTabClicked(label);
    m_includeTypes = includeTypesForTab(label);
    startLoad();
}

QString AggregatedSearchView::includeTypesForTab(const QString& label) const
{
    if (label == tr("Movies")) {
        return QStringLiteral("Movie");
    }
    if (label == tr("Series")) {
        return QStringLiteral("Series");
    }
    if (label == tr("People")) {
        return QStringLiteral("Person");
    }
    // "All" / 默认：电影 + 剧集 + 合集 + 演员。
    return QStringLiteral("Movie,Series,BoxSet,Person");
}
