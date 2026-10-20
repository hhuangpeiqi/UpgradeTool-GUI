#include "gui/AdbRunner.h"
#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

static int failures = 0;
#define CHECK(c)                                                                                                       \
	do {                                                                                                               \
		if (!(c)) {                                                                                                    \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                           \
			++failures;                                                                                                \
		}                                                                                                              \
	} while (0)
template <class F> bool Await(F predicate)
{
	QElapsedTimer timer;
	timer.start();
	while (!predicate() && timer.elapsed() < 3000) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	return predicate();
}
static QByteArray Read(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}
static void Write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	CHECK(file.open(QIODevice::WriteOnly));
	file.write(data);
}

// This executable also acts as ADB. No test invokes the host's real ADB or USB stack.
static int FakeAdb(QStringList args)
{
	const auto mode = qEnvironmentVariable("AW_TEST_MODE");
	const auto dir = qEnvironmentVariable("AW_TEST_DIR");
	QFile log(dir + "/log");
	if (!log.open(QIODevice::WriteOnly | QIODevice::Append))
		return 3;
	log.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact) + '\n');
	log.close();
	if (args == QStringList{"version"}) {
		if (mode == "version-bad") std::printf("unrelated tool\n");
		else std::printf("Android Debug Bridge version 1.0.41\nVersion fake-test\n");
		return 0;
	}
	if (args.value(0) == "devices") {
		if (mode == "poll-fail") { std::printf("test server error\n"); return 7; }
		if (mode == "poll-hang") QThread::msleep(30000);
		if (args.contains("-l") && mode == "missing")
			return 0;
		std::printf("List of devices attached\nboard device usb:1-5.4 model:AUT\n");
		if (mode == "multi" || mode == "cancel")
			std::printf("second device usb:1-5.2\n");
		return 0;
	}
	if (args.value(0) != "-s" || (args.value(1) != "board" && args.value(1) != "second"))
		return 2;
	const auto command = args.value(3);
	if (args.value(2) == "shell" && command.contains("AW_ANDROID=")) {
		if (mode == "probe-hang") {
			QThread::msleep(1500);
			return 0;
		}
		const bool android = mode == "android" || mode == "hang" || mode == "wrong-port" || mode == "unmapped" ||
		                     mode == "normal-reboot" || mode == "duplicate";
		std::printf(
		    "AW_ANDROID=%s\nAW_BUILDROOT=%s\nAW_SOC=%s\nAW_UID=%s\nAW_DEVMEM=%s\nAW_REBOOT=%s\nAW_REBOOT_LINK=%s\n",
		    android ? "12" : "", android ? "0" : "1", mode == "wrong-soc" ? "sun50i" : "arm,sun8iw11p1",
		    mode == "nonroot" ? "2000" : "0", mode == "no-devmem" ? "0" : "1", mode == "unknown" ? "" : "/sbin/reboot",
		    mode == "linux" || mode == "unknown" ? "reboot" : "reboot -> ../bin/busybox");
		return 0;
	}
	if (mode == "write-fail") {
		std::printf("FEL flag verification failed\nAW_SWITCH_ERROR\n");
		return 1;
	}
	Write(dir + "/requested", "1");
	if (mode == "hang")
		QThread::msleep(1500);
	return 0;
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc > 1)
		return FakeAdb(app.arguments().mid(1));
	qputenv("ADB", QCoreApplication::applicationFilePath().toUtf8());
	{
		QTemporaryDir dir;
		qputenv("AW_TEST_DIR", dir.path().toUtf8());
		qputenv("AW_TEST_MODE", "poll-ok");
		gui::AdbRunner runner(nullptr, [](bool) { return QVector<gui::AdbUsbDevice>{}; }, 200, 650);
		int availability = 0, diagnostics = 0;
		bool available = false;
		QString error;
		QObject::connect(&runner, &gui::AdbRunner::availabilityChanged, [&](bool ok, const QString &message) {
			++availability; available = ok; error = message;
		});
		QObject::connect(&runner, &gui::AdbRunner::diagnosticMessage, [&](const QString &message) {
			++diagnostics; CHECK(message.contains(QCoreApplication::applicationFilePath()));
			CHECK(message.contains("fake-test"));
		});
		runner.Start();
		CHECK(Await([&] { return available && !runner.Devices().isEmpty(); }));
		CHECK(diagnostics == 1);
		qputenv("AW_TEST_MODE", "poll-fail");
		QMetaObject::invokeMethod(&runner, "onPollTick");
		CHECK(Await([&] { return !available && runner.Devices().isEmpty(); }));
		CHECK(error.contains("test server error"));
		const int failedCount = availability;
		QMetaObject::invokeMethod(&runner, "onPollTick");
		CHECK(Await([&] { return Read(dir.filePath("log")).count("devices") >= 3; }));
		// Wait for that process to finish before checking duplicate-error suppression.
		for (int i = 0; i < 50; ++i) { QCoreApplication::processEvents(); QThread::msleep(2); }
		CHECK(availability == failedCount);
		qputenv("AW_TEST_MODE", "poll-ok");
		QMetaObject::invokeMethod(&runner, "onPollTick");
		CHECK(Await([&] { return available && !runner.Devices().isEmpty(); }));
		qputenv("AW_TEST_MODE", "poll-hang");
		QMetaObject::invokeMethod(&runner, "onPollTick");
		QMetaObject::invokeMethod(&runner, "onPollKillTimeout");
		CHECK(!available && runner.Devices().isEmpty() && error.contains("timed out"));
		qputenv("AW_TEST_MODE", "poll-ok");
		QMetaObject::invokeMethod(&runner, "onPollTick");
		CHECK(Await([&] { return available; }));
		CHECK(diagnostics == 1 && Read(dir.filePath("log")).count("version") == 1);
		qputenv("ADB", dir.filePath("missing-adb").toUtf8());
		QMetaObject::invokeMethod(&runner, "onPollTick");
		CHECK(Await([&] { return !available && error.contains("Cannot start ADB process"); }));
		CHECK(runner.Devices().isEmpty());
		qputenv("ADB", QCoreApplication::applicationFilePath().toUtf8());
		QMetaObject::invokeMethod(&runner, "onPollTick");
		CHECK(Await([&] { return available && !runner.Devices().isEmpty(); }));
		CHECK(diagnostics == 1);
		runner.Stop();
	}
	{
		QTemporaryDir dir;
		qputenv("AW_TEST_DIR", dir.path().toUtf8());
		qputenv("AW_TEST_MODE", "version-bad");
		gui::AdbRunner runner;
		QString error;
		QObject::connect(&runner, &gui::AdbRunner::availabilityChanged, [&](bool ok, const QString &message) {
			CHECK(!ok); error = message;
		});
		runner.Start();
		CHECK(Await([&] { return error.contains("Invalid ADB version"); }));
		CHECK(runner.Devices().isEmpty() && !Read(dir.filePath("log")).contains("devices"));
	}
	QString t3Command;
	auto scenario = [&](const QString &mode, gui::AdbRunner::FelMethod method, bool expectOk,
	                    const QString &expectedCommand) {
		QTemporaryDir dir;
		qputenv("AW_TEST_DIR", dir.path().toUtf8());
		qputenv("AW_TEST_MODE", mode.toUtf8());
		int completed = 0;
		bool ok = false;
		QString message;
		int choices = 0;
		{
			gui::AdbRunner runner(
			    nullptr,
			    [&](bool readSerial) {
				    if (mode == "unmapped")
					    return QVector<gui::AdbUsbDevice>{};
				    const bool requested = QFile::exists(dir.path() + "/requested");
				    gui::AdbUsbDevice d{1, {5, 4}, "board", !readSerial && requested && mode != "normal-reboot"};
				    if (!readSerial && mode == "wrong-port")
					    d.ports = {5, 2};
				    if (readSerial && mode == "duplicate")
					    return QVector<gui::AdbUsbDevice>{d, d};
				    return QVector<gui::AdbUsbDevice>{d};
			    },
			    200, 650);
			QObject::connect(&runner, &gui::AdbRunner::finished, &app, [&](bool result, const QString &text) {
				++completed;
				ok = result;
				message = text;
			});
			QObject::connect(&runner, &gui::AdbRunner::deviceChoiceRequested, &app, [&](const QStringList &devices) {
				++choices;
				CHECK(devices.size() == 2);
				if (mode == "cancel" && completed == 0)
					runner.CancelSwitch();
				else
					runner.ChooseDevice("board");
			});
			runner.Start();
			CHECK(Await([&] { return !runner.Devices().isEmpty(); }));
			runner.SwitchToFel(method);
			if (mode != "cancel")
				runner.SwitchToLoader(); // An overlapping switch must not change the selected operation.
			CHECK(Await([&] { return completed != 0; }));
			CHECK(completed == 1);
			CHECK(ok == expectOk);
			CHECK(!message.isEmpty());
			if (mode == "multi" || mode == "cancel")
				CHECK(choices == 1);
			std::printf("ADB scenario %s: %s\n", mode.toUtf8().constData(), message.toUtf8().constData());
			// Failure/timeout/cancellation releases the runner for the existing Rockchip entry.
			qputenv("AW_TEST_MODE", "loader");
			CHECK(Await([&] { return !runner.Devices().isEmpty(); }));
			runner.SwitchToLoader();
			CHECK(Await([&] { return completed == 2; }));
			CHECK(ok);
		}
		const auto log = Read(dir.path() + "/log");
		bool found = false;
		for (const auto &line : log.split('\n')) {
			const auto args = QJsonDocument::fromJson(line).array();
			if (args.size() >= 4 && args[0].toString() == "-s" && args[2].toString() != "devices") {
				CHECK(args[1].toString() == "board");
				if (args[2].toString() == "reboot" && args[3].toString() == "efex")
					found |= expectedCommand == "adb";
				if (args[2].toString() == "shell" && args[3].toString() == "reboot efex")
					found |= expectedCommand == "shell";
				if (args[2].toString() == "shell" && args[3].toString().contains("devmem 0x01c20508")) {
					CHECK(expectedCommand == "t3");
					found |= expectedCommand == "t3";
					t3Command = args[3].toString();
				}
			}
		}
		CHECK(found == !expectedCommand.isEmpty());
		if (expectedCommand.isEmpty())
			CHECK(!log.contains("0x01c20508") && !log.contains("reboot efex") && !log.contains("\"efex\""));
	};
	using Method = gui::AdbRunner::FelMethod;
	scenario("android", Method::Auto, true, "adb");
	scenario("linux", Method::Auto, true, "shell");
	scenario("unknown", Method::Auto, true, "adb");
	scenario("busybox", Method::Auto, true, "t3");
	scenario("t3", Method::T3Buildroot, true, "t3");
	scenario("nonroot", Method::T3Buildroot, false, "");
	scenario("no-devmem", Method::T3Buildroot, false, "");
	scenario("wrong-soc", Method::T3Buildroot, false, "");
	scenario("android", Method::T3Buildroot, false, "");
	scenario("nonroot", Method::Auto, false, "");
	scenario("no-devmem", Method::Auto, false, "");
	scenario("wrong-soc", Method::Auto, false, "");
	scenario("write-fail", Method::T3Buildroot, false, "t3");
	scenario("multi", Method::T3Buildroot, true, "t3");
	scenario("cancel", Method::Auto, false, "");
	scenario("missing", Method::Auto, false, "");
	scenario("probe-hang", Method::Auto, false, "");
	scenario("hang", Method::Auto, true, "adb");
	scenario("wrong-port", Method::Auto, false, "adb");
	scenario("normal-reboot", Method::Auto, false, "adb");
	scenario("unmapped", Method::Auto, false, "adb");
	scenario("duplicate", Method::Auto, false, "adb");
