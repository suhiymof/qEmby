#include "seriesdanmakumatchdialog.h"

#include "../managers/thememanager.h"
#include "loadingoverlay.h"
#include "moderntoast.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <config/configstore.h>
#include <services/danmaku/danmakuservice.h>
#include <qcorotimer.h>
#include <stdexcept>

namespace {

constexpr int kSeriesCandidateRole = Qt::UserRole + 1;

constexpr char kEpisodeOffsetKey[] = "danmaku/episode_offset";

QString offsetConfigKey(const QString &provider, const QString &seriesId)
{
    return QString::fromLatin1("%1/%2/%3")
        .arg(QLatin1String(kEpisodeOffsetKey), provider, seriesId);
}

QString providerDisplayName(const QString &provider)
{
    if (provider == QLatin1String("bilibili")) {
        return QCoreApplication::translate("SeriesDanmakuMatchDialog", "BiliBili");
    }
    if (provider == QLatin1String("dandanplay")) {
        return QLatin1String("dandanplay");
    }
    if (provider == QLatin1String("danmu_api")) {
        return QLatin1String("danmu_api");
    }
    return provider;
}

} // namespace

SeriesDanmakuMatchDialog::SeriesDanmakuMatchDialog(
    QEmbyCore *core, Mode mode, QList<DanmakuMatchCandidate> seriesCandidates,
    DanmakuMediaContext context, QString initialKeyword, QString activeTargetId,
    QString activeEndpointId, QWidget *parent)
    : PlayerOverlayDialog(parent),
      m_core(core),
      m_mode(mode),
      m_context(std::move(context)),
      m_initialKeyword(std::move(initialKeyword)),
      m_activeTargetId(std::move(activeTargetId)),
      m_activeEndpointId(std::move(activeEndpointId)),
      m_seriesResults(std::move(seriesCandidates))
{
    setSurfaceObjectName("seriesDanmakuMatchDialog");
    setSurfacePreferredSize(QSize(720, 520));
    setTitle(m_mode == Mode::Multi ? tr("Match Danmaku for Series")
                                   : tr("Search Danmaku"));

    // ---- Stage 1: series list -------------------------------------------
    m_promptLabel = new QLabel(tr("Choose the series to match danmaku for."), this);
    m_promptLabel->setObjectName("dialog-text");
    m_promptLabel->setWordWrap(true);
    contentLayout()->addWidget(m_promptLabel);
    contentLayout()->addSpacing(8);

    auto *searchRow = new QHBoxLayout();
    searchRow->setSpacing(10);
    m_seriesSearchEdit = new QLineEdit(this);
    m_seriesSearchEdit->setObjectName("PlaylistSearchEdit");
    m_seriesSearchEdit->setPlaceholderText(tr("Enter series title or keyword"));
    m_seriesSearchEdit->setClearButtonEnabled(true);
    m_seriesSearchEdit->addAction(
        ThemeManager::getAdaptiveIcon(QStringLiteral(":/svg/light/search.svg")),
        QLineEdit::LeadingPosition);
    QString defaultKeyword = m_initialKeyword.trimmed();
    if (defaultKeyword.isEmpty()) {
        defaultKeyword = m_context.isEpisode()
                             ? m_context.seriesName.trimmed()
                             : m_context.title.trimmed();
    }
    m_seriesSearchEdit->setText(defaultKeyword);
    searchRow->addWidget(m_seriesSearchEdit, 1);

    m_seriesSearchButton = new QPushButton(tr("Search"), this);
    m_seriesSearchButton->setObjectName("dialog-btn-primary");
    m_seriesSearchButton->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(m_seriesSearchButton);
    contentLayout()->addLayout(searchRow);
    contentLayout()->addSpacing(6);

    m_statusLabel = new QLabel(tr("Searching danmaku servers..."), this);
    m_statusLabel->setObjectName("dialog-text");
    m_statusLabel->setWordWrap(true);
    contentLayout()->addWidget(m_statusLabel);
    contentLayout()->addSpacing(8);

    m_seriesListContainer = new QWidget(this);
    auto *listLayout = new QVBoxLayout(m_seriesListContainer);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);

    m_seriesFilterEdit = new QLineEdit(m_seriesListContainer);
    m_seriesFilterEdit->setObjectName("PlaylistSearchEdit");
    m_seriesFilterEdit->setPlaceholderText(tr("Filter results"));
    m_seriesFilterEdit->setClearButtonEnabled(true);
    m_seriesFilterEdit->setEnabled(false);
    listLayout->addWidget(m_seriesFilterEdit);

    m_seriesList = new QListWidget(m_seriesListContainer);
    m_seriesList->setObjectName("ManageLibPathList");
    m_seriesList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_seriesList->setWordWrap(true);
    m_seriesList->setMinimumHeight(240);
    listLayout->addWidget(m_seriesList);

    m_loadingOverlay = new LoadingOverlay(m_seriesListContainer);
    m_loadingOverlay->setHudPanelVisible(false);
    m_loadingOverlay->setSubtleOverlay(true);

    contentLayout()->addWidget(m_seriesListContainer, 1);
    contentLayout()->addSpacing(12);

    // ---- Stage 2: episode picker (hidden initially) ---------------------
    m_episodePanel = new QWidget(this);
    auto *episodeLayout = new QVBoxLayout(m_episodePanel);
    episodeLayout->setContentsMargins(0, 0, 0, 0);
    episodeLayout->setSpacing(6);

    auto *episodeHeader = new QHBoxLayout();
    m_backButton = new QPushButton(QStringLiteral("\u2190 ") + tr("Back"), m_episodePanel);
    m_backButton->setObjectName("dialog-btn-cancel");
    m_backButton->setCursor(Qt::PointingHandCursor);
    episodeHeader->addWidget(m_backButton);

    m_episodeTitleLabel = new QLabel(m_episodePanel);
    m_episodeTitleLabel->setObjectName("dialog-title");
    episodeHeader->addWidget(m_episodeTitleLabel, 1);

    if (m_mode == Mode::Multi) {
        m_offsetEdit = new QLineEdit(m_episodePanel);
        m_offsetEdit->setObjectName("PlaylistSearchEdit");
        m_offsetEdit->setPlaceholderText(tr("Episode offset"));
        m_offsetEdit->setFixedWidth(110);
        m_offsetEdit->setToolTip(
            tr("Add N to every episode number when the danmaku source numbers "
               "differ from your library (e.g. source starts at 3)."));
        episodeHeader->addWidget(m_offsetEdit);
        m_selectAllButton = new QPushButton(tr("Select All"), m_episodePanel);
        m_selectAllButton->setObjectName("dialog-btn-primary");
        m_selectAllButton->setCursor(Qt::PointingHandCursor);
        episodeHeader->addWidget(m_selectAllButton);
    }
    episodeLayout->addLayout(episodeHeader);

    m_episodeList = new QListWidget(m_episodePanel);
    m_episodeList->setObjectName("ManageLibPathList");
    m_episodeList->setSelectionMode(m_mode == Mode::Multi
                                        ? QAbstractItemView::ExtendedSelection
                                        : QAbstractItemView::SingleSelection);
    m_episodeList->setWordWrap(true);
    m_episodeList->setMinimumHeight(240);
    episodeLayout->addWidget(m_episodeList, 1);

    if (m_mode == Mode::Multi) {
        m_selectionSummaryLabel = new QLabel(m_episodePanel);
        m_selectionSummaryLabel->setObjectName("dialog-text");
        episodeLayout->addWidget(m_selectionSummaryLabel);
    }

    auto *episodeButtons = new QHBoxLayout();
    episodeButtons->addStretch();
    auto *cancelButton = new QPushButton(tr("Cancel"), m_episodePanel);
    cancelButton->setObjectName("dialog-btn-cancel");
    cancelButton->setCursor(Qt::PointingHandCursor);
    connect(cancelButton, &QPushButton::clicked, this,
            &PlayerOverlayDialog::reject);
    episodeButtons->addWidget(cancelButton);
    m_confirmButton = new QPushButton(tr("Confirm"), m_episodePanel);
    m_confirmButton->setObjectName("dialog-btn-primary");
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setEnabled(false);
    connect(m_confirmButton, &QPushButton::clicked, this, [this]() {
        if (!m_selectedEpisodes.isEmpty()) {
            accept();
        }
    });
    episodeButtons->addWidget(m_confirmButton);
    episodeLayout->addLayout(episodeButtons);
    m_episodePanel->hide();

    contentLayout()->addWidget(m_episodePanel);

    // ---- wiring ---------------------------------------------------------
    connect(m_seriesSearchButton, &QPushButton::clicked, this,
            [this]() { triggerSeriesSearch(); });
    connect(m_seriesSearchEdit, &QLineEdit::returnPressed, this,
            [this]() { triggerSeriesSearch(); });
    connect(m_seriesFilterEdit, &QLineEdit::textChanged, this,
            [this](const QString &) { rebuildSeriesList(); });
    connect(m_seriesList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int row = m_seriesList->row(item);
        if (row >= 0 && row < m_seriesResults.size()) {
            enterEpisodePicker(row);
        }
    });
    connect(m_episodeList, &QListWidget::itemSelectionChanged, this,
            [this]() { updateSelectionSummary(); });
    if (m_selectAllButton) {
        connect(m_selectAllButton, &QPushButton::clicked, this, [this]() {
            m_episodeList->selectAll();
            updateSelectionSummary();
        });
    }
    if (m_offsetEdit) {
        connect(m_offsetEdit, &QLineEdit::textChanged, this,
                [this](const QString &) { applyEpisodeOffset(); });
    }
    connect(m_backButton, &QPushButton::clicked, this, [this]() {
        m_episodePanel->hide();
        m_seriesListContainer->show();
        m_promptLabel->show();
        m_seriesSearchEdit->setFocus();
    });

    m_episodeOffset = 0;
}

