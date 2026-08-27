#include "aggregatedfavoritesview.h"
#include <services/aggregator/searchaggregator.h>
#include <services/manager/servermanager.h>

namespace {
constexpr int kPerServerFavoritesLimit = 50;
}

AggregatedFavoritesView::AggregatedFavoritesView(QEmbyCore* core, QWidget* parent)
    : AggregatedViewBase(core, parent)
{
    // 类别 tab：电影 / 电视剧 / 单集 / 演员 / 合集（与单服务器收藏一致）。
    addCategoryTab(tr("Movies"), /*checked=*/true);
    addCategoryTab(tr("TV Shows"));
    addCategoryTab(tr("Episodes"));
    addCategoryTab(tr("People"));
    addCategoryTab(tr("Collections"));
}

void AggregatedFavoritesView::startLoad()
{
    if (!m_aggregator) {
        clearSections();
        return;
    }

    clearSections();
    createSkeletonSections();

    m_aggregator->getFavoritesAllServers(
        m_includeTypes, kPerServerFavoritesLimit,
        [this](const ServerProfile& profile, QList<MediaItem> items) {
            fillSection(profile, items);
        },
        [this](int /*attempted*/, int /*succeeded*/) {
            setAllSectionsLoaded();
        });
}

void AggregatedFavoritesView::onCategoryTabClicked(const QString& label)
{
    AggregatedViewBase::onCategoryTabClicked(label);
    m_includeTypes = includeTypesForTab(label);
    startLoad();
}

QString AggregatedFavoritesView::includeTypesForTab(const QString& label) const
{
    if (label == tr("TV Shows")) return QStringLiteral("Series");
    if (label == tr("Episodes")) return QStringLiteral("Episode");
    if (label == tr("People")) return QStringLiteral("Person");
    if (label == tr("Collections")) return QStringLiteral("BoxSet");
    return QStringLiteral("Movie");
}

void AggregatedFavoritesView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    startLoad();
}
