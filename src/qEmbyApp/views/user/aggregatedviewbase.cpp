#include "aggregatedviewbase.h"
#include "../../components/horizontallistviewgallery.h"
#include "../../utils/smoothscrollcontroller.h"
#include <qembycore.h>
#include <services/aggregator/searchaggregator.h>
#include <services/manager/servermanager.h>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QFrame>
#include <utility>

// ============================ AggregatedServerSection ============================

AggregatedServerSection::AggregatedServerSection(QEmbyCore* core,
                                                 const ServerProfile& profile,
                                                 QWidget* parent)
    : QWidget(parent)
    , m_core(core)
    , m_profile(profile)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(4);

    // —— header 行：◆ 服名 (N 项) [加载中...]  › ——
    // 用 QPushButton 承载（可点击 emit sectionClicked，样式 QSS 控制）。
    auto* header = new QPushButton(this);
    header->setObjectName(QStringLiteral("aggregate-server-header"));
    header->setCursor(Qt::PointingHandCursor);
    header->setFlat(true);

    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 8, 4);
    headerLayout->setSpacing(6);

    m_headerLabel = new QLabel(header);
    m_headerLabel->setObjectName(QStringLiteral("aggregate-server-name"));
    const QString name = profile.name.isEmpty() ? profile.url : profile.name;
    m_headerLabel->setText(QStringLiteral("\u25C6 %1").arg(name)); // ◆
    headerLayout->addWidget(m_headerLabel);

    m_countLabel = new QLabel(header);
    m_countLabel->setObjectName(QStringLiteral("aggregate-server-count"));
    m_countLabel->setText(QString());
    headerLayout->addWidget(m_countLabel);

    m_loadingLabel = new QLabel(header);
    m_loadingLabel->setObjectName(QStringLiteral("aggregate-server-loading"));
    m_loadingLabel->setText(QStringLiteral("\u23F3 %1").arg(tr("Loading...")));
    headerLayout->addWidget(m_loadingLabel);

    headerLayout->addStretch(1);
    auto* chevron = new QLabel(QStringLiteral("\u203A"), header); // ›
    chevron->setObjectName(QStringLiteral("aggregate-server-chevron"));
    headerLayout->addWidget(chevron);

    layout->addWidget(header);

    connect(header, &QPushButton::clicked, this,
            [this]() { Q_EMIT sectionClicked(m_profile); });

    // —— 横向卡片 gallery ——
    m_gallery = new HorizontalListViewGallery(core, this);
    m_gallery->setObjectName(QStringLiteral("aggregate-server-gallery"));
    // 转发 gallery 信号到上层（由 AggregatedViewBase 统一接 BaseView 槽）。
    connect(m_gallery, &HorizontalListViewGallery::itemClicked, this,
            [this](const MediaItem& item) { Q_EMIT itemActivated(item); });
    connect(m_gallery, &HorizontalListViewGallery::playRequested, this,
            &AggregatedServerSection::playRequested);
    connect(m_gallery, &HorizontalListViewGallery::favoriteRequested, this,
            &AggregatedServerSection::favoriteRequested);
    connect(m_gallery, &HorizontalListViewGallery::moreMenuRequested, this,
            &AggregatedServerSection::moreMenuRequested);
    layout->addWidget(m_gallery);
}

void AggregatedServerSection::setItems(const QList<MediaItem>& items)
{
    m_gallery->setItems(items);
    m_countLabel->setText(tr("(%1)").arg(items.size()));
    setLoading(false);
}

void AggregatedServerSection::setLoading(bool loading)
{
    if (m_loadingLabel) {
        m_loadingLabel->setVisible(loading);
    }
    if (m_countLabel) {
        m_countLabel->setVisible(!loading);
    }
}

// ============================ AggregatedViewBase ============================