QList<DanmakuMatchCandidate> SeriesDanmakuMatchDialog::selectedEpisodes() const
{
    return m_selectedEpisodes;
}

void SeriesDanmakuMatchDialog::showEvent(QShowEvent *event)
{
    PlayerOverlayDialog::showEvent(event);
    updateLoadingOverlayGeometry();
    if (m_seriesSearchEdit && !m_loaded) {
        m_seriesSearchEdit->setFocus();
        m_seriesSearchEdit->selectAll();
    }
    if (!m_loaded) {
        m_loaded = true;
        triggerSeriesSearch();
    }
}

void SeriesDanmakuMatchDialog::resizeEvent(QResizeEvent *event)
{
    PlayerOverlayDialog::resizeEvent(event);
    updateLoadingOverlayGeometry();
}

QCoro::Task<void> SeriesDanmakuMatchDialog::searchSeries(QString queryText)
{
    QPointer<SeriesDanmakuMatchDialog> safeThis(this);
    QPointer<QEmbyCore> core(m_core);
    const DanmakuMediaContext context = m_context;
    queryText = queryText.trimmed();
    if (queryText.isEmpty()) {
        queryText = context.displayTitle().trimmed();
    }
    if (queryText.isEmpty()) {
        queryText = context.title.trimmed();
    }
    if (queryText.isEmpty() || !safeThis || !core || !core->danmakuService()) {
        co_return;
    }

    m_isLoading = true;
    updateStatusText(tr("Searching danmaku servers..."));
    updateUiState();

    try {
        const QList<DanmakuMatchCandidate> results =
            co_await core->danmakuService()->searchAllCandidates(context,
                                                                 queryText);
        if (!safeThis) {
            co_return;
        }
        safeThis->m_seriesResults = results;
        safeThis->m_isLoading = false;
        safeThis->rebuildSeriesList();
        safeThis->updateStatusText(
            results.isEmpty()
                ? tr("No danmaku sources found for \"%1\"").arg(queryText)
                : tr("%1 result(s) found").arg(results.size()));
        safeThis->updateUiState();
    } catch (const std::exception &e) {
        if (!safeThis) {
            co_return;
        }
        safeThis->m_seriesResults.clear();
        safeThis->m_isLoading = false;
        safeThis->rebuildSeriesList();
        safeThis->updateStatusText(tr("Search failed"));
        safeThis->updateUiState();
        qWarning().noquote()
            << "[SeriesDanmaku] search failed"
            << "| error:" << e.what();
        ModernToast::showMessage(
            tr("Failed to search danmaku: %1").arg(QString::fromUtf8(e.what())),
            3000);
    }
}

