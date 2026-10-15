#pragma once
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

template<class Predicate>
bool Await(Predicate predicate, int timeout = 5000, unsigned long interval = 1) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(interval);
    }
    return predicate();
}
