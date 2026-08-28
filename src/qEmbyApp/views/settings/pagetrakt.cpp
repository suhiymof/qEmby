#include "pagetrakt.h"

#include "../../components/elidedlabel.h"
#include "../../components/modernswitch.h"
#include "../../components/settingscard.h"
#include "../../utils/qcoroutil.h"
#include <config/config_keys.h>
#include <config/configstore.h>
#include <services/trakt/traktservice.h>

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <qcorotimer.h>
#include <stdexcept>

namespace {

QString statusLine(TraktService *service)
{
    if (!service->clientId().isEmpty() && !service->isLoggedIn()) {
        return QObject::tr("Client ID configured, but not signed in");
    }
    if (service->isLoggedIn()) {
        const QString user = service->userName();
        return user.isEmpty()
                   ? QObject::tr("Signed in")
                   : QObject::tr("Signed in as %1").arg(user);
    }
    return QObject::tr("Not signed in");
}

} // namespace

PageTrakt::PageTrakt(QEmbyCore *core, QWidget *parent)
    : SettingsPageBase(core, tr("Trakt"), parent)
{
    // ---- Account card: sign in / sign out -------------------------------
    m_accountBtn = new QPushButton(tr("Sign In"), this);
    m_accountBtn->setObjectName("SettingsCardButton");
    m_accountBtn->setCursor(Qt::PointingHandCursor);
    m_accountBtn->setFixedHeight(30);

    m_accountCard = new SettingsCard(":/svg/dark/user.svg", tr("Trakt Account"),
                                     statusLine(TraktService::instance()),
                                     m_accountBtn, QString(), this);
    m_mainLayout->addWidget(m_accountCard);

    // ---- Client ID ------------------------------------------------------
    m_clientIdEdit = new QLineEdit(this);
    m_clientIdEdit->setPlaceholderText(
        tr("Client ID from trakt.tv/oauth/applications"));
    m_clientIdEdit->setText(ConfigStore::instance()->get<QString>(
        ConfigKeys::TraktClientId, QString()));
    m_mainLayout->addWidget(new SettingsCard(
        ":/svg/dark/settings.svg", tr("Client ID"),
        tr("API credentials of your registered Trakt application"),
        m_clientIdEdit, ConfigKeys::TraktClientId, this));

    // ---- Feature switches ------------------------------------------------
    m_mainLayout->addWidget(new SettingsCard(
        ":/svg/dark/player.svg", tr("Real-time Scrobble"),
        tr("Report watch progress to Trakt while playing"),
        new ModernSwitch(this), ConfigKeys::TraktScrobbleEnabled, this,
        QVariant(false)));

    m_mainLayout->addWidget(new SettingsCard(
        ":/svg/dark/heart.svg", tr("Sync Watched Status"),
        tr("Show a Trakt sync button on detail pages"),
        new ModernSwitch(this), ConfigKeys::TraktSyncButtonEnabled, this,
        QVariant(false)));

    m_mainLayout->addWidget(new SettingsCard(
        ":/svg/dark/refresh.svg", tr("Resume Check"),
        tr("Check Trakt progress on playback and offer to resume from it"),
        new ModernSwitch(this), ConfigKeys::TraktResumeCheckEnabled, this,
        QVariant(false)));

    m_mainLayout->addStretch();

    connect(m_accountBtn, &QPushButton::clicked, this, [this]() {
        if (m_loginInProgress) {
            return;
        }
        if (TraktService::instance()->isLoggedIn()) {
            signOut();
        } else {
            launchTask(startDeviceLogin(), this);
        }
    });
}

void PageTrakt::refreshAccountUi()
{
    TraktService *service = TraktService::instance();
    const bool loggedIn = service->isLoggedIn();
    m_accountBtn->setText(loggedIn ? tr("Sign Out") : tr("Sign In"));
    const auto labels = m_accountCard->findChildren<ElidedLabel *>("SettingsCardDesc");
    for (ElidedLabel *label : labels) {
        label->setFullText(statusLine(service));
    }
}

void PageTrakt::updateStatusText(const QString &text)
{
    const auto labels = m_accountCard->findChildren<ElidedLabel *>("SettingsCardDesc");
    for (ElidedLabel *label : labels) {
        label->setFullText(text);
    }
}

QCoro::Task<void> PageTrakt::startDeviceLogin()
{
    TraktService *service = TraktService::instance();
    const QString clientId = m_clientIdEdit->text().trimmed();
    if (clientId.isEmpty()) {
        updateStatusText(tr("Enter your Client ID first"));
        co_return;
    }
    ConfigStore::instance()->set(ConfigKeys::TraktClientId, clientId);

    m_loginInProgress = true;
    m_accountBtn->setEnabled(false);
    updateStatusText(tr("Requesting device code..."));

    TraktService::DeviceCode code;
    try {
        code = co_await service->requestDeviceCode(clientId);
    } catch (const std::exception &e) {
        updateStatusText(tr("Login failed: %1").arg(QString::fromUtf8(e.what())));
        m_loginInProgress = false;
        m_accountBtn->setEnabled(true);
        co_return;
    }

    QDesktopServices::openUrl(QUrl(code.verificationUrl));
    updateStatusText(tr("Enter code %1 at %2")
                         .arg(code.userCode, code.verificationUrl));

    const int intervalMs = qMax(2, code.intervalSeconds) * 1000;
    const QDateTime deadline =
        QDateTime::currentDateTime().addSecs(qMax(60, code.expiresInSeconds));
    QTimer waitTimer;
    waitTimer.setSingleShot(true);

    while (QDateTime::currentDateTime() < deadline) {
        waitTimer.start(intervalMs);
        co_await waitTimer;

        TraktPollStatus status = TraktPollStatus::Failed;
        try {
            status = co_await service->pollDeviceToken(clientId, code.deviceCode);
        } catch (const std::exception &e) {
            qWarning().noquote() << "[Trakt] Device poll error:" << e.what();
        }
        if (status == TraktPollStatus::Approved) {
            refreshAccountUi();
            break;
        }
        if (status == TraktPollStatus::Expired) {
            updateStatusText(tr("Code expired, please try again"));
            break;
        }
        if (status == TraktPollStatus::Failed) {
            updateStatusText(tr("Login failed, please try again"));
            break;
        }
    }

    m_loginInProgress = false;
    m_accountBtn->setEnabled(true);
    refreshAccountUi();
}

void PageTrakt::signOut()
{
    TraktService::instance()->signOut();
    refreshAccountUi();
}
