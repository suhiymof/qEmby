#include "aggregatedhistoryview.h"
#include <services/aggregator/searchaggregator.h>
#include <services/manager/servermanager.h>

namespace {
// 聚合视图每 server 预览条数（防图片请求风暴，见 aggregatedsearchview.cpp 注释）。
constexpr int kPerServerHistoryLimit = 12;
}

AggregatedHistoryView::AggregatedHistoryView(QEmbyCore* core, QWidget* parent)
    : AggregatedViewBase(core, parent)
{
}

void AggregatedHistoryView::startLoad()
{
    if (!m_aggregator) {
        clearSections();
        return;
    }

    clearSections();
    createSkeletonSections();

    m_aggregator->getResumeItemsAllServers(
        kPerServerHistoryLimit,
        [this](const ServerProfile& profile, QList<MediaItem> items) {
            fillSection(profile, items);
        },
        [this](int /*attempted*/, int /*succeeded*/) {
            setAllSectionsLoaded();
        });
}

void AggregatedHistoryView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // 每次进入视图刷新一次（历史进度随时在变）。
    startLoad();
}

QString AggregatedHistoryView::scopedPageTitle(const ServerProfile& profile) const
{
    Q_UNUSED(profile);
    return tr("继续观看");
}