#ifdef Q_OS_UNIX
	// Execute the actual generated T3 shell command against harmless local substitutes.
	QTemporaryDir shellDir;
	auto executable = [&](const QString &name, const QByteArray &body) {
		const auto path = shellDir.filePath(name);
		Write(path, "#!/bin/sh\n" + body);
		QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	};
	executable("devmem", "if [ $# = 3 ]; then exit ${AW_WRITE_EXIT:-0}; fi\nprintf '%s\\n' \"$AW_READ_VALUE\"\n");
	executable("sync", "echo sync >> \"$AW_SHELL_LOG\"\n");
	executable("reboot", "echo reboot >> \"$AW_SHELL_LOG\"\n");
	qputenv("PATH", shellDir.path().toUtf8() + ':' + qgetenv("PATH"));
	qputenv("AW_SHELL_LOG", shellDir.filePath("result").toUtf8());
	for (const auto &value :
	     {QByteArray("0x5AA5A55A"), QByteArray("0x5aa5a55a"), QByteArray("0x00000000"), QByteArray()}) {
		QFile::remove(shellDir.filePath("result"));
		qputenv("AW_READ_VALUE", value);
		QProcess process;
		process.start("/bin/sh", {"-c", t3Command});
		CHECK(process.waitForFinished(3000));
		const bool verified = value.toLower() == "0x5aa5a55a";
		CHECK((process.exitCode() == 0) == verified);
		CHECK(Read(shellDir.filePath("result")).contains("reboot") == verified);
	}
	QFile::remove(shellDir.filePath("result"));
	qputenv("AW_WRITE_EXIT", "1");
	QProcess process;
	process.start("/bin/sh", {"-c", t3Command});
	CHECK(process.waitForFinished(3000));
	CHECK(process.exitCode() != 0);
	CHECK(!QFile::exists(shellDir.filePath("result")));
#endif
	std::printf("ADB runner: %d failures\n", failures);
	return failures ? 1 : 0;
}
