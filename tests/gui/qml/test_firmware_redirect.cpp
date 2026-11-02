#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QTest>
#include <libusb.h>

static int failures = 0;
int main(int argc, char **argv) {
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    qmlRegisterUncreatableMetaObject(gui::Operations::staticMetaObject, "UpgradeTool", 1, 0, "Operations", "Operation IDs");
    qmlRegisterType<gui::DirModel>("UpgradeTool", 1, 0, "DirModel");
    if (libusb_init(nullptr) < 0) return 2;
    {
        gui::AppController controller(nullptr, gui::DefaultEdlHooks(), 45000, nullptr, gui::DriverHooks{}, false);
        QObject::disconnect(&controller, SIGNAL(requestScan()), nullptr, nullptr);
        controller.ignorePermission();
        controller.language()->setMode("en");
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &application, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) { std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData()); ++failures; }
        });
        engine.rootContext()->setContextProperty("app", &controller);
        engine.load(QUrl("qrc:/qt/qml/UpgradeTool/Main.qml"));
        CHECK(!engine.rootObjects().isEmpty());
        if (engine.rootObjects().isEmpty()) return 1;
        auto *window = engine.rootObjects().first();
        auto *dialog = window->findChild<QObject *>("firmwareRedirectDialog");
        auto *confirm = window->findChild<QObject *>("firmwareRedirectConfirm");
        auto *cancel = window->findChild<QObject *>("firmwareRedirectCancel");
        CHECK(dialog && confirm && cancel);
        if (!dialog || !confirm || !cancel) return 1;
        auto write = [&](const QString &name, const QByteArray &bytes) {
            const auto path = temporary.filePath(name);
            QFile file(path); CHECK(file.open(QIODevice::WriteOnly));
            CHECK(file.write(bytes) == bytes.size()); return path;
        };
        const auto aw = write(QStringLiteral("全志 package.bin"), "IMAGEWTY");
        const auto rk = write("rockchip.img", "RKFW");
        const auto unknown = write("unknown.img", "unknown");
        int prompts = 0;
        QObject::connect(&controller, &gui::AppController::firmwareRedirectRequested, [&] { ++prompts; });
        controller.setVendor(0);
        auto *pathField = window->findChild<QObject *>("rkFirmwarePath");
        CHECK(pathField);
        pathField->setProperty("text", aw);
        CHECK(QMetaObject::invokeMethod(pathField, "editingFinished"));
        CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
        CHECK(controller.vendor() == 0 && controller.rockchip()->firmwarePath().isEmpty());
        CHECK(QMetaObject::invokeMethod(cancel, "clicked"));
        CHECK(!dialog->property("visible").toBool() && controller.vendor() == 0);
        CHECK(pathField->property("text").toString().isEmpty());
        controller.selectFirmware(0, aw);
        CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
        QTest::keyClick(qobject_cast<QQuickWindow *>(window), Qt::Key_Escape);
        CHECK(AwaitUi([&] { return !dialog->property("visible").toBool(); }));
        CHECK(controller.vendor() == 0);
        controller.selectFirmware(0, aw);
        CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
        CHECK(QMetaObject::invokeMethod(confirm, "clicked"));
        CHECK(controller.vendor() == 1 && controller.allwinner()->firmware() == aw);
        CHECK(window->property("currentPage").toInt() == 1 && !controller.tasks()->busy());
        // Header recognition does not mark a truncated package valid.
        CHECK(AwaitUi([&] { return controller.allwinner()->states()[1].toMap()["error"].toBool(); }));
        CHECK(!controller.allwinner()->valid());
        controller.selectFirmware(1, rk);
        CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
        CHECK(QMetaObject::invokeMethod(confirm, "clicked"));
        CHECK(controller.vendor() == 0 && controller.rockchip()->firmwarePath() == rk);
        CHECK(!controller.tasks()->busy());
        controller.selectFirmware(0, temporary.filePath("missing.img"));
        CHECK(AwaitUi([&] { return controller.rockchip()->firmwarePath().endsWith("missing.img")
            && controller.rockchip()->firmwareHintError(); }));
        CHECK(!dialog->property("visible").toBool());
        const auto ssfw = write("storage.img", "SSFW");
        controller.setVendor(1);
        controller.selectFirmware(1, ssfw);
        CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
        // Leaving the page invalidates even a ready confirmation.
        window->setProperty("currentPage", 2);
        controller.confirmFirmwareRedirect();
        CHECK(controller.vendor() == 1 && !dialog->property("visible").toBool());
        controller.setVendor(0);
        window->setProperty("currentPage", 1);
        // Superseded and cancelled requests must never navigate or prompt.
        const int previousPrompts = prompts;
        controller.selectFirmware(0, aw);
        controller.selectFirmware(0, unknown);
        CHECK(AwaitUi([&] { return controller.rockchip()->firmwarePath() == unknown; }));
        CHECK(prompts == previousPrompts);
        controller.selectFirmware(0, aw);
        window->setProperty("currentPage", 2);
        controller.selectFirmware(0, unknown);
        controller.cancelFirmwareSelection();
        QCoreApplication::processEvents();
        CHECK(prompts == previousPrompts && controller.vendor() == 0);
        CHECK(!dialog->property("visible").toBool());
        CHECK(controller.rockchip()->firmwarePath() == unknown);
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
