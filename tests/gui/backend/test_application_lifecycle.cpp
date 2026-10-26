#include "gui/InspectionQueue.h"
#include "gui/ApplicationLifecycle.h"
#include "gui/backend/AppController.h"
#include "gui/QtObserver.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include <QCloseEvent>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <libusb.h>
#include <atomic>

static int failures = 0;
static gui::EdlHooks NoDevices()
{
    gui::EdlHooks hooks;
    hooks.discover = [](const QString &, const gui::EdlCancel &, QString &) { return QVector<gui::EdlEndpoint>{}; };
    return hooks;
}

class RejectCloseWindow : public QWindow {
public:
    int closes = 0;
    bool event(QEvent *event) override {
        if (event->type() == QEvent::Close) { ++closes; event->ignore(); return true; }
        return QWindow::event(event);
    }
};

static void TestExit(QGuiApplication &application, bool closeWindow)
{
    gui::AppController controller(nullptr, NoDevices(), 100);
    QWindow window; window.show();
    RejectCloseWindow auxiliary; auxiliary.show();
    gui::ApplicationLifecycle lifecycle(&controller, &window);
    int ready = 0;
    QObject::connect(&controller, &gui::AppController::shutdownReady, [&] { ++ready; });
    if (closeWindow) window.close();
    else { QEvent quit(QEvent::Quit); QCoreApplication::sendEvent(&application, &quit); }
    CHECK(controller.shuttingDown());
    controller.requestQuit();
    CHECK(Await([&] { return ready == 1; }));
    CHECK(auxiliary.closes == 0);
    controller.requestQuit();
    QCoreApplication::processEvents();
    CHECK(ready == 1);
}

static void TestCancellation()
{
    gui::AppController controller(nullptr, NoDevices(), 100);
    core::TaskJob job;
    controller.tasks()->start(job);
    auto *thread = controller.findChild<QThread *>();
    auto *work = new QObject;
    work->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, work, &QObject::deleteLater);
    std::atomic_bool running = false, unwound = false;
    QMetaObject::invokeMethod(work, [&] {
        running = true;
        while (!job.cancelled->stop_requested()) QThread::msleep(1);
        QThread::msleep(80);
        unwound = true;
    }, Qt::QueuedConnection);
    CHECK(Await([&] { return running.load(); }));
    auto *observer = controller.findChild<gui::QtObserver *>();
    bool selecting = false;
    QObject::connect(observer, &gui::QtObserver::storageSelectionRequested, &controller, [&] { selecting = true; });
    auto *selection = QThread::create([&] { observer->OnSelectStorage({"eMMC", "SD"}, 0); });
    selection->start();
    CHECK(Await([&] { return selecting; }));
    int prompts = 0, ready = 0;
    QObject::connect(&controller, &gui::AppController::quitConfirmationRequested, [&] { ++prompts; });
    QObject::connect(&controller, &gui::AppController::shutdownReady, [&] { CHECK(unwound.load()); ++ready; });
    controller.requestQuit(); controller.requestQuit();
    CHECK(prompts == 1 && !controller.shuttingDown() && !job.cancelled->stop_requested());
    controller.confirmQuit(false);
    CHECK(!job.cancelled->stop_requested());
    controller.requestQuit();
    CHECK(prompts == 2);
    controller.confirmQuit(true);
    CHECK(controller.shuttingDown() && job.cancelled->stop_requested());
    CHECK(ready == 0);
    bool responsive = false;
    QTimer::singleShot(0, &controller, [&] { responsive = true; });
    CHECK(Await([&] { return ready == 1; }));
    CHECK(responsive);
    CHECK(selection->wait(1000));
    delete selection;
}

static void TestDiscoveryCancellation()
{
    auto hooks = NoDevices();
    std::atomic_bool started = false, cancelled = false;
    hooks.discover = [&](const QString &, const gui::EdlCancel &cancel, QString &) {
        started = true;
        while (!cancel->stop_requested()) QThread::msleep(1);
        cancelled = true;
        return QVector<gui::EdlEndpoint>{};
    };
    gui::AppController controller(nullptr, hooks, 100);
    controller.setVendor(2);
    CHECK(Await([&] { return started.load(); }));
    bool ready = false;
    QObject::connect(&controller, &gui::AppController::shutdownReady, [&] { ready = true; });
    controller.requestQuit();
    CHECK(Await([&] { return ready; }));
    CHECK(cancelled.load());
}

