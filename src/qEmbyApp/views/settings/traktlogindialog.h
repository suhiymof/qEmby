#ifndef TRAKTLOGINDIALOG_H
#define TRAKTLOGINDIALOG_H

#include <QDialog>
#include <functional>

class QWebEngineView;
class QUrl;

// Embedded browser that walks the user through Trakt's OAuth authorize page
// (same UX as WebView2-based players such as Rodeo Player). The redirect_uri
// points to a fictional host which never resolves: when the page redirects
// there, the navigation is intercepted locally, the authorization code is
// captured and the dialog closes. The URL is never actually fetched.
class TraktLoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TraktLoginDialog(const QString &clientId, QWidget *parent = nullptr);

    // Non-empty after accept() when the user approved the application.
    QString authCode() const { return m_authCode; }
    // Trakt-reported error (e.g. access_denied) when authorization failed.
    QString errorText() const { return m_errorText; }

private:
    class AuthPage;
    void handleCallbackUrl(const QUrl &url);

    QWebEngineView *m_view = nullptr;
    QString m_authCode;
    QString m_errorText;
};

#endif // TRAKTLOGINDIALOG_H
