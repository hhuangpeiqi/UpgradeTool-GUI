#pragma once

#include <QObject>
#include <QPointer>

class QWindow;
namespace gui {
class AppController;

// Intercept application Quit before Qt closes every top-level window. Hidden
// popup windows must not be able to veto an explicit application exit.
class ApplicationLifecycle : public QObject {
public:
    ApplicationLifecycle(AppController *controller, QWindow *window);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    AppController *m_controller;
    QPointer<QWindow> m_window;
};
}
