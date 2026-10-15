#pragma once
#include "Await.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>

template<class Predicate> bool AwaitUi(Predicate predicate) {
    return Await(predicate, 8000, 5);
}

// Repeater delegates belong to the visual tree, not QObject::children().
inline QQuickItem *FindVisualItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems())
        if (auto *match = FindVisualItem(child, name)) return match;
    return nullptr;
}
inline void MovePointer(QQuickWindow *window, const QPointF &position) {
    QMouseEvent event(QEvent::MouseMove, position, window->mapToGlobal(position.toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
}
