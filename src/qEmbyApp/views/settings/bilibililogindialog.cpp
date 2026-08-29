#include "bilibililogindialog.h"

#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>

BiliBiliLoginDialog::BiliBiliLoginDialog(const QUrl &loginUrl, QWidget *parent)
    : QDialog(parent, Qt::WindowCloseButtonHint | Qt::WindowTitleHint)
{
    setWindowTitle(tr("BiliBili 登录"));
    resize(420, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QWebEngineView(this);
    layout->addWidget(m_view);
    m_view->load(loginUrl);
}
