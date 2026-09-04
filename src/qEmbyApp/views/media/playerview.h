#ifndef PLAYERVIEW_H
#define PLAYERVIEW_H

#include "../baseview.h"
#include "../../components/mpvwidget.h"
#include "../../components/modernslider.h"
#include "../../components/loadingoverlay.h" 
#include "../../components/playerdanmakucontroller.h"
#include <models/media/mediaitem.h>
#include <models/media/playbackinfo.h>
#include <services/introdb/introdbservice.h>
#include <services/trakt/traktservice.h>

#include <QVariant>
#include <qcorotask.h>

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QRect>
#include <QPoint>
#include <QMenu>
#include <QKeyEvent>
#include <QProgressBar>
#include <QHash>
#include <QPointer>
#include <QSet>

class QEmbyCore;
class PlayerOverlayDialog;
class PlayerMediaSwitcherPanel;
class PlayerOsdLayer;
class PlayerLongPressHandler;
class PlayerStatisticsOverlay;
class NativeDanmakuOverlay;

class PlayerView : public BaseView {
    Q_OBJECT
public:
    explicit PlayerView(QEmbyCore *core, QWidget *parent = nullptr);
    ~PlayerView() override;
    void prepareForStackLeave() override;

    
    void playMedia(const QString &mediaId, const QString &title, const QString &streamUrl, long long startPositionTicks = 0, const QVariant& sourceInfoVar = QVariant(), bool allowSourceFetch = true);

    
    bool isMediaPlaying() const;
    void pausePlayback();
    void resumePlayback();
    void restoreAfterWindowShow(bool shouldResumePlaying);
    void applyDecodeDecision(const MediaSourceInfo &source);
    void stopAndReport(); 

signals:
    void playerChromeVisibilityChanged(bool visible);
    void playbackTitleChanged(const QString &title);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void onPositionChanged(double position);
    void onDurationChanged(double duration);
    void onPlaybackStateChanged(bool isPaused);
    void onMpvPropertyChanged(const QString &property, const QVariant &value);
    
    void togglePlayPause();
    void onSliderMoved(int value);
    
    
    void seekRelative(double delta, bool silent = false);
    
    
    void toggleMute();
    void changeVolume(int delta, bool silent = false);
    void onVolumeSliderMoved(int value);

    void showSpeedMenu();
    void showAudioMenu();
    void showSubtitleMenu();
    void showDanmakuMenu();
    void showDanmakuIdentifyDialog();
    void loadLocalDanmakuFile();
    void loadExternalSubtitleFile();
    void openSubtitleSettingsDialog();
    void openDanmakuSettingsDialog();
    void showSettingsMenu(); 
    
    void cycleVideoScale();
    void toggleFullscreenWindow();

    void showControls();
    void hideControls();
    void reportProgressToServer();
    void onBackClicked();

    // Trakt scrobble / resume-check helpers. All fire-and-forget and silent:
    // failures must never disturb playback.
    bool traktScrobbleActive() const;
    QCoro::Task<TraktMediaIds> traktEnsureIdsResolved();
    QCoro::Task<void> traktScrobbleAt(QString action);
    void traktOnProgressTick();
    void traktOnPauseStateChanged(bool isPaused);
    void traktOnPlaybackStopped();
    QCoro::Task<void> traktCheckResumeProgress();


    
    
    void updateLoadingState();
    void updateDanmakuButtonState();

private:
    void setupUi();
    void updateTitleElision();
    void updateOverlayLayout();
    void clearMediaSwitcherCache();
    void applyMediaSwitcherMode();
    void updateMediaSwitcherButton();
    bool shouldShowDanmakuHudControls() const;
    bool useHudMediaSwitcher() const;
    QString formatMediaSwitcherPlaybackTitle(const MediaItem &item) const;
    bool findNextResumeMediaFromCache(QString &mediaId,
                                      QString &title,
                                      long long &startPositionTicks,
                                      bool skipCurrentSeries) const;
    void populateRightSidebarFromCache();
    void showHudMediaSwitcher();
    void hideHudMediaSwitcher();
    void syncHudMediaSwitcherContent();
    bool findAdjacentMediaFromCache(int direction, QString &mediaId,
                                    QString &title,
                                    long long &startPositionTicks) const;
    
    
    void setupRightSidebar();
    
    
    QCoro::Task<void> showRightSidebar();
    QCoro::Task<void> ensureMediaSwitcherDataLoaded();
    QCoro::Task<void> autoPlayNextMediaIfEnabled();
    QCoro::Task<void> switchFromMediaSwitcher(QString mediaId,
                                              QString title,
                                              long long startPositionTicks);
    QCoro::Task<void> ensureMediaSourcesThenPlay(QString mediaId,
                                                 QString title,
                                                 QString streamUrl,
                                                 long long startPositionTicks,
                                                 MediaSourceInfo currentSource,
                                                 QString serverId = QString());
    // Continuous-play optimization: negotiate the next episode's playback
    // source in the background while the current one plays, so the
    // auto-advance starts without waiting for another PlaybackInfo round trip.
    QCoro::Task<void> prefetchNextEpisodeSource();

