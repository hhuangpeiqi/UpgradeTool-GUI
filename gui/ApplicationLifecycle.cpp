#include "gui/ApplicationLifecycle.h"
#include "gui/backend/AppController.h"
#include <QCoreApplication>
#include <QEvent>
#include <QWindow>

namespace gui {
ApplicationLifecycle::ApplicationLifecycle(AppController *controller, QWindow *window)
    : m_controller(controller), m_window(window)
{
    QCoreApplication::instance()->installEventFilter(this);
}

bool ApplicationLifecycle::eventFilter(QObject *object, QEvent *event)
{
    if ((object == QCoreApplication::instance() && event->type() == QEvent::Quit) ||
        (object == m_window && event->type() == QEvent::Close)) {
        event->ignore();
        m_controller->requestQuit();
        return true;
    }
    return QObject::eventFilter(object, event);
}
}
