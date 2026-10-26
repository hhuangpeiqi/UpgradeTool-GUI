#include "gui/AdbService.h"
#include "gui/InspectionQueue.h"
#include "gui/EdlRunner.h"
#include "gui/backend/TaskController.h"
#include "platform/UsbLocation.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <cstdio>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
template<class F> bool Await(F predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return predicate();
}
struct Model { int active = 0, started = 0, killed = 0; bool overlap = false; QStringList commands; };
class FakeProcess : public platform::IProcess {
    std::shared_ptr<Model> model;
    bool running = false;
    QElapsedTimer elapsed;
    QString command;
public:
    explicit FakeProcess(std::shared_ptr<Model> value) : model(std::move(value)) {}
    void Start(const std::string &, const std::vector<std::string> &args) override {
        command = QString::fromStdString(args.at(0)); elapsed.start(); running = true;
        model->overlap |= ++model->active > 1; ++model->started; model->commands.append(command);
    }
    void Poll() override { if (running && command != "hang" && elapsed.elapsed() >= 15) { running = false; --model->active; } }
    bool Running() const override { return running; }
    void Kill() override { if (running) { running = false; --model->active; ++model->killed; } }
    bool NormalExit() const override { return !running; }
    int ExitCode() const override { return 0; }
    std::string TakeOutput() override {
        return command == "version" ? "Android Debug Bridge version 1.0.41\n" :
            command == "devices" ? "List of devices attached\nboard device usb:1-2.3\nlocked unauthorized\nlost offline\n" : "";
    }
};
static void TestCommands() {
    qputenv("ADB", QCoreApplication::applicationFilePath().toUtf8());
    auto model = std::make_shared<Model>();
    gui::AdbPollTiming timing; timing.pollIntervalMs = 20;
    gui::AdbService service(nullptr, [model] { return std::make_unique<FakeProcess>(model); }, timing);
    QVector<gui::AdbCommandResult> results;
    QObject::connect(&service, &gui::AdbService::commandFinished, [&](quint64, const auto &result) { results.append(result); });
    service.Start();
    CHECK(Await([&] { return service.Devices() == QStringList{"board"}; }));
    CHECK(service.snapshot().size() == 3);
    CHECK(service.snapshot()[0].usb == "usb:1-2.3");
    CHECK(!service.snapshot()[1].authorized() && !service.snapshot()[2].authorized());
    service.execute(service.AdbPath(), {"shell"}, std::make_shared<std::stop_source>(), 1000);
    service.execute(service.AdbPath(), {"reboot"}, std::make_shared<std::stop_source>(), 1000);
    CHECK(Await([&] { return results.size() == 2; }));
    CHECK(results[0].succeeded() && results[1].succeeded() && !model->overlap);
    CHECK(model->commands.count("reboot") == 1);
    service.setPaused(true);
    auto cancel = std::make_shared<std::stop_source>();
    service.execute(service.AdbPath(), {"hang"}, cancel, 1000);
    CHECK(Await([&] { return model->commands.contains("hang"); }));
    cancel->request_stop();
    CHECK(Await([&] { return results.size() == 3; }));
    CHECK(results.back().cancelled && !results.back().succeeded());
    service.execute(service.AdbPath(), {"hang"}, std::make_shared<std::stop_source>(), 25);
    CHECK(Await([&] { return results.size() == 4; }));
    CHECK(results.back().timedOut && model->killed == 2);
    // Blocking clients run off the GUI thread and share the same command queue.
    std::atomic_bool done = false;
    gui::AdbCommandResult blocking;
    auto *thread = QThread::create([&] {
        blocking = service.executeBlocking(service.AdbPath(), {"devices", "-l"}, std::make_shared<std::stop_source>(), 1000);
        done = true;
    });
    thread->start();
    CHECK(Await([&] { return done.load(); }));
    CHECK(thread->wait(1000)); delete thread;
    CHECK(blocking.succeeded() && !model->overlap);
    const auto hooks = gui::DefaultEdlHooks(&service);
    const int queries = model->commands.count("devices");
    done = false;
    bool rebooted = false;
    thread = QThread::create([&] {
        QString error;
        rebooted = hooks.reboot("injected-adb", "board", std::make_shared<std::stop_source>(), error);
        done = true;
    });
    thread->start();
    CHECK(Await([&] { return done.load(); }));
    CHECK(thread->wait(1000)); delete thread;
    CHECK(rebooted && model->commands.count("-s") == 1 && model->commands.count("devices") == queries);
    CHECK(!model->overlap);
    service.Stop();
}
static void TestInspections() {
    gui::InspectionQueue queue;
    QObject owner;
    auto old = std::make_shared<std::stop_source>();
    std::atomic_bool started = false;
    bool staleApplied = false, newestApplied = false;
    queue.submit(&owner, "xml", old, [&](std::stop_token token) -> gui::InspectionQueue::Completion {
        started = true;
        while (!token.stop_requested()) QThread::msleep(1);
        return [&] { staleApplied = true; };
    });
    CHECK(Await([&] { return started.load(); }));
    QElapsedTimer replacement; replacement.start();
    queue.submit(&owner, "xml", std::make_shared<std::stop_source>(), [&](std::stop_token) {
        return gui::InspectionQueue::Completion([&] { newestApplied = true; });
    });
    CHECK(replacement.elapsed() < 100 && old->stop_requested());
    CHECK(Await([&] { return newestApplied; }));
    CHECK(!staleApplied);
    // Destroying the result receiver drops completion without dereferencing it.
    auto *removed = new QObject;
    std::atomic_bool parsed = false;
    bool destroyedApplied = false;
    queue.submit(removed, "deleted", std::make_shared<std::stop_source>(), [&](std::stop_token) {
        parsed = true;
        return gui::InspectionQueue::Completion([&] { destroyedApplied = true; });
    });
    delete removed;
    CHECK(Await([&] { return parsed.load(); }));
    QCoreApplication::processEvents();
    CHECK(!destroyedApplied);
    auto shutdown = std::make_shared<std::stop_source>();
    started = false;
    queue.submit(&owner, "shutdown", shutdown, [&](std::stop_token token) -> gui::InspectionQueue::Completion {
        started = true;
        while (!token.stop_requested()) QThread::msleep(1);
        return {};
    });
    CHECK(Await([&] { return started.load(); }));
    queue.shutdown();
    CHECK(shutdown->stop_requested());
    CHECK(Await([&] { return queue.stopped(); }));
}
static void TestLocationsAndProgress() {
    platform::UsbLocation a{"pciroot(0)#usbroot(0)#usb(3)#usbmi(1)", 0, {3}};
    platform::UsbLocation b{"PCIROOT(0)#USBROOT(0)#USB(3)", 0, {3}};
    CHECK(a.matches(b));
    b.native = "PCIROOT(1)#USBROOT(0)#USB(3)";
    CHECK(!a.matches(b)); // Identical downstream port numbers on different controllers.
    CHECK(!platform::UsbLocation{}.matches({}));
    gui::TaskController tasks;
    core::TaskJob job; job.steps.emplace_back(core::Operation::QualcommFlash);
    tasks.start(job); tasks.setSpeed("old speed");
    tasks.onStageProgress("Sahara", 10, 5, -1);
    CHECK(tasks.progressStage() == "Sahara" && tasks.progressSpeed().isEmpty() && tasks.progressValue() == 50);
    tasks.onStageProgress("Waiting", 0, 0, -1);
    CHECK(tasks.progressValue() == -1 && tasks.progressSpeed().isEmpty());
    job.steps[0].qualcomm().dryRun = true; tasks.start(job);
    tasks.onStageProgress("Write", 10, 1, 42);
    CHECK(tasks.progressStage() == "Simulation: Write" && tasks.progressValue() == 42 && tasks.progressSpeed().isEmpty());
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    TestCommands(); TestInspections(); TestLocationsAndProgress();
    std::printf("Shared services: %d failures\n", failures);
    return failures ? 1 : 0;
}