    void hideRightSidebar(bool immediate = false);
    void setEffectivePlaybackSpeed(double speed);
    void handlePointerActivity(const QPoint &globalPos);
    void setCursorHidden(bool hidden);
    bool areControlsFullyVisible() const;
    void setPlayerChromeVisible(bool visible);

    void stopTransientUiAnimations(bool immediate = false);
    void beginViewTeardown();

    QPushButton* createHudButton(const QString& iconPath, const QSize& size = QSize(24, 24));
    QString formatTime(double seconds, double totalSeconds) const;
    void applySubtitleStyleSettings();
    void resumePlaybackAfterFinishedSeek();

    
    QString externalSubtitleConfigKey() const;
    QString readPersistedExternalSubtitle() const;
    void persistExternalSubtitle(const QString &absPath);
    void clearPersistedExternalSubtitle();
    void applyPersistedExternalSubtitleIfAny();
    QString openPlayerFileDialog(const QString &title, const QString &startDir,
                                 const QString &filter);
    
    int findSubtitleTrackIdByPath(const QString &absPath) const;
    
    void showToast(const QString& msg);
    void updateStatisticsDisplay();
    void setScaleIcon(); 
    QString formatDanmakuProviderLabel(QString provider) const;
    QString formatDanmakuSourceServiceLabel(QString provider,
                                            QString serverName) const;
    QString buildDanmakuSummaryText() const;
    QString buildDanmakuTooltipText() const;
    void closeActivePlayerDialog();
    void trackPlayerDialog(PlayerOverlayDialog *dialog);
    void updatePowerInhibition();
    QCoro::Task<void> requestIntroDBSegments();
    void checkAndSkipSegment(double position);

    void showCenteredPopup(QWidget* popup, QPushButton* btn); 
    QWidget* m_activePopup = nullptr; 
    QPointer<PlayerOverlayDialog> m_activePlayerDialog;

    
    static QCoro::Task<void> executeFetchLogo(QPointer<PlayerView> safeThis, QEmbyCore* core, QString mediaId, QString serverId = QString());
    static QCoro::Task<void> resolveDanmakuPlaybackContext(
        QPointer<PlayerView> safeThis,
        QPointer<QEmbyCore> core,
        QString mediaId,
        QString fallbackTitle,
        MediaSourceInfo sourceInfo,
        QString serverId = QString());

    MpvWidget *m_mpvWidget;
    NativeDanmakuOverlay *m_nativeDanmakuOverlay = nullptr;

    
    QWidget *m_topHUD;
    QWidget *m_bottomHUD;
    PlayerStatisticsOverlay *m_statisticsOverlay;
    LoadingOverlay *m_loadingOverlay; 

    
    QLabel *m_logoLabel;
    QGraphicsOpacityEffect *m_logoOpacity;

    
    QGraphicsOpacityEffect *m_speedOpacity;

    
    PlayerOsdLayer *m_osdLayer = nullptr;
    
    PlayerLongPressHandler *m_longPressHandler = nullptr;

    
    QWidget *m_rightSidebar;
    QWidget *m_rightTrigger;
    QPropertyAnimation *m_rightSidebarAnim;
    
    
    QLabel *m_sidebarTitleLabel;
    class QListWidget *m_resumeList;

    QPushButton *m_backBtn;
    QLabel *m_titleLabel;
    QPushButton *m_minBtn;
    QPushButton *m_maxBtn;
    QPushButton *m_closeBtn;
    QLabel *m_networkSpeedLabel;

    
    QLabel *m_currentTimeLabel;
    ModernSlider *m_progressSlider;
    QLabel *m_totalTimeLabel;

    
    QPushButton *m_prevMediaBtn;
    QPushButton *m_playPauseBtn;
    QPushButton *m_rewindBtn;
    QPushButton *m_forwardBtn;
    QPushButton *m_nextMediaBtn;
    
    
    QPushButton *m_volumeBtn;
    ModernSlider *m_volumeSlider;

    QLabel *m_toastLabel;
    QTimer *m_toastTimer;