void SeriesDanmakuMatchDialog::triggerSeriesSearch()
{
    if (!m_seriesSearchEdit || m_isLoading) {
        return;
    }
    m_pendingTask = searchSeries(m_seriesSearchEdit->text());
}

void SeriesDanmakuMatchDialog::rebuildSeriesList()
{
    if (!m_seriesList) {
        return;
    }
    m_seriesList->clear();
    const QString filter = m_seriesFilterEdit->text().trimmed();
    for (int i = 0; i < m_seriesResults.size(); ++i) {
        const DanmakuMatchCandidate &candidate = m_seriesResults.at(i);
        const QString displayText = candidate.displayText();
        if (!filter.isEmpty() &&
            !displayText.contains(filter, Qt::CaseInsensitive) &&
            !candidate.provider.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        QString text = displayText;
        if (candidate.isSeries()) {
            text = QStringLiteral("%1 (%2)")
                       .arg(displayText)
                       .arg(candidate.episodes.size());
        }
        auto *item = new QListWidgetItem(text, m_seriesList);
        item->setData(kSeriesCandidateRole, i);
        item->setToolTip(candidate.isSeries()
                             ? tr("Click to choose episodes")
                             : tr("Click to load danmaku"));
    }
}

void SeriesDanmakuMatchDialog::enterEpisodePicker(int row)
{
    if (row < 0 || row >= m_seriesResults.size()) {
        return;
    }
    m_currentSeries = m_seriesResults.at(row);

    // Episode-level candidate (not a series): nothing to pick — accept it
    // immediately with a single entry.
    if (!m_currentSeries.isSeries()) {
        m_selectedEpisodes = {m_currentSeries};
        accept();
        return;
    }

    // Restore the saved per-series offset (multi mode).
    m_episodeOffset = 0;
    if (m_mode == Mode::Multi && m_offsetEdit) {
        m_episodeOffset = ConfigStore::instance()
                              ->get<int>(offsetConfigKey(m_currentSeries.provider,
                                                         m_currentSeries.targetId),
                                          0);
        m_offsetEdit->setText(QString::number(m_episodeOffset));
    }

    m_selectedEpisodes.clear();
    m_episodeTitleLabel->setText(m_currentSeries.displayText());
    m_episodeList->clear();
    for (const DanmakuEpisode &ep : m_currentSeries.episodes) {
        const QString label =
            QStringLiteral("%1. %2")
                .arg(ep.episodeNumber > 0 ? ep.episodeNumber : 0, 2, 10, QChar('0'))
                .arg(ep.longTitle.isEmpty() ? ep.title : ep.longTitle);
        auto *item = new QListWidgetItem(label, m_episodeList);
        item->setData(kSeriesCandidateRole, QVariant());
    }
    m_seriesListContainer->hide();
    m_promptLabel->hide();
    m_episodePanel->show();
    updateSelectionSummary();
    updateUiState();
}

void SeriesDanmakuMatchDialog::rebuildEpisodeList()
{
    if (!m_episodeList) {
        return;
    }
    m_episodeList->clear();
    for (const DanmakuEpisode &ep : m_currentSeries.episodes) {
        const int adjusted = ep.episodeNumber + m_episodeOffset;
        const QString label =
            QStringLiteral("%1. %2")
                .arg(adjusted > 0 ? adjusted : 0, 2, 10, QChar('0'))
                .arg(ep.longTitle.isEmpty() ? ep.title : ep.longTitle);
        auto *item = new QListWidgetItem(label, m_episodeList);
        item->setData(kSeriesCandidateRole, QVariant());
    }
}

void SeriesDanmakuMatchDialog::applyEpisodeOffset()
{
    if (!m_offsetEdit) {
        return;
    }
    bool ok = false;
    const int offset = m_offsetEdit->text().trimmed().toInt(&ok);
    m_episodeOffset = ok ? offset : 0;
    ConfigStore::instance()->set(offsetConfigKey(m_currentSeries.provider,
                                                 m_currentSeries.targetId),
                                 m_episodeOffset);
    rebuildEpisodeList();
    updateSelectionSummary();
}

void SeriesDanmakuMatchDialog::updateSelectionSummary()
{
    if (m_mode != Mode::Multi) {
        const bool hasSelection =
            m_episodeList && m_episodeList->currentItem() != nullptr;
        if (m_confirmButton) {
            m_confirmButton->setEnabled(hasSelection);
        }
        if (m_selectionSummaryLabel) {
            m_selectionSummaryLabel->setText(
                hasSelection ? tr("1 episode selected")
                             : tr("Select an episode"));
        }
        return;
    }

    QList<int> selectedIndexes;
    for (int i = 0; i < m_episodeList->count(); ++i) {
        if (m_episodeList->item(i)->isSelected()) {
            selectedIndexes.append(i);
        }
    }
    m_selectedEpisodes.clear();
    for (const int idx : selectedIndexes) {
        if (idx < 0 || idx >= m_currentSeries.episodes.size()) {
            continue;
        }
        const DanmakuEpisode &ep = m_currentSeries.episodes.at(idx);
        DanmakuMatchCandidate episodeCandidate = m_currentSeries;
        episodeCandidate.episodes.clear();
        episodeCandidate.targetId = ep.cid;
        episodeCandidate.episodeNumber = ep.episodeNumber + m_episodeOffset;
        episodeCandidate.seasonNumber = -1;
        episodeCandidate.durationMs = ep.durationMs;
        episodeCandidate.title = ep.longTitle.isEmpty() ? ep.title : ep.longTitle;
        episodeCandidate.subtitle = m_currentSeries.title;
        m_selectedEpisodes.append(episodeCandidate);
    }
    if (m_selectionSummaryLabel) {
        m_selectionSummaryLabel->setText(
            tr("%1 / %2 episodes selected")
                .arg(m_selectedEpisodes.size())
                .arg(m_currentSeries.episodes.size()));
    }
    if (m_confirmButton) {
        m_confirmButton->setEnabled(!m_selectedEpisodes.isEmpty());
    }
}

void SeriesDanmakuMatchDialog::updateUiState()
{
    const bool searching = m_isLoading;
    if (m_seriesSearchButton) {
        m_seriesSearchButton->setEnabled(!searching);
    }
    if (m_seriesFilterEdit) {
        m_seriesFilterEdit->setEnabled(!searching);
    }
    if (m_loadingOverlay) {
        m_loadingOverlay->setVisible(searching);
    }
}

void SeriesDanmakuMatchDialog::updateLoadingOverlayGeometry()
{
    if (m_loadingOverlay && m_seriesListContainer) {
        m_loadingOverlay->setGeometry(m_seriesListContainer->rect());
        m_loadingOverlay->raise();
    }
}

void SeriesDanmakuMatchDialog::updateStatusText(const QString &text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}
