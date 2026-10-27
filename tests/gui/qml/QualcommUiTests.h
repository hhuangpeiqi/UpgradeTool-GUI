#pragma once
#include "tests/support/qt/TestDevices.h"
#include "tests/support/qt/FakeDiagSession.h"
#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QImage>
#include <QColor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QtTest/QTest>
#include <memory>
#include <QSettings>
#include <QVariantList>
#include <libusb.h>
#include <cstdio>
namespace platform { class LogService; }
#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"

struct QualcommUiTests {
    int &failures;
    int argc;
    char **argv;
    QTemporaryDir &temporary;
    QGuiApplication &application;
    QDir &fixture;
    gui::AppController &controller;
    std::atomic<int> &endpointMask, &diagSwitches, &adbSwitches;
    std::atomic_bool &requestSent, &showEdl;
    gui::QualcommController *qc;
    QQuickWindow *window;
    QQuickItem *download = nullptr;
    QObject *loadXml = nullptr, *programPicker = nullptr, *patchPicker = nullptr;
    QObject *edlDialog = nullptr, *flashPage = nullptr;
    QQuickItem *toolsPage = nullptr, *toolNavigation = nullptr, *toolSelector = nullptr;
    const QStringList toolContexts = {"chipinfo", "reset", "ramdump", "kickstart", "zip", "archive", "digests", "provision"};
    const core::Operation toolOperations[8] = {core::Operation::QualcommChipInfo, core::Operation::QualcommReset,
        core::Operation::QualcommRamdump, core::Operation::QualcommKickstart, core::Operation::QualcommCreateZip,
        core::Operation::QualcommCreateArchive, core::Operation::QualcommCreateDigests, core::Operation::QualcommProvision};

    void Write(const QString &path, const QByteArray &data) {
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly));
        CHECK(file.write(data) == data.size());
    }
    void selectTool(int index) {
        CHECK(QMetaObject::invokeMethod(toolsPage, "selectTool", Q_ARG(QVariant, QVariant(index))));
        QCoreApplication::processEvents();
        CHECK(toolsPage->property("selectedTool").toInt() == index);
    }
    bool waitForLayout();
    bool TestDevices();
    bool TestFlash();
    bool TestTools();
    bool TestLayout();
};