    QPushButton *m_speedBtn = nullptr;
    QPushButton *m_mediaSwitchBtn = nullptr;
    QPushButton *m_audioBtn = nullptr;
    QPushButton *m_subtitleBtn = nullptr;
    QPushButton *m_danmakuBtn = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    QPushButton *m_scaleBtn = nullptr;      
    QPushButton *m_fullscreenBtn = nullptr; 
    PlayerMediaSwitcherPanel *m_mediaSwitchDrawer = nullptr;
    int m_bottomHudBaseHeight = 110;

    PlayerDanmakuController *m_danmakuController = nullptr;

    QGraphicsOpacityEffect *m_topOpacity;
    QGraphicsOpacityEffect *m_bottomOpacity;
    QParallelAnimationGroup *m_fadeGroup;
    QTimer *m_hideTimer; 
    QTimer *m_reportTimer;
    QTimer *m_mousePollTimer;
    
    
    QTimer *m_bufferTimer;

    QString m_currentMediaId;
    QString m_currentMediaSourceId; 
    QString m_currentPlaySessionId; 
    MediaItem m_currentMediaItem;
    MediaSourceInfo m_currentMediaSourceInfo;
    
    
    QString m_originalStreamUrl; 
    QVariant m_currentSourceInfoVar; 

    bool m_isPlaying;
    bool m_isBuffering = false; 
    bool m_isSeeking = false;   
    bool m_playerChromeVisible = true;
    
    double m_currentPosition;
    double m_totalDuration = 0.0; 
    double m_pendingSeekSeconds = 0.0;
    bool m_windowRestorePending = false;
    bool m_windowRestoreShouldPlay = false;
    double m_osdSeekPreviewPosition = -1.0;
    
    double m_currentSpeed = 1.0;
    int m_videoScaleMode = 1;
    
    
    double m_currentVolume = 100.0;
    bool m_isMuted = false;

    bool m_showNetworkSpeed = true; 
    bool m_useRelayNetworkSpeed = false;
    bool m_showStatisticsOverlay = false; 
    qint64 m_effectiveNetworkSpeed = 0;
    bool m_hasSetVideoSize = false; 
    bool m_hasReportedStop = false; 

    // Trakt scrobble state (per playback session; reset in playMedia)
    TraktMediaIds m_traktIds;
    QString m_traktResolvedMediaId;
    bool m_traktResolveInFlight = false;
    bool m_traktResumeChecked = false;
    bool m_traktStopped = false;
    qint64 m_traktLastScrobbleMs = 0;
    bool m_isPlaybackFinished = false;
    bool m_autoPlayAdvanceInProgress = false;
    bool m_isViewTearingDown = false;
    bool m_powerInhibitionHeld = false;
    bool m_isRightSidebarVisible = false; 
    
    
    QString m_switcherPendingItemId;
    QString m_switcherPendingTitle;
    long long m_switcherPendingTicks = 0;
    
    
    bool m_isSeriesMode = false;
    QString m_seriesId;
    QString m_seriesName;

    
    IntroDBService::EpisodeSegments m_episodeSegments;
    bool m_introSkipped = false;
    bool m_outroSkipped = false;
    bool m_segmentsRequested = false;

    
    QString m_switcherCacheMediaId;
    bool m_switcherCacheReady = false;
    QList<MediaItem> m_switcherResumeItems;
    QList<MediaItem> m_switcherSeriesSeasons;
    QHash<QString, QList<MediaItem>> m_switcherSeasonEpisodes;

    // Playback sources negotiated ahead of time for the upcoming episode
    // (continuous play); keyed by itemId. Bounded: cleared when it grows
    // beyond a few entries.
    QHash<QString, MediaSourceInfo> m_prefetchedSources;

    // Guards the progress-threshold prefetch trigger (one shot per media).
    bool m_prefetchTriggered = false;
    int m_prefetchThreshold = 90;

    // Pure-DV recheck after playback starts: list-style items arrive without
    // MediaStreams, so the initial hwdec decision has no data. Rechecked once
    // per media per playMedia invocation via item detail (reset on each
    // playMedia so switching back retries; reload path does not re-enter
    // playMedia, preventing loops).
    QSet<QString> m_dvRecheckPending;
    // Sticky software-decode decision for the current media. Sources without
    // MediaStreams (list/resume paths) can't be re-judged on later reloads
    // (window restore); without this sticky flag they would reset hwdec back
    // to hardware decoding and DV profile 5 would render green again.
    bool m_swDecodeForCurrentMedia = false;

    QString m_fullTitle;
    
    QRect m_originalGeometry;
    bool m_wasMaximized = false;
    QPoint m_dragPos;
    QPoint m_lastMousePos; 
    bool m_didDrag = false; 
    QTimer *m_singleClickTimer = nullptr; 

    
    int m_targetAudioStreamIndex = -2;
    int m_targetSubStreamIndex = -2;
};

#endif 
