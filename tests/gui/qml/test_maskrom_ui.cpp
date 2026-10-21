#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"
#include "tests/support/qt/TestDevices.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QSettings>
#include <QTemporaryDir>
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
        // Capture requests without ever resetting attached hardware.
        CHECK(QObject::disconnect(&controller, SIGNAL(requestScan()), nullptr, nullptr));
        CHECK(QObject::disconnect(&controller, SIGNAL(requestExecute(core::TaskJob)), nullptr, nullptr));
        controller.ignorePermission();
        controller.language()->setMode("en");
        int submissions = 0, refreshes = 0;
        core::TaskJob captured;
        QObject::connect(&controller, &gui::AppController::requestExecute, &application,
                         [&](const core::TaskJob &job) { ++submissions; captured = job; });
        QObject::connect(&controller, &gui::AppController::requestScan, &application, [&] { ++refreshes; });
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
        CHECK(window);
        if (!window) return 1;
        window->setProperty("currentPage", 2);
        auto *button = window->findChild<QQuickItem *>("enterMaskromButton");
        CHECK(button);
        if (!button) return 1;
        auto inject = [&](const QVariantList &devices) {
            CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                                            Q_ARG(QVariantList, SnapshotDevices(devices))));
        };
        auto device = [](int number, int mode) {
            return QVariantMap{{"index", number}, {"vendor", 0}, {"usbType", mode},
                {"mode", mode == 2 ? "Loader" : "Maskrom"}, {"locationId", number},
                {"key", QString::number(number)}, {"serial", QString::number(number)}};
        };
        auto run = [&] { controller.rockchip()->runOperation(gui::Operations::EnterMaskrom); };
        auto finish = [&](core::OperationStatus status) {
            core::TaskSummary summary; summary.status = status;
            summary.succeeded = status == core::OperationStatus::Succeeded ? captured.targets.size() : 0;
            summary.failed = status == core::OperationStatus::Failed ? 1 : 0;
            CHECK(QMetaObject::invokeMethod(&controller, "onTaskFinished", Qt::DirectConnection,
                                            Q_ARG(core::TaskSummary, summary)));
        };
        inject({}); CHECK(!button->isEnabled()); run(); CHECK(submissions == 0);
        inject({device(1, 1)}); CHECK(!button->isEnabled()); run(); CHECK(submissions == 0);
        inject({device(1, 2), device(2, 1)});
        CHECK(button->isEnabled());
        controller.setDeviceIndex(1); CHECK(!button->isEnabled());
        controller.setBatchTargets({"1", "2"}, true);
        CHECK(!button->isEnabled()); run(); CHECK(submissions == 0 && !controller.tasks()->busy());
        controller.language()->setMode("zh");
        CHECK(controller.rockchipMaskromEligibility().value("reason").toString().contains(QStringLiteral("所有所选设备")));
        controller.language()->setMode("en");
        inject({device(1, 2), device(2, 2)});
        CHECK(button->isEnabled());
        CHECK(QMetaObject::invokeMethod(button, "clicked"));
        CHECK(submissions == 1 && captured.targets.size() == 2 && captured.continueOnError);
        CHECK(captured.steps.size() == 1 && captured.steps[0].operation() == core::Operation::EnterMaskrom);
        CHECK(!captured.rebootAfter && controller.tasks()->busy() && !button->isEnabled());
        run(); CHECK(submissions == 1);
        CHECK(QMetaObject::invokeMethod(&controller, "onUpgradePrompt", Qt::DirectConnection,
            Q_ARG(int, static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_START))));
        CHECK(controller.tasks()->progressValue() == -1 && controller.tasks()->progressText().contains("Maskrom"));
        controller.cancel(); CHECK(captured.cancelled->stop_requested());
        finish(core::OperationStatus::Cancelled);
        CHECK(!controller.tasks()->busy() && button->isEnabled());
        CHECK(controller.rockchip()->advancedHint().contains("remaining operations cancelled"));
        controller.clearBatch();
        controller.setDeviceIndex(0);
        CHECK(button->isEnabled()); run();
        CHECK(submissions == 2 && captured.targets.size() == 1 && !captured.continueOnError);
        const auto before = refreshes;
        core::OperationResult result; result.operation = core::Operation::EnterMaskrom;
        result.status = core::OperationStatus::Succeeded;
        controller.rockchip()->applyResult(result);
        CHECK(controller.rockchip()->advancedHint() == "Entered Maskrom mode");
        controller.language()->setMode("zh");
        CHECK(controller.rockchip()->advancedHint() == QStringLiteral("已进入 Maskrom 模式"));
        controller.language()->setMode("en");
        finish(core::OperationStatus::Succeeded);
        CHECK(refreshes > before);
        inject({device(1, 1)}); CHECK(!button->isEnabled());
        result.status = core::OperationStatus::Failed; result.error = core::ErrorCode::Timeout;
        controller.rockchip()->applyResult(result);
        CHECK(controller.rockchip()->advancedHintError());
        CHECK(controller.rockchip()->advancedHint().contains("Timed out"));
        controller.language()->setMode("zh");
        CHECK(controller.rockchip()->advancedHint().contains(QStringLiteral("超时")));
        CHECK(button->property("text").toString() == QStringLiteral("进入 Maskrom"));
        controller.language()->setMode("en");
        CHECK(controller.rockchip()->advancedHint().contains("Timed out"));
        // Existing reset feedback remains independent of the Maskrom operation.
        result.operation = core::Operation::ResetDevice; result.status = core::OperationStatus::Succeeded;
        result.error = core::ErrorCode::None; controller.rockchip()->applyResult(result);
        CHECK(controller.rockchip()->advancedHint() == "Reset device succeeded");
        CHECK(controller.rockchip()->imageHint() == "Reboot command sent.");
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
