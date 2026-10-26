#include "gui/AdbRunner.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

template<class F> static bool Await(F predicate, int timeout = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return predicate();
}

struct Model {
    enum Mode { Healthy, SlowStartup, SlowQuery, Failed, Hanging, Empty, BadVersion, SpawnFailure };
    Mode mode = Healthy;
    QStringList commands;
    int active = 0;
    int kills = 0;
    bool overlap = false;
};

class ScriptProcess final : public platform::IProcess {
    std::shared_ptr<Model> model;
    QElapsedTimer elapsed;
    bool running = false;
    int delay = 0, code = 0;
    std::string output;
public:
    explicit ScriptProcess(std::shared_ptr<Model> state) : model(std::move(state)) {}
    ~ScriptProcess() override { if (running) Kill(); }
    void Start(const std::string &, const std::vector<std::string> &args) override {
        if (running) throw std::logic_error("already running");
        const auto command = QString::fromStdString(args.at(0));
        model->commands.append(command);
        if (model->mode == Model::SpawnFailure) throw std::runtime_error("injected spawn failure");
        model->overlap |= model->active != 0;
        ++model->active;
        running = true;
        elapsed.start();
        delay = 0; code = 0; output.clear();
        if (command == "version") {
            output = model->mode == Model::BadVersion ? "unrelated tool\n"
                : "Android Debug Bridge version 1.0.41\nVersion fake-test\n";
        } else if (command == "start-server") {
            if (model->mode == Model::SlowStartup) delay = 100;
        } else if (command == "devices") {
            if (model->mode == Model::SlowQuery) delay = 100;
            if (model->mode == Model::Hanging) delay = 100000;
            if (model->mode == Model::Failed) { code = 1; output = "connection reset\n"; }
            else output = model->mode == Model::Empty ? "List of devices attached\n"
                : "List of devices attached\nboard\tdevice\n";
        } else {
            CHECK(false); // Includes accidental kill-server or device reboot retries.
        }
    }
    void Poll() override {
        if (running && elapsed.elapsed() >= delay) { running = false; --model->active; }
    }
    bool Running() const override { return running; }
    int ExitCode() const override { return code; }
    bool NormalExit() const override { return code >= 0; }
    std::string TakeOutput() override { return std::exchange(output, {}); }
    void Kill() override {
        if (running) { ++model->kills; --model->active; running = false; code = -1; }
    }
};

struct Harness {
    std::shared_ptr<Model> model = std::make_shared<Model>();
    gui::AdbRunner runner;
    QVector<QJsonObject> records;
    bool available = false;
    int errors = 0;
    QElapsedTimer failureElapsed;
    int retryDelay = 0;
    bool respectedBackoff = true;
    explicit Harness(gui::AdbPollTiming timing = {10, 30, 200, 30, 120})
        : runner(nullptr, [](bool) { return QVector<gui::AdbUsbDevice>{}; }, 200, 650,
                 [state = model] { return std::make_unique<ScriptProcess>(state); }, timing) {
        QObject::connect(&runner, &gui::AdbRunner::traceMessage, [&](const QString &message) {
            const auto record = QJsonDocument::fromJson(message.toUtf8()).object();
            records.append(record);
            const auto event = record.value("event").toString();
            if (event == "failed" || event == "timeout") {
                retryDelay = record.value("retry_ms").toInt();
                failureElapsed.start();
            }
            if (event == "start" && failureElapsed.isValid())
                respectedBackoff &= failureElapsed.elapsed() >= retryDelay - 2;
        });
        QObject::connect(&runner, &gui::AdbRunner::availabilityChanged, [&](bool ok, const QString &message) {
            available = ok;
            if (!ok && !message.isEmpty()) ++errors;
        });
    }
    int Count(const QString &event) const {
        int count = 0;
        for (const auto &r : records) if (r.value("event") == event) ++count;
        return count;
    }
};

