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
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QtTest/QTest>
#include <atomic>

int main(int argc, char **argv) {
    if (argc > 1) {
        const auto argument = QByteArray(argv[1]);
        if (argument == "start-server") return 0;
        if (argument == "version") { std::printf("Android Debug Bridge version 1.0.41\n"); return 0; }
        if (argument == "devices") { std::printf("List of devices attached\n"); return 0; }
    }
    QGuiApplication application(argc, argv);
    application.setApplicationDisplayName("UpgradeTool-GUI");
    QQuickStyle::setStyle("Basic");
    QTemporaryDir temporary;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    qputenv("ADB", application.applicationFilePath().toUtf8());
    qmlRegisterUncreatableMetaObject(gui::Operations::staticMetaObject, "UpgradeTool", 1, 0, "Operations", "Operation IDs");
    qmlRegisterType<gui::DirModel>("UpgradeTool", 1, 0, "DirModel");
    int failures = 0, installs = 0;
    std::atomic<bool> holdInstall{false}, installerEntered{false}, completeInstall{false};
    std::atomic<int> scanMode{0};
    gui::DriverDevice board;
    board.vendor = 0; board.vid = 0x2207; board.pid = 0x350b;
    board.instance = "USB\\VID_2207&PID_350B\\BOARD-01";
    board.hardwareId = "USB\\VID_2207&PID_350B";
    board.label = "Rockchip USB Device";
    board.verifiedInterface = true; board.state = gui::DriverState::Missing;
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
        settings.setValue("drivers/pending", QJsonDocument(QJsonObject{{"schema", 1}, {"device", board.toJson()}}).toJson());
    }
    auto aw = board; aw.vendor = 1; aw.label = "Allwinner FEL"; aw.instance += "-AW";
    auto qc = board; qc.vendor = 2; qc.label = "Qualcomm HS-USB QDLoader 9008"; qc.instance += "-QC";
    qc.vid = 0x05c6; qc.pid = 0x9008; qc.hardwareId = "USB\\VID_05C6&PID_9008";
    gui::DriverHooks hooks;
    hooks.verificationTimeoutMs = 0;
    hooks.qualcommInstalled = [] { return true; };
    hooks.driverOperation = [&](auto operation, const auto &target, const auto &affected, const auto &progress) {
        if (operation == gui::DriverOperation::RockusbAssistant) return hooks.installWithProgress(target, affected, progress);
        return gui::DriverInstallResult{false, true, {}};
    };
    hooks.scan = [&, board, aw, qc](bool) {
        const int mode = scanMode;
        if (mode == 2) return gui::DriverScan{};
        if (mode == 3) return gui::DriverScan{{}, "Test detection failure"};
        if (mode == 5) {
            auto diagnostic = qc;
            diagnostic.pid = 0x9092; diagnostic.interfaceNumber = 0;
            diagnostic.instance = "USB\\VID_05C6&PID_9092&MI_00\\BOARD";
            diagnostic.hardwareId = "USB\\VID_05C6&PID_9092&MI_00";
            diagnostic.label = "Qualcomm diagnostic interface"; diagnostic.problem = 28;
            return gui::DriverScan{{diagnostic}, {}};
        }
        if (mode == 4) {
            gui::DriverScan result;
            for (int i = 0; i < 8; ++i) {
                auto device = board;
                device.instance += QString::number(i);
                result.devices.push_back(device);
            }
            return result;
        }
        auto device = board;
        if (mode == 1) device.state = gui::DriverState::Ready;
        return gui::DriverScan{{device, aw, qc}, {}};
    };
    hooks.installWithProgress = [&](const auto &, const auto &, const gui::DriverProgress &progress) {
        ++installs; installerEntered = true;
        progress(gui::DriverPhase::Preparing);
        progress(gui::DriverPhase::Installing);
        while (holdInstall) QThread::msleep(1);
        if (completeInstall) return gui::DriverInstallResult{true, false, {}};
        return gui::DriverInstallResult{false, true, {}};
    };
    gui::AppController controller(nullptr, gui::DefaultEdlHooks(), 100, nullptr, hooks, true);
    controller.ignorePermission();
    CHECK(AwaitUi([&] { return controller.drivers()->attentionCount() > 0; }));
    QQmlApplicationEngine engine;
    QObject::connect(controller.language(), &gui::LanguageManager::languageChanged, &engine, &QQmlApplicationEngine::retranslate);
    QObject::connect(&engine, &QQmlEngine::warnings, &application, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors) { std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData()); ++failures; }
    });
    engine.rootContext()->setContextProperty("app", &controller);
    engine.load(QUrl("qrc:/qt/qml/UpgradeTool/Main.qml"));
    CHECK(!engine.rootObjects().isEmpty());
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *dialog = window->findChild<QObject *>("driverDialog");
    auto *confirmation = window->findChild<QObject *>("driverConfirmationDialog");
    CHECK(window && dialog && confirmation);
    if (!window || !dialog || !confirmation) return 1;
    CHECK(QMetaObject::invokeMethod(dialog, "openDialog"));
    CHECK(AwaitUi([&] { return !controller.drivers()->checking() && controller.drivers()->canResume(); }));
    auto *resume = FindVisualItem(window->contentItem(), "driverResumeButton");
    auto *forget = FindVisualItem(window->contentItem(), "driverForgetButton");
    CHECK(resume && forget && resume->isVisible() && resume->isEnabled());
    if (!resume || !forget) return 1;
    for (const auto &language : {"en", "zh"}) {
        controller.language()->setMode(language);
        window->setWidth(1024); window->setHeight(640);
        CHECK(resume->property("text").toString() == QString::fromUtf8(QByteArray(language) == "zh" ? "继续安装…" : "Continue setup…"));
        if (argc > 1) {
            QDir().mkpath(QString::fromLocal8Bit(argv[1])); QTest::qWait(80);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1]) + QString("/driver-pending-%1.png").arg(language)));
        }
    }
    CHECK(QMetaObject::invokeMethod(resume, "clicked"));
    CHECK(AwaitUi([&] { return confirmation->property("visible").toBool(); }));
    CHECK(installs == 0);
    CHECK(QMetaObject::invokeMethod(confirmation, "close"));
    CHECK(QMetaObject::invokeMethod(forget, "clicked"));
    CHECK(!controller.drivers()->hasPending());
    CHECK(QMetaObject::invokeMethod(dialog, "close"));
    for (const auto &language : {"en", "zh"}) for (const auto &theme : {"light", "dark"}) for (int width : {1024, 1300}) {
        controller.language()->setMode(language); controller.theme()->setMode(theme);
        CHECK(controller.drivers()->devices().first().toMap()["stateText"].toString() ==
            QString::fromUtf8(QByteArray(language) == "zh" ? "未安装驱动" : "Driver missing"));
        controller.setVendor(0); window->setWidth(width); window->setHeight(width == 1024 ? 640 : 780);
        auto *driver = FindVisualItem(window->contentItem(), "statusDriver");
        auto *summary = FindVisualItem(window->contentItem(), "deviceStatusBadge");
        auto *progress = FindVisualItem(window->contentItem(), "statusProgress");
        auto *combo = FindVisualItem(window->contentItem(), "deviceCombo");
        CHECK(driver && summary && progress && combo);
        if (!driver || !summary || !progress || !combo) return 1;
        CHECK(AwaitUi([&] { return driver->isVisible() && driver->width() > 0; }));
        CHECK(!progress->isVisible());
        CHECK(FindVisualItem(window->contentItem(), "statusDriverText")->property("text").toString()
            == controller.drivers()->summaryText());
        QTest::qWait(40);
        const QRectF driverRect(driver->mapToScene(QPointF()), QSizeF(driver->width(), driver->height()));
        CHECK(driverRect.right() <= width);
        CHECK(!driverRect.intersects(QRectF(summary->mapToScene(QPointF()), QSizeF(summary->width(), summary->height()))));
        CHECK(!driverRect.intersects(QRectF(progress->mapToScene(QPointF()), QSizeF(progress->width(), progress->height()))));
        CHECK(combo->mapToScene(QPointF()).x() + combo->width() <= width);
        CHECK(!dialog->property("visible").toBool());
        if (argc > 1) {
            QDir().mkpath(QString::fromLocal8Bit(argv[1]));
            const auto path = QString::fromLocal8Bit(argv[1]) + QString("/driver-bar-%1-%2-%3.png").arg(language, theme).arg(width);
            CHECK(window->grabWindow().save(path));
        }
        CHECK(QMetaObject::invokeMethod(driver, "clicked"));
        CHECK(AwaitUi([&] { return dialog->property("visible").toBool() && !controller.drivers()->checking(); }));
        auto *install = FindVisualItem(window->contentItem(), "driverInstallButton");
        CHECK(install && install->isEnabled());
        if (!install) return 1;
        CHECK(install->property("text").toString() == QString::fromUtf8(QByteArray(language) == "zh" ? "安装…" : "Install…"));
        controller.tasks()->setBusy(true);
        CHECK(AwaitUi([&] { return !install->isEnabled(); }));
        controller.tasks()->finish();
        CHECK(AwaitUi([&] { return !controller.drivers()->checking() && install->isEnabled(); }));
        CHECK(QMetaObject::invokeMethod(install, "clicked"));
        CHECK(AwaitUi([&] { return confirmation->property("visible").toBool(); }));
        CHECK(controller.drivers()->confirmationTitle() == QString::fromUtf8(QByteArray(language) == "zh" ? "安装瑞芯微驱动？" : "Install Rockchip drivers?"));
        auto *confirmInstall = FindVisualItem(window->contentItem(), "driverConfirmButton");
        CHECK(confirmInstall);
        if (!confirmInstall) return 1;
        CHECK(confirmInstall->property("text").toString() == QString::fromUtf8(QByteArray(language) == "zh" ? "安装" : "Install"));
        CHECK(installs == 0);
        if (argc > 1) {
            QTest::qWait(40);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1])
                + QString("/driver-confirmation-%1-%2-%3.png").arg(language, theme).arg(width)));
        }
        CHECK(QMetaObject::invokeMethod(confirmation, "close"));
        QTest::qWait(40);
        if (argc > 1) {
            const auto path = QString::fromLocal8Bit(argv[1]) + QString("/driver-dialog-%1-%2-%3.png").arg(language, theme).arg(width);
            CHECK(window->grabWindow().save(path));
            auto *card = install->parentItem()->parentItem()->parentItem();
            card->setProperty("expanded", true);
            QTest::qWait(40);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1])
                + QString("/driver-expanded-%1-%2-%3.png").arg(language, theme).arg(width)));
            card->setProperty("expanded", false);
        }
        CHECK(QMetaObject::invokeMethod(dialog, "close"));

        auto *message = FindVisualItem(window->contentItem(), "statusProgressMessage");
        auto *speed = FindVisualItem(window->contentItem(), "statusProgressSpeed");
        auto *cancel = FindVisualItem(window->contentItem(), "statusCancel");
        auto *percentage = FindVisualItem(window->contentItem(), "statusPercentage");
        CHECK(message && speed && cancel && percentage);
        if (!message || !speed || !cancel || !percentage) return 1;
        auto capture = [&](const QString &state) {
            if (argc <= 1) return;
            QTest::qWait(180);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1])
                + QString("/status-%1-%2-%3-%4.png").arg(state, language, theme).arg(width)));
        };
        auto rescan = [&] {
            controller.drivers()->refresh();
            CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
            QCoreApplication::processEvents();
        };
        const auto headerPosition = summary->mapToScene(QPointF());
        const qreal narrowComboWidth = combo->width();
        scanMode = 1; rescan();
        CHECK(AwaitUi([&] { return !driver->isVisible() && combo->width() > narrowComboWidth; }));
        CHECK(!progress->isVisible() && !cancel->isVisible() && !percentage->isVisible());
        CHECK(summary->mapToScene(QPointF()) == headerPosition);
        capture("normal");

        controller.tasks()->setBusy(true);
        controller.tasks()->onProgress("Writing system", 100, 42, 0);
        controller.tasks()->setSpeed("25 MB/s · 00:12");
        CHECK(AwaitUi([&] { return progress->isVisible() && cancel->isVisible() && speed->isVisible(); }));
        CHECK(message->property("text").toString() == "Writing system");
        CHECK(percentage->property("text").toString() == "42%");
        const QList<QQuickItem *> taskItems{message, progress, percentage, speed, cancel};
        CHECK(AwaitUi([&] {
            for (int i = 1; i < taskItems.size(); ++i)
                if (taskItems[i - 1]->mapToScene(QPointF(taskItems[i - 1]->width(), 0)).x()
                        > taskItems[i]->mapToScene(QPointF()).x() + 0.5) return false;
            return cancel->mapToScene(QPointF(cancel->width(), 0)).x() <= width;
        }));
        for (int i = 1; i < taskItems.size(); ++i) {
            CHECK(taskItems[i - 1]->mapToScene(QPointF(taskItems[i - 1]->width(), 0)).x()
                <= taskItems[i]->mapToScene(QPointF()).x());
        }
        CHECK(summary->mapToScene(QPointF()) == headerPosition);
        CHECK(summary->mapToScene(QPointF(0, summary->height())).y() <= message->mapToScene(QPointF()).y());
        CHECK(cancel->mapToScene(QPointF(cancel->width(), cancel->height())).x() <= width);
        capture("running");
        CHECK(QMetaObject::invokeMethod(cancel, "clicked"));
        CHECK(controller.tasks()->cancelRequested());
        controller.tasks()->setSpeed({});
        controller.tasks()->setProgress(-1, "Waiting for device");
        CHECK(AwaitUi([&] { return progress->property("indeterminate").toBool() && !percentage->isVisible(); }));
        capture("indeterminate");
        controller.tasks()->setProgress(100, "Operation complete");
        controller.tasks()->finish();
        CHECK(AwaitUi([&] { return !progress->isVisible() && !cancel->isVisible(); }));
        CHECK(message->property("text").toString() == "Operation complete");
        capture("complete");
        controller.tasks()->resetProgress();

        scanMode = 2; rescan();
        CHECK(!driver->isVisible());
        capture("disconnected");
        scanMode = 3; rescan();
        CHECK(AwaitUi([&] { return driver->isVisible(); }));
        CHECK(controller.drivers()->summaryLevel() == "warning");
        capture("detection-failed");
        scanMode = 0; rescan();
    }
    if (argc > 1) {
        scanMode = 4;
        CHECK(QMetaObject::invokeMethod(dialog, "openDialog"));
        CHECK(AwaitUi([&] { return !controller.drivers()->checking() && controller.drivers()->devices().size() == 8; }));
        for (const auto &theme : {"light", "dark"}) {
            controller.theme()->setMode(theme);
            QTest::qWait(40);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1])
                + QString("/driver-scroll-%1.png").arg(theme)));
        }
        CHECK(QMetaObject::invokeMethod(dialog, "close"));
        scanMode = 0;
    }
    controller.setVendor(1);
    CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
    CHECK(QMetaObject::invokeMethod(dialog, "openDialog"));
    CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
    for (const auto &language : {"en", "zh"}) {
        controller.language()->setMode(language);
        auto *awInstall = FindVisualItem(window->contentItem(), "driverInstallButton");
        CHECK(awInstall && awInstall->isVisible() && awInstall->isEnabled());
        if (!awInstall) return 1;
        CHECK(QMetaObject::invokeMethod(awInstall, "clicked"));
        CHECK(AwaitUi([&] { return confirmation->property("visible").toBool(); }));
        CHECK(controller.drivers()->confirmationText() == QString::fromUtf8(QByteArray(language) == "zh"
            ? "驱动将自动安装，请等待完成。"
            : "The driver will install automatically. Please wait for completion."));
        if (argc > 1) {
            QTest::qWait(80);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1])
                + QString("/allwinner-driver-confirmation-%1.png").arg(language)));
        }
        CHECK(QMetaObject::invokeMethod(confirmation, "close"));
    }
    CHECK(QMetaObject::invokeMethod(dialog, "close"));
    controller.setVendor(2);
    CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
    CHECK(QMetaObject::invokeMethod(dialog, "openDialog"));
    CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
    auto *uninstall = FindVisualItem(window->contentItem(), "qualcommUninstallButton");
    CHECK(uninstall && uninstall->isVisible() && uninstall->isEnabled());
    for (const auto &language : {"en", "zh"}) for (const auto &theme : {"light", "dark"}) {
        controller.language()->setMode(language); controller.theme()->setMode(theme);
        window->setWidth(1024); window->setHeight(640);
        QCoreApplication::processEvents();
        auto *qcInstall = FindVisualItem(window->contentItem(), "driverInstallButton");
        CHECK(qcInstall && qcInstall->isVisible() && qcInstall->isEnabled());
        if (!qcInstall) return 1;
        CHECK(QMetaObject::invokeMethod(qcInstall, "clicked"));
        CHECK(AwaitUi([&] { return confirmation->property("visible").toBool(); }));
        CHECK(controller.drivers()->confirmationTitle() == QString::fromUtf8(QByteArray(language) == "zh" ? "安装高通驱动？" : "Install Qualcomm drivers?"));
        CHECK(QMetaObject::invokeMethod(confirmation, "close"));
        CHECK(QMetaObject::invokeMethod(uninstall, "clicked"));
        CHECK(AwaitUi([&] { return confirmation->property("visible").toBool(); }));
        CHECK(controller.drivers()->confirmationTitle() == QString::fromUtf8(QByteArray(language) == "zh" ? "卸载高通驱动？" : "Uninstall Qualcomm drivers?"));
        CHECK(controller.drivers()->canConfirm());
        if (argc > 1) {
            QTest::qWait(80);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1]) + QString("/qualcomm-driver-confirmation-%1-%2.png").arg(language, theme)));
        }
        CHECK(QMetaObject::invokeMethod(confirmation, "close"));
    }
    scanMode = 5; controller.drivers()->refresh();
    CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
    CHECK(controller.drivers()->summaryLevel() == "warning");
    CHECK(controller.drivers()->attentionCount() == 1);
    CHECK(controller.drivers()->devices().first().toMap()["canInstall"].toBool());
    CHECK(FindVisualItem(window->contentItem(), "statusDriver")->isVisible());
    scanMode = 0;
    CHECK(installs == 0);
    CHECK(QMetaObject::invokeMethod(dialog, "close"));
    controller.setVendor(0);
    CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
    // A normal assistant close without a ready device offers reconnect guidance
    // in both languages and releases the app for use while waiting.
    completeInstall = true;
    controller.drivers()->prepareInstall(board.instance);
    CHECK(QMetaObject::invokeMethod(confirmation, "close"));
    controller.drivers()->confirmInstall();
    CHECK(AwaitUi([&] { return controller.drivers()->waitingForReconnect() && !controller.interactionLocked(); }));
    CHECK(installs == 1 && controller.drivers()->hasPending());
    bool reconnectLogged = false;
    for (int i = 0; i < controller.log()->count(); ++i) {
        const auto index = controller.log()->index(i);
        if (controller.log()->data(index, gui::LogModel::TextRole).toString().contains(controller.drivers()->message())) {
            reconnectLogged = true;
            CHECK(!controller.log()->data(index, gui::LogModel::ErrorRole).toBool());
        }
    }
    CHECK(reconnectLogged);
    CHECK(QMetaObject::invokeMethod(dialog, "openDialog"));
    for (const auto &language : {"en", "zh"}) {
        controller.language()->setMode(language);
        CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
        auto *reopen = FindVisualItem(window->contentItem(), "driverResumeButton");
        auto *refreshButton = FindVisualItem(window->contentItem(), "driverRefreshButton");
        auto *closeButton = FindVisualItem(window->contentItem(), "driverCloseButton");
        CHECK(reopen && reopen->isEnabled() && refreshButton && refreshButton->isEnabled());
        CHECK(closeButton && closeButton->isEnabled());
        if (reopen) CHECK(reopen->property("text").toString() == QString::fromUtf8(
            QByteArray(language) == "zh" ? "重新打开驱动助手…" : "Reopen driver assistant…"));
        CHECK(controller.drivers()->message() == QString::fromUtf8(QByteArray(language) == "zh"
            ? "设备尚未就绪，请重新插拔 USB，连接后将自动检测驱动。"
            : "Device is not ready. Reconnect the USB cable; the driver will be checked automatically."));
        CHECK(!FindVisualItem(window->contentItem(), "statusProgress")->isVisible());
        if (argc > 1) {
            QTest::qWait(80);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1])
                + QString("/driver-reconnect-%1.png").arg(language)));
        }
    }
    scanMode = 1; controller.drivers()->refresh(false);
    CHECK(AwaitUi([&] { return !controller.drivers()->hasPending() && !controller.drivers()->checking(); }));
    CHECK(!controller.drivers()->waitingForReconnect() && !controller.interactionLocked() && installs == 1);
    CHECK(QMetaObject::invokeMethod(dialog, "close"));
    completeInstall = false; installs = 0; installerEntered = false;
    scanMode = 0; controller.drivers()->refresh();
    CHECK(AwaitUi([&] { return !controller.drivers()->checking(); }));
    core::Cancellation inspection;
    QObject::connect(controller.rockchip(), &gui::RockchipController::inspectRequested,
        [&](const core::InspectionRequest &request) { inspection = request.cancelled; });
    controller.rockchip()->setFirmware(temporary.filePath("inspect-before-install.img"));
    CHECK(inspection && !inspection->stop_requested());
    CHECK(AwaitUi([&] { return !dialog->property("visible").toBool(); }));
    controller.drivers()->prepareInstall(board.instance);
    CHECK(AwaitUi([&] { return confirmation->property("visible").toBool(); }));
    CHECK(!dialog->property("visible").toBool());
    CHECK(installs == 0);
    CHECK(QMetaObject::invokeMethod(confirmation, "close"));
    holdInstall = true;
    controller.drivers()->confirmInstall();
    CHECK(controller.interactionLocked() && !controller.tasks()->busy());
    CHECK(AwaitUi([&] { return installerEntered.load(); }));
    auto *installProgress = FindVisualItem(window->contentItem(), "statusProgress");
    CHECK(AwaitUi([&] { return installProgress->isVisible() && installProgress->property("indeterminate").toBool(); }));
    CHECK(!FindVisualItem(window->contentItem(), "statusCancel")->isVisible());
    CHECK(AwaitUi([&] { return controller.drivers()->elapsedSeconds() >= 1; }));
    CHECK(FindVisualItem(window->contentItem(), "statusDriverElapsed")->isVisible());
    for (const auto &language : {"en", "zh"}) {
        controller.language()->setMode(language);
        CHECK(QMetaObject::invokeMethod(dialog, "openDialog"));
        CHECK(AwaitUi([&] { return dialog->property("visible").toBool(); }));
        auto *background = FindVisualItem(window->contentItem(), "driverCloseButton");
        CHECK(background && background->isEnabled());
        if (!background) { holdInstall = false; return 1; }
        CHECK(background->property("text").toString() == QString::fromUtf8(QByteArray(language) == "zh" ? "转到后台" : "Run in background"));
        CHECK(controller.drivers()->progressText().contains(QString::fromUtf8(QByteArray(language) == "zh" ? "瑞芯微驱动助手" : "Rockchip driver assistant")));
        if (argc > 1) {
            QTest::qWait(40);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1]) + QString("/driver-installing-%1.png").arg(language)));
        }
        CHECK(QMetaObject::invokeMethod(background, "clicked"));
        CHECK(AwaitUi([&] { return !dialog->property("visible").toBool(); }));
        CHECK(controller.drivers()->busy() && controller.interactionLocked() && installs == 1);
        if (argc > 1) {
            QTest::qWait(40);
            CHECK(window->grabWindow().save(QString::fromLocal8Bit(argv[1]) + QString("/driver-background-%1.png").arg(language)));
        }
    }
    CHECK(inspection && !inspection->stop_requested());
    controller.setVendor(1); CHECK(controller.vendor() == 0);
    controller.rockchip()->runOperation(gui::Operations::EraseFlash);
    CHECK(!controller.tasks()->busy());
    bool readyToExit = false;
    QObject::connect(&controller, &gui::AppController::shutdownReady, [&] { readyToExit = true; });
    controller.requestQuit(); controller.confirmQuit(true);
    QTest::qWait(60);
    CHECK(controller.shuttingDown() && !readyToExit);
    holdInstall = false;
    CHECK(AwaitUi([&] { return readyToExit; }));
    CHECK(installs == 1);
    return failures ? 1 : 0;
}
