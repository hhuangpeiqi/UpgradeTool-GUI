#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include "gui/AdbRunner.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QDir>
#include <QSettings>
#include <QTest>
#include <libusb.h>

static int failures = 0;

int main(int argc, char **argv) {
    // This executable is also the fake ADB. Device selection never sends a reboot.
    if (argc > 1) {
        if (QString::fromLocal8Bit(argv[1]) == "start-server") return 0;
        if (QString::fromLocal8Bit(argv[1]) == "version") {
            std::printf("Android Debug Bridge version 1.0.41\nVersion fake-test\n");
            return 0;
        }
        if (QString::fromLocal8Bit(argv[1]) == "devices") {
            std::printf("List of devices attached\nboard device\nsecond device\n");
            return 0;
        }
        return 42;
    }
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QGuiApplication application(argc, argv);
    qputenv("ADB", application.applicationFilePath().toUtf8());
    QQuickStyle::setStyle("Basic");
    qmlRegisterUncreatableMetaObject(gui::Operations::staticMetaObject, "UpgradeTool", 1, 0, "Operations", "Operation IDs");
    qmlRegisterType<gui::DirModel>("UpgradeTool", 1, 0, "DirModel");
    if (libusb_init(nullptr) < 0) return 2;
    {
        gui::EdlHooks hooks;
        hooks.discover = [](const QString &, const gui::EdlCancel &, QString &) {
            gui::EdlEndpoint endpoint;
            endpoint.id = "diag"; endpoint.label = "Test DIAG";
            endpoint.location = "usb:1-2"; endpoint.suggested = true;
            return QVector<gui::EdlEndpoint>{endpoint};
        };
        hooks.openDiag = [](const gui::EdlEndpoint &, const gui::EdlCancel &, QString &error) {
            error = "Test connection failure";
            return std::unique_ptr<gui::DiagSession>{};
        };
        gui::DriverHooks driverHooks;
        driverHooks.scan = [](bool) { return gui::DriverScan{}; };
        driverHooks.qualcommInstalled = [] { return false; };
        gui::AppController controller(nullptr, std::move(hooks), 1, nullptr, std::move(driverHooks), true);
        controller.ignorePermission();
        CHECK(AwaitUi([&] { return controller.adbReady(); }));
        QQmlApplicationEngine engine;
        QObject::connect(controller.language(), &gui::LanguageManager::languageChanged, &engine, &QQmlApplicationEngine::retranslate);
        QObject::connect(&engine, &QQmlEngine::warnings, &application, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) {
                std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData());
                ++failures;
            }
        });
        engine.rootContext()->setContextProperty("app", &controller);
        engine.load(QUrl("qrc:/qt/qml/UpgradeTool/Main.qml"));
        CHECK(!engine.rootObjects().isEmpty());
        if (engine.rootObjects().isEmpty()) return 1;
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        CHECK(window);
        if (!window) return 1;
        CHECK(QTest::qWaitForWindowExposed(window));
        // Xvfb has no window manager to activate the window for keyboard input.
        window->requestActivate();
        CHECK(QTest::qWaitForWindowActive(window));
        auto *dialog = window->findChild<QObject *>("adbDeviceDialog");
        auto *progress = window->findChild<QObject *>("statusProgress");
        auto *percentage = window->findChild<QObject *>("statusPercentage");
        auto *message = window->findChild<QQuickItem *>("statusProgressMessage");
        auto *tooltip = window->findChild<QObject *>("statusProgressTooltip");
        auto *recoveryDialog = window->findChild<QObject *>("switchRecoveryDialog");
        auto *adb = controller.findChild<gui::AdbRunner *>();
        CHECK(dialog && progress && percentage && message && tooltip && recoveryDialog && adb);
        if (!dialog || !progress || !percentage || !message || !tooltip || !recoveryDialog || !adb) return 1;
        for (const auto &language : {"en", "zh"}) for (const auto &theme : {"light", "dark"}) {
            controller.language()->setMode(language);
            controller.theme()->setMode(theme);
            for (int width : {1024, 1300}) for (int vendor : {0, 1}) for (int page : {0, 1}) {
                controller.setVendor(vendor);
                window->setProperty("currentPage", page);
                window->setWidth(width);
                window->setHeight(width == 1024 ? 640 : 780);
                auto *button = FindVisualItem(window->contentItem(), vendor == 0 ? "rkSwitchButton" : "awSwitchButton");
                // Stack pages share names, so select the visible page's button.
                for (auto *candidate : window->findChildren<QQuickItem *>(vendor == 0 ? "rkSwitchButton" : "awSwitchButton"))
                    if (candidate->isVisible()) button = candidate;
                CHECK(button && button->isEnabled());
                if (!button) continue;
                if (vendor == 0) {
                    auto availability = [&](bool available) {
                        CHECK(QMetaObject::invokeMethod(&controller, "onAdbAvailabilityChanged", Qt::DirectConnection,
                            Q_ARG(bool, available), Q_ARG(QString, QString())));
                    };
                    auto devices = [&](const QStringList &serials) {
                        CHECK(QMetaObject::invokeMethod(&controller, "onAdbDevicesChanged", Qt::DirectConnection,
                            Q_ARG(QStringList, serials)));
                    };
                    QQmlExpression tooltipText(qmlContext(button), button, QStringLiteral("ToolTip.text"));
                    const auto expected = QString::fromUtf8(language == QStringLiteral("en")
                        ? "No ADB device detected" : "未检测到 ADB 设备");
                    // A runtime failure disables switching even before the cached list is cleared.
                    availability(false);
                    CHECK(!button->isEnabled());
                    CHECK(tooltipText.evaluate().toString() == expected && !tooltipText.hasError());
                    devices({});
                    availability(true);
                    CHECK(!button->isEnabled());
                    CHECK(tooltipText.evaluate().toString() == expected && !tooltipText.hasError());
                    devices({"board", "second"});
                    CHECK(button->isEnabled());
                }
                QCoreApplication::processEvents();
                const QPointF before = button->mapToScene(QPointF());
                const QSizeF size(button->width(), button->height());
                const QString text = button->property("text").toString();
                controller.tasks()->setProgress(75, "Previous task");
                controller.tasks()->setSpeed("Old speed");
                CHECK(QMetaObject::invokeMethod(button, "clicked"));
                CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
                CHECK(controller.switching() && controller.tasks()->busy());
                CHECK(button->property("busy").toBool() && !button->isEnabled());
                CHECK(controller.tasks()->progressValue() == -1 && controller.tasks()->progressSpeed().isEmpty());
                CHECK(progress->property("indeterminate").toBool());
                CHECK(percentage->property("text").toString().isEmpty());
                CHECK(message->property("text").toString() == controller.tasks()->progressText());
                CHECK(tooltip->property("text").toString() == controller.tasks()->progressText());
                CHECK(!controller.tasks()->progressText().isEmpty());
                CHECK(message->isVisible());
                CHECK(button->property("text").toString() == text);
                CHECK(button->mapToScene(QPointF()) == before);
                CHECK(QSizeF(button->width(), button->height()) == size);
                CHECK(QMetaObject::invokeMethod(dialog, "close"));
                CHECK(AwaitUi([&] { return !controller.tasks()->busy(); }));
                CHECK(!controller.switching() && !button->property("busy").toBool());
                CHECK(button->isEnabled() && !progress->property("indeterminate").toBool());
                CHECK(controller.tasks()->progressValue() == 0);
                CHECK(message->property("text").toString() == controller.tasks()->progressText());
                CHECK(!message->property("text").toString().isEmpty());
                CHECK(!qobject_cast<QQuickItem *>(progress)->isVisible());
                CHECK(!recoveryDialog->property("visible").toBool());
            }
            window->setWidth(1024);
            window->setHeight(640);
            CHECK(AwaitUi([&] {
                return recoveryDialog->property("_maxHeight").toReal() <= window->height() - 24;
            }));
            CHECK(!window->findChild<QObject *>("switchRecoveryRefreshButton"));
            CHECK(!window->findChild<QObject *>("switchRecoveryDetail"));
            CHECK(!window->findChild<QObject *>("switchRecoverySummary"));
            auto *closeButton = window->findChild<QQuickItem *>("switchRecoveryCloseButton");
            auto *instructions = window->findChild<QQuickItem *>("switchRecoveryInstructions");
            CHECK(closeButton && instructions);
            if (!closeButton || !instructions) continue;
            for (int vendor : {0, 1, 2}) {
                controller.setVendor(vendor);
                if (vendor == 2) {
                    controller.refreshEdl();
                    CHECK(AwaitUi([&] { return controller.edl()->ready(); }));
                    controller.requestEdlSwitch();
                } else {
                    if (vendor == 0) controller.switchToLoader();
                    else controller.switchToFel();
                    CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
                    // Simulate a transport failure after selecting a target, without real USB hardware.
                    dialog->setProperty("handled", true);
                    CHECK(QMetaObject::invokeMethod(dialog, "close"));
                    adb->finished(false, QStringLiteral("Original diagnostic <preserved>\n") + QString(2000, 'x'));
                    adb->CancelSwitch();
                }
                CHECK(AwaitUi([&] { return recoveryDialog->property("visible").toBool(); }));
                CHECK(!controller.switching() && !controller.tasks()->busy());
                CHECK(recoveryDialog->property("titleText").toString() == QString::fromUtf8(
                    QString(language) == "zh" ? "未能完成刷机模式切换" : "Flash mode switch incomplete"));
                CHECK(instructions->property("text").toString() == QString::fromUtf8(QString(language) == "zh"
                    ? "请手动进入刷机模式"
                    : "Please enter flash mode manually."));
                CHECK(AwaitUi([&] {
                    const auto height = recoveryDialog->property("height").toReal();
                    return height > 100 && height <= window->height() - 24;
                }));
                CHECK(recoveryDialog->property("width").toReal() <= window->width() - 32);
                const auto preview = qEnvironmentVariable("UPGRADE_TOOL_RECOVERY_PREVIEW");
                if (!preview.isEmpty()) {
                    QDir output(preview);
                    CHECK(output.mkpath("."));
                    CHECK(window->grabWindow().save(output.filePath(
                        QString("%1-%2-%3.png").arg(language, theme).arg(vendor))));
                }
                if (vendor == 0) {
                    // Closing via Escape must not start another switch or change the task result.
                    CHECK(AwaitUi([&] { return recoveryDialog->property("activeFocus").toBool(); }));
                    QTest::keyClick(window, Qt::Key_Escape);
                } else {
                    CHECK(QMetaObject::invokeMethod(closeButton, "clicked"));
                }
                CHECK(AwaitUi([&] { return !recoveryDialog->property("visible").toBool(); }));
                CHECK(!controller.switching() && !controller.tasks()->busy());
            }
            auto *driverButton = window->findChild<QQuickItem *>("switchRecoveryDriverButton");
            auto *driverDialog = window->findChild<QObject *>("driverDialog");
            CHECK(driverButton && driverDialog);
            if (driverButton && driverDialog) for (int vendor : {0, 1, 2}) {
                controller.setVendor(vendor);
                const auto missingDriver = QCoreApplication::translate("gui::AdbRunner",
                    "The target USB interface driver is missing. Open USB Drivers to install it.");
                const auto fallback = QCoreApplication::translate("SwitchRecoveryDialog",
                    "Check the USB driver, access permissions, and whether another tool is using the device.");
                for (const auto &diagnostic : {QString("Original diagnostic <preserved>\n") + QString(2000, 'x'),
                                               QString(), QString(" \n\t"), missingDriver}) {
                    controller.switchAccessRequested(diagnostic);
                    CHECK(AwaitUi([&] { return recoveryDialog->property("visible").toBool(); }));
                    CHECK(instructions->property("text").toString() ==
                          (diagnostic.trimmed().isEmpty() ? fallback : diagnostic));
                    CHECK(recoveryDialog->property("height").toReal() <= window->height() - 24);
                    CHECK(QMetaObject::invokeMethod(closeButton, "clicked"));
                    CHECK(AwaitUi([&] { return !recoveryDialog->property("visible").toBool(); }));
                }
                controller.switchAccessRequested(missingDriver);
                CHECK(AwaitUi([&] { return recoveryDialog->property("visible").toBool(); }));
                CHECK(driverButton->isVisible());
                CHECK(AwaitUi([&] {
                    const auto driverRight = driverButton->mapToScene(QPointF(driverButton->width(), 0)).x();
                    return qAbs(closeButton->mapToScene(QPointF()).x() - driverRight - 10) < 0.5;
                }));
                CHECK(recoveryDialog->property("titleText").toString() == QString::fromUtf8(
                    QString(language) == "zh" ? "设备暂不可访问" : "Device access unavailable"));
                CHECK(instructions->property("text").toString() == missingDriver);
                CHECK(recoveryDialog->property("height").toReal() <= window->height() - 24);
                CHECK(QMetaObject::invokeMethod(driverButton, "clicked"));
                CHECK(AwaitUi([&] { return driverDialog->property("visible").toBool(); }));
                CHECK(!recoveryDialog->property("visible").toBool());
                CHECK(!controller.drivers()->busy() && !controller.tasks()->busy());
                CHECK(QMetaObject::invokeMethod(driverDialog, "close"));
                CHECK(AwaitUi([&] { return !driverDialog->property("visible").toBool(); }));
                controller.switchRecoveryRequested();
                CHECK(AwaitUi([&] { return recoveryDialog->property("visible").toBool(); }));
                CHECK(!driverButton->isVisible());
                CHECK(instructions->property("text").toString() == QString::fromUtf8(QString(language) == "zh"
                    ? "请手动进入刷机模式" : "Please enter flash mode manually."));
                CHECK(QMetaObject::invokeMethod(closeButton, "clicked"));
                CHECK(AwaitUi([&] { return !recoveryDialog->property("visible").toBool(); }));
            }
        }
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
