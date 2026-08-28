#ifndef PAGETRAKT_H
#define PAGETRAKT_H

#include "settingspagebase.h"
#include <qcorotask.h>

class QPushButton;
class QLineEdit;
class QFrame;

class PageTrakt : public SettingsPageBase {
    Q_OBJECT
public:
    PageTrakt(QEmbyCore *core, QWidget *parent = nullptr);

private:
    void refreshAccountUi();
    void updateStatusText(const QString &text);
    QCoro::Task<void> startBrowserLogin();
    void signOut();

    QPushButton *m_accountBtn = nullptr;
    QLineEdit *m_clientIdEdit = nullptr;
    QLineEdit *m_secretEdit = nullptr;
    QFrame *m_accountCard = nullptr;
    bool m_loginInProgress = false;
};

#endif // PAGETRAKT_H
