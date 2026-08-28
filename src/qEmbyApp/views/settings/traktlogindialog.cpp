#include "traktlogindialog.h"

#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>

TraktLoginDialog::TraktLoginDialog(const QUrl &activateUrl, QWidget *parent)
    : QDialog(parent, Qt::WindowCloseButtonHint | Qt::WindowTitleHint)
{
    setWindowTitle(tr("Trakt Authorization"));
    resize(520, 720);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QWebEngineView(this);
    layout->addWidget(m_view);
    m_view->load(activateUrl);
}