static void TestRecovery()
{
    // Use a fixed path for the injected process; no real executable is launched.
    qputenv("ADB", QCoreApplication::applicationFilePath().toUtf8());
    {
        Harness h;
        h.model->mode = Model::SlowStartup;
        h.runner.Start();
        CHECK(Await([&] { return h.available; }));
        CHECK(h.model->commands.mid(0, 3) == QStringList({"version", "start-server", "devices"}));
        CHECK(h.model->kills == 0 && !h.model->overlap);
        CHECK(h.runner.RecoveryMessage().isEmpty());
        h.model->mode = Model::SlowQuery;
        CHECK(Await([&] { return h.Count("slow_query") != 0; }));
        CHECK(!h.available && h.runner.Devices().isEmpty());
        CHECK(!h.runner.RecoveryMessage().isEmpty());
        CHECK(h.model->kills == 0 && h.model->active == 1);
        CHECK(Await([&] { return h.available; }));
        CHECK(h.errors == 0 && h.model->kills == 0);
    }
    {
        Harness h;
        h.model->mode = Model::Failed;
        h.runner.Start();
        CHECK(Await([&] { return h.Count("failed") >= 5; }));
        QList<int> retries;
        for (const auto &r : h.records) if (r.value("event") == "failed") retries.append(r.value("retry_ms").toInt());
        CHECK(retries.mid(0, 5) == QList<int>({30, 60, 120, 120, 120}));
        CHECK(h.errors == 1 && !h.model->overlap && h.respectedBackoff);
        CHECK(h.model->commands.count("start-server") >= 5);
        h.model->mode = Model::Empty;
        CHECK(Await([&] { return h.available; }));
        CHECK(h.runner.Devices().isEmpty());
        h.model->mode = Model::Failed;
        CHECK(Await([&] { return h.errors == 2; }));
        CHECK(h.records.last().value("retry_ms").toInt() == 30);
    }
    {
        Harness h;
        h.model->mode = Model::Hanging;
        h.runner.Start();
        CHECK(Await([&] { return h.Count("timeout") != 0; }));
        CHECK(h.model->kills == 1 && h.errors == 1);
        h.model->mode = Model::Healthy;
        CHECK(Await([&] { return h.available; }));
        CHECK(!h.model->overlap && h.model->commands.count("version") == 1);
        // Resume immediately after cancellation, before the old completion is delivered.
        h.model->mode = Model::Hanging;
        CHECK(Await([&] { return h.model->active != 0; }));
        h.runner.Stop();
        const auto count = h.model->commands.size();
        h.model->mode = Model::Healthy;
        h.runner.Resume();
        CHECK(h.model->commands.size() == count);
        CHECK(Await([&] { return h.available; }));
        CHECK(!h.model->overlap);
        h.runner.Stop();
        const auto stoppedCount = h.model->commands.size();
        QCoreApplication::processEvents();
        CHECK(h.model->commands.size() == stoppedCount);
    }
    {
        Harness h;
        h.model->mode = Model::SlowStartup;
        h.runner.Start();
        CHECK(Await([&] { return h.model->commands.contains("start-server"); }));
        qputenv("ADB", "different-injected-adb");
        h.model->mode = Model::Healthy;
        CHECK(Await([&] { return h.available; }));
        CHECK(h.model->commands.count("version") == 2);
        CHECK(!h.model->overlap);
        qputenv("ADB", QCoreApplication::applicationFilePath().toUtf8());
    }
    for (const auto mode : {Model::BadVersion, Model::SpawnFailure}) {
        Harness h;
        h.model->mode = mode;
        h.runner.Start();
        CHECK(Await([&] { return h.errors == 1; }));
        CHECK(!h.model->commands.contains("devices"));
        h.model->mode = Model::Healthy;
        CHECK(Await([&] { return h.available; }));
        CHECK(!h.model->overlap);
    }
}

#ifdef Q_OS_WIN
static void WriteResult(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

// The fake server inherits only the ACK writer. Killing its client closes the reader,
// making the delayed WriteFile fail just as it does in the Windows ADB startup path.
static int NativeAdb(const QStringList &args)
{
    const auto dir = qEnvironmentVariable("AW_ACK_TEST_DIR");
    if (args.value(0) == "--ack-server") {
        const auto handle = reinterpret_cast<HANDLE>(quintptr(args.value(1).toULongLong()));
        QThread::msleep(8000);
        DWORD written = 0;
        const bool ok = WriteFile(handle, "OK\n", 3, &written, nullptr) && written == 3;
        CloseHandle(handle);
        WriteResult(dir + "/ack-result", ok ? "ACK" : "broken");
        return ok ? 0 : 1;
    }
    if (args.value(0) == "version") {
        std::printf("Android Debug Bridge version 1.0.41\nVersion fake-native\n");
        return 0;
    }
    const bool query = qEnvironmentVariable("AW_ACK_TEST_MODE") == "query";
    if ((!query && args.value(0) == "start-server") || (query && args.value(0) == "devices")) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE reader = nullptr, writer = nullptr;
        if (!CreatePipe(&reader, &writer, &security, 0)) return 2;
        SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
        const auto command = QStringLiteral("\"%1\" --ack-server %2")
            .arg(QCoreApplication::applicationFilePath()).arg(quintptr(writer)).toStdWString();
        auto writableCommand = command;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        const bool started = CreateProcessW(nullptr, writableCommand.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
        CloseHandle(writer);
        if (!started) { CloseHandle(reader); return 3; }
        CloseHandle(child.hThread);
        char ack[3]{}; DWORD read = 0;
        const bool ok = ReadFile(reader, ack, 3, &read, nullptr) && read == 3;
        CloseHandle(reader);
        WaitForSingleObject(child.hProcess, 2000);
        CloseHandle(child.hProcess);
        if (!ok) return 4;
    }
    if (args.value(0) == "devices") std::printf("List of devices attached\nboard\tdevice\n");
    return 0;
}

static void TestNativeAck()
{
    for (const auto *mode : {"startup", "query"}) {
        QTemporaryDir dir;
        qputenv("AW_ACK_TEST_DIR", dir.path().toUtf8());
        qputenv("AW_ACK_TEST_MODE", mode);
        gui::AdbRunner runner;
        bool available = false, slow = false;
        int kills = 0;
        QObject::connect(&runner, &gui::AdbRunner::availabilityChanged,
                         [&](bool ok, const QString &) { available = ok; });
        QObject::connect(&runner, &gui::AdbRunner::traceMessage, [&](const QString &message) {
            const auto event = QJsonDocument::fromJson(message.toUtf8()).object().value("event").toString();
            if (event == "slow_query") slow = true;
            if (event == "timeout_kill_requested") ++kills;
        });
        runner.Start();
        CHECK(Await([&] { return available; }, 14000));
        runner.Stop();
        QFile result(dir.filePath("ack-result"));
        CHECK(result.open(QIODevice::ReadOnly));
        CHECK(result.readAll() == "ACK");
        CHECK(kills == 0 && !runner.Devices().isEmpty());
        CHECK(slow == (QByteArray(mode) == "query"));
    }
    qunsetenv("AW_ACK_TEST_DIR");
    qunsetenv("AW_ACK_TEST_MODE");
}
#endif

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (argc > 1) return NativeAdb(app.arguments().mid(1));
#endif
    TestRecovery();
#ifdef Q_OS_WIN
    TestNativeAck();
#endif
    std::printf("ADB recovery: %d failures\n", failures);
    return failures ? 1 : 0;
}
