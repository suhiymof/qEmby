#include "traktlogindialog.h"

#include <services/trakt/traktservice.h>

#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>

// Custom page that intercepts the redirect to the fictional callback host.
class TraktLoginDialog::AuthPage : public QWebEnginePage
{
public:
    std::function<void(const QUrl &)> callbackHandler;

    explicit AuthPage(QObject *parent = nullptr)
        : QWebEnginePage(parent)
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type,
                                 bool isMainFrame) override
    {
        const QString callbackHost = QUrl(TraktService::redirectUri()).host();
        if (!callbackHost.isEmpty() && url.host() == callbackHost) {
            if (callbackHandler) {
                callbackHandler(url);
            }
            return false; // never actually navigate to the fake redirect
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
};

TraktLoginDialog::TraktLoginDialog(const QString &clientId, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Trakt Authorization"));
    resize(520, 720);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QWebEngineView(this);
    layout->addWidget(m_view);

    auto *page = new AuthPage(m_view);
    page->callbackHandler =
        [this](const QUrl &url) { handleCallbackUrl(url); };
    m_view->setPage(page);

    QUrl url(QStringLiteral("https://trakt.tv/oauth/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"),
                       TraktService::redirectUri());
    url.setQuery(query);
    m_view->load(url);
}

void TraktLoginDialog::handleCallbackUrl(const QUrl &url)
{
    const QUrlQuery query(url.query());
    m_authCode = query.queryItemValue(QStringLiteral("code")).trimmed();
    if (m_authCode.isEmpty()) {
        m_errorText = query.queryItemValue(QStringLiteral("error")).trimmed();
        if (m_errorText.isEmpty()) {
            m_errorText = QStringLiteral("missing_code");
        }
    }
    accept();
}
