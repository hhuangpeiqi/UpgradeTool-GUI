#include "gui/backend/AppController.h"
#include "gui/QtObserver.h"
#include "gui/OperationSpec.h"
#include "Config.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <libusb.h>
#include <thread>
#include <atomic>
#include <cstdio>
CRKLog *g_pLogObject = NULL;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
template<typename Predicate> static bool Await(Predicate predicate) {
	QElapsedTimer timer; timer.start();
	while (!predicate() && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return predicate();
}
int main(int argc, char **argv) {
	QTemporaryDir temporary;
	qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
	qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
	QGuiApplication application(argc, argv);
	libusb_init(NULL);
	{
		gui::QtObserver observer;
		std::atomic<bool> done(false); int response = -1;
		QObject::connect(&observer, &gui::QtObserver::storageSelectionRequested, &application, [&](const QStringList &, int) { observer.SetSelectResult(2); });
		std::thread worker([&] { response = observer.OnSelectStorage({"FLASH", "EMMC"}, 0); done.store(true); });
		CHECK(Await([&] { return done.load(); }));
		observer.CancelSelection(); worker.join(); CHECK(response == 2);
		observer.BeginSelectionSession(); done.store(false);
		QObject::disconnect(&observer, nullptr, &application, nullptr);
		std::thread cancelled([&] { response = observer.OnSelectStorage({"FLASH"}, 0); done.store(true); });
		observer.CancelSelection(); CHECK(Await([&] { return done.load(); })); cancelled.join(); CHECK(response == 0);
		// Cancellation arriving before the worker asks must also bypass the wait.
		CHECK(observer.OnSelectStorage({"FLASH"}, 0) == 0);
	}
	{
		gui::AppController controller(NULL);
		int submissions = 0, finishes = 0;
		core::TaskJob captured;
		QObject::connect(&controller, &gui::AppController::requestExecute, &application, [&](const core::TaskJob &job) { ++submissions; captured = job; });
		QObject::connect(&controller, &gui::AppController::busyChanged, &application, [&] { if (!controller.busy()) ++finishes; });
		// Invalid numbers are rejected before submitting a request, regardless of devices.
		controller.runOperation(gui::Operations::EraseLba, {{"begin", "-1"}, {"count", "4"}});
		controller.runOperation(gui::Operations::EraseLba, {{"begin", "0x100000000"}, {"count", "4"}});
		controller.runOperation(gui::Operations::EraseLba, {{"begin", "invalid"}, {"count", "4"}});
		CHECK(submissions == 0 && !controller.busy());
		QFile parameter(temporary.filePath("parameter with spaces.txt")); CHECK(parameter.open(QIODevice::WriteOnly));
		parameter.write("TYPE: GPT\nCMDLINE:mtdparts=rk29xxnand:0x1000@0x2000(boot)\n"); parameter.close();
		QString output = temporary.filePath("result with spaces.gpt");
		controller.createGpt(parameter.fileName(), output);
		CHECK(submissions == 1 && controller.busy()); CHECK(captured.steps.size() == 1 && captured.targets.empty());
		CHECK(Await([&] { return !controller.busy(); })); CHECK(finishes == 1); CHECK(QFile(output).size() == 0x4400);
		// Cancelling immediately must survive queued task startup.
		controller.createGpt(parameter.fileName(), temporary.filePath("cancel.gpt")); controller.cancel();
		CHECK(Await([&] { return !controller.busy(); })); CHECK(finishes == 2); CHECK(captured.cancelled->load());
		CHECK(controller.advancedHint().contains(QString::fromUtf8("已取消")));
		// A new task gets a new cancellation token.
		controller.createGpt(parameter.fileName(), output);
		CHECK(Await([&] { return !controller.busy(); })); CHECK(finishes == 3 && !captured.cancelled->load());
		// Batch selections distinguish duplicate and empty serials using USB location keys.
		QVariantList devices;
		for (unsigned int i = 1; i <= 3; ++i) {
			QVariantMap device;
			device["index"] = i; device["serial"] = i < 3 ? "duplicate" : "";
			device["locationId"] = 0xffff0000u + i; device["mode"] = "Loader";
			device["key"] = QString::number(i); devices.append(device);
		}
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection, Q_ARG(QVariantList, devices)));
		controller.setBatchTargets({"1", "2", "3"}, true);
		CHECK(controller.batchSelected() == 3);
		controller.runOperation(gui::Operations::TestDevice);
		CHECK(captured.targets.size() == 3 && captured.continueOnError);
		CHECK(captured.targets[0].location != captured.targets[1].location);
		controller.cancel(); CHECK(Await([&] { return !controller.busy(); }));
		// Local work never receives the selected batch targets.
		controller.createGpt(parameter.fileName(), output);
		CHECK(captured.targets.empty()); CHECK(Await([&] { return !controller.busy(); }));

	}
	// Closing immediately with a queued job must join the thread without a deadlock.
	{
		gui::AppController controller(NULL);
		controller.runOperation(gui::Operations::FirmwareInfo, {{"path", temporary.filePath("missing.img")}});
	}
	libusb_exit(NULL);
	std::printf("test_gui_backend: %s\n", failures ? "FAIL" : "PASS"); return failures ? 1 : 0;
}
