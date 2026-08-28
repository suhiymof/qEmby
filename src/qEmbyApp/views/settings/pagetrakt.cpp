#include "pagetrakt.h"

#include "../../components/elidedlabel.h"
#include "../../components/modernswitch.h"
#include "../../components/settingscard.h"
#include "../../utils/qcoroutil.h"
#include "traktlogindialog.h"
#include <config/config_keys.h>
#include <config/configstore.h>
#include <services/trakt/traktservice.h>

#include <QDebug>
#include <QFrame>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <stdexcept>

namespace {

QString statusLine(TraktService *service)
{
    if (!service->clientId().isEmpty() && !service->clientSecret().isEmpty()
        && !service->isLoggedIn()) {
        return QObject::tr("API credentials configured, but not signed in");
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

    // ---- API credentials -------------------------------------------------
    m_clientIdEdit = new QLineEdit(this);
    m_clientIdEdit->setPlaceholderText(
        tr("Client ID from trakt.tv/oauth/applications"));
    m_clientIdEdit->setText(ConfigStore::instance()->get<QString>(
        ConfigKeys::TraktClientId, QString()));
    m_mainLayout->addWidget(new SettingsCard(
        ":/svg/dark/settings.svg", tr("Client ID"),
        tr("Redirect URI of your Trakt application must be set to %1")
            .arg(TraktService::redirectUri()),
        m_clientIdEdit, ConfigKeys::TraktClientId, this));

    m_secretEdit = new QLineEdit(this);
    m_secretEdit->setEchoMode(QLineEdit::Password);
    m_secretEdit->setPlaceholderText(
        tr("Client Secret from trakt.tv/oauth/applications"));
    m_secretEdit->setText(ConfigStore::instance()->get<QString>(
        ConfigKeys::TraktClientSecret, QString()));
    m_mainLayout->addWidget(new SettingsCard(
        ":/svg/dark/settings.svg", tr("Client Secret"),
        tr("API credentials of your registered Trakt application"),
        m_secretEdit, ConfigKeys::TraktClientSecret, this));

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
            launchTask(startBrowserLogin(), this);
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

QCoro::Task<void> PageTrakt::startBrowserLogin()
{
    TraktService *service = TraktService::instance();
    const QString clientId = m_clientIdEdit->text().trimmed();
    const QString clientSecret = m_secretEdit->text().trimmed();
    if (clientId.isEmpty() || clientSecret.isEmpty()) {
        updateStatusText(tr("Enter Client ID and Client Secret first"));
        co_return;
    }
    auto *store = ConfigStore::instance();
    store->set(ConfigKeys::TraktClientId, clientId);
    store->set(ConfigKeys::TraktClientSecret, clientSecret);

    m_loginInProgress = true;
    m_accountBtn->setEnabled(false);
    updateStatusText(tr("Complete the authorization in the opened window..."));

    QPointer<PageTrakt> guard(this);
    TraktLoginDialog dialog(clientId, this);
    dialog.exec();

    // The dialog runs a nested event loop; the settings page could be gone
    // by the time it returns (app teardown). Bail out before touching members.
    if (!guard) {
        co_return;
    }

    if (dialog.authCode().isEmpty()) {
        m_loginInProgress = false;
        m_accountBtn->setEnabled(true);
        if (!dialog.errorText().isEmpty()) {
            updateStatusText(tr("Authorization failed: %1")
                                 .arg(dialog.errorText()));
        } else {
            // Dialog closed without a callback: user cancelled.
            refreshAccountUi();
        }
        co_return;
    }

    updateStatusText(tr("Exchanging authorization code..."));
    bool ok = false;
    try {
        ok = co_await service->exchangeAuthorizationCode(dialog.authCode());
    } catch (const std::exception &e) {
        updateStatusText(tr("Login failed: %1").arg(QString::fromUtf8(e.what())));
        m_loginInProgress = false;
        m_accountBtn->setEnabled(true);
        co_return;
    }

    m_loginInProgress = false;
    m_accountBtn->setEnabled(true);
    if (!ok) {
        updateStatusText(
            tr("Login failed, please check Client ID and Client Secret"));
    }
    refreshAccountUi();
}

void PageTrakt::signOut()
{
    TraktService::instance()->signOut();
    // Fully sign out: also drop the Trakt session cookies kept by the
    // embedded browser, so the next sign-in asks for credentials again.
    QWebEngineProfile::defaultProfile()->cookieStore()->deleteAllCookies();
    refreshAccountUi();
}