static void TestInspectionCancellation()
{
    gui::AppController controller(nullptr, NoDevices(), 100);
    auto *queue = controller.findChild<gui::InspectionQueue *>(); CHECK(queue);
    if (!queue) return;
    std::atomic_bool started = false, unwound = false;
    auto cancel = std::make_shared<std::stop_source>();
    queue->submit(&controller, "exit-test", cancel, [&](std::stop_token token) -> gui::InspectionQueue::Completion {
        started = true;
        while (!token.stop_requested()) QThread::msleep(1);
        unwound = true;
        return {};
    });
    CHECK(Await([&] { return started.load(); }));
    bool ready = false, responsive = false;
    QObject::connect(&controller, &gui::AppController::shutdownReady, [&] { CHECK(unwound.load()); ready = true; });
    controller.requestQuit();
    QTimer::singleShot(0, &controller, [&] { responsive = true; });
    CHECK(Await([&] { return ready; }));
    CHECK(responsive && cancel->stop_requested());
}

static void TestQuitDialog()
{
    gui::AppController controller(nullptr, NoDevices(), 100);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &controller);
    engine.load(QUrl("qrc:/qt/qml/UpgradeTool/Main.qml"));
    CHECK(!engine.rootObjects().isEmpty());
    if (engine.rootObjects().isEmpty()) return;
    auto *dialog = engine.rootObjects().first()->findChild<QObject *>("quitDialog");
    CHECK(dialog);
    if (!dialog) return;
    controller.tasks()->start({});
    controller.requestQuit();
    CHECK(Await([&] { return dialog->property("visible").toBool(); }));
    CHECK(QMetaObject::invokeMethod(dialog->findChild<QObject *>("continueRunningButton"), "clicked"));
    CHECK(Await([&] { return !dialog->property("visible").toBool(); }));
    CHECK(!controller.shuttingDown());
    controller.requestQuit();
    CHECK(Await([&] { return dialog->property("visible").toBool(); }));
    bool ready = false;
    QObject::connect(&controller, &gui::AppController::shutdownReady, [&] { ready = true; });
    CHECK(QMetaObject::invokeMethod(dialog->findChild<QObject *>("confirmQuitButton"), "clicked"));
    CHECK(Await([&] { return ready; }));
}

int main(int argc, char **argv)
{
    QTemporaryDir settings;
    CHECK(settings.isValid());
    // A nonexistent executable prevents polling from using the host ADB service.
    qputenv("ADB", settings.filePath("missing-adb").toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QGuiApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle("Basic");
    qmlRegisterUncreatableMetaObject(gui::Operations::staticMetaObject, "UpgradeTool", 1, 0, "Operations", "Operation IDs");
    qmlRegisterType<gui::DirModel>("UpgradeTool", 1, 0, "DirModel");
    // Lifecycle tests do not execute host ADB commands.
    if (libusb_init(nullptr) < 0) return 2;
    if (application.arguments().contains("--quit-child")) {
        gui::AppController controller(nullptr, NoDevices(), 100);
        QWindow window; window.show();
        RejectCloseWindow auxiliary; auxiliary.show();
        gui::ApplicationLifecycle lifecycle(&controller, &window);
        QObject::connect(&controller, &gui::AppController::shutdownReady, &application, [] { QCoreApplication::exit(0); });
        QTimer::singleShot(50, &application, &QCoreApplication::quit);
        return application.exec();
    }
    TestExit(application, false);
    TestExit(application, true);
    TestCancellation();
    TestDiscoveryCancellation();
    TestInspectionCancellation();
    TestQuitDialog();
    QProcess child;
    child.start(application.applicationFilePath(), {"--quit-child"});
    CHECK(child.waitForFinished(5000));
    CHECK(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0);
    libusb_exit(nullptr);
    std::printf("Application lifecycle: %d failures\n", failures);
    return failures ? 1 : 0;
}
