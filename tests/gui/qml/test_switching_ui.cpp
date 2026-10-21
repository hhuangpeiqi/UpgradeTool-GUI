#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QSettings>
#include <libusb.h>

static int failures = 0;

int main(int argc, char **argv) {
    // This executable is also the fake ADB. Device selection never sends a reboot.
    if (argc > 1) {
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
        gui::AppController controller(nullptr, gui::DefaultEdlHooks(), 45000, nullptr, gui::DriverHooks{}, false);
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
        auto *dialog = window->findChild<QObject *>("adbDeviceDialog");
        auto *progress = window->findChild<QObject *>("statusProgress");
        auto *percentage = window->findChild<QObject *>("statusPercentage");
        auto *message = window->findChild<QQuickItem *>("statusProgressMessage");
        auto *tooltip = window->findChild<QObject *>("statusProgressTooltip");
        CHECK(dialog && progress && percentage && message && tooltip);
        if (!dialog || !progress || !percentage || !message || !tooltip) return 1;
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
            }
        }
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