AggregatedViewBase::AggregatedViewBase(QEmbyCore* core, QWidget* parent)
    : BaseView(core, parent)
    , m_core(core)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // —— 顶部类别 tab 区（子类 addCategoryTab 填充）——
    m_tabBar = new QWidget(this);
    m_tabBar->setObjectName(QStringLiteral("aggregate-tab-bar"));
    m_tabLayout = new QHBoxLayout(m_tabBar);
    m_tabLayout->setContentsMargins(24, 12, 24, 0);
    m_tabLayout->setSpacing(24);
    m_tabLayout->addStretch(1);
    rootLayout->addWidget(m_tabBar);

    // —— 滚动区：按服务器分组的 section 列表 ——
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("aggregate-scroll"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget(m_scrollArea);
    container->setObjectName(QStringLiteral("aggregate-container"));
    m_sectionsLayout = new QVBoxLayout(container);
    m_sectionsLayout->setContentsMargins(24, 12, 24, 24);
    m_sectionsLayout->setSpacing(8);
    m_sectionsLayout->addStretch(1);
    m_scrollArea->setWidget(container);
    rootLayout->addWidget(m_scrollArea, 1);

    m_vScrollController =
        new SmoothScrollController(m_scrollArea->verticalScrollBar(), this);
    m_vScrollController->setDuration(160);

    // —— SearchAggregator：跨服务器 fan-out ——
    if (m_core && m_core->serverManager()) {
        m_aggregator = new SearchAggregator(
            m_core->serverManager(),
            m_core->serverManager()->network(), this);
    }
}

AggregatedViewBase::~AggregatedViewBase() = default;

void AggregatedViewBase::createSkeletonSections()
{
    clearSections();
    if (!m_core || !m_core->serverManager()) return;

    const QList<ServerProfile> servers = m_core->serverManager()->servers();
    for (const ServerProfile& profile : servers) {
        if (!profile.isValid()) continue;
        auto* section = new AggregatedServerSection(m_core, profile, this);
        section->setLoading(true);
        connect(section, &AggregatedServerSection::sectionClicked, this,
                [this](const ServerProfile& p) {
                    // 阶段5 实现 Server Scoped 跳转；当前占位 emit。
                    Q_EMIT serverScopedRequested(p);
                });
        // 卡片交互 → BaseView 统一处理（播放/收藏/详情/更多菜单）。
        connect(section, &AggregatedServerSection::itemActivated, this,
                [this](const MediaItem& item) {
                    Q_EMIT navigateToDetail(item.id, item.name, item);
                });
        connect(section, &AggregatedServerSection::playRequested, this,
                &BaseView::handlePlayRequested);
        connect(section, &AggregatedServerSection::favoriteRequested, this,
                &BaseView::handleFavoriteRequested);
        connect(section, &AggregatedServerSection::moreMenuRequested, this,
                &BaseView::handleMoreMenuRequested);
        m_sectionsLayout->insertWidget(m_sectionsLayout->count() - 1, section);
        m_sections.append(section);
    }
}

void AggregatedViewBase::fillSection(const ServerProfile& profile,
                                     const QList<MediaItem>& items)
{
    for (AggregatedServerSection* section : std::as_const(m_sections)) {
        if (section->serverId() == profile.id) {
            section->setItems(items);
            return;
        }
    }
}

void AggregatedViewBase::setAllSectionsLoaded()
{
    for (AggregatedServerSection* section : std::as_const(m_sections)) {
        section->setLoading(false);
    }
}

void AggregatedViewBase::clearSections()
{
    for (AggregatedServerSection* section : std::as_const(m_sections)) {
        m_sectionsLayout->removeWidget(section);
        section->deleteLater();
    }
    m_sections.clear();
}

QPushButton* AggregatedViewBase::addCategoryTab(const QString& label, bool checked)
{
    auto* tab = new QPushButton(label, m_tabBar);
    tab->setObjectName(QStringLiteral("aggregate-category-tab"));
    tab->setCheckable(true);
    tab->setChecked(checked);
    tab->setCursor(Qt::PointingHandCursor);
    connect(tab, &QPushButton::clicked, this,
            [this, label]() { onCategoryTabClicked(label); });
    m_tabLayout->insertWidget(m_tabLayout->count() - 1, tab);
    m_categoryTabs.append(tab);
    if (checked) m_currentTabLabel = label;
    return tab;
}

void AggregatedViewBase::onCategoryTabClicked(const QString& label)
{
    m_currentTabLabel = label;
    for (QPushButton* tab : std::as_const(m_categoryTabs)) {
        tab->setChecked(tab->text() == label);
    }
    // 子类重写并调用 startLoad()。
}
