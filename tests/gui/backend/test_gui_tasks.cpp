#include "tests/support/qt/TestDevices.h"
#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/QtObserver.h"
#include "gui/OperationSpec.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QSettings>
#include <libusb.h>
#include <thread>
#include <atomic>
#include <cstdio>
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include "BackendTests.h"

int TestTasks([[maybe_unused]] QGuiApplication &application) {
    int failures = 0;
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    {
        gui::TaskController tasks;
        tasks.onProgress("Write boot", 1024, 512, 0);
        CHECK(tasks.progressStage() == "Write boot");
        CHECK(tasks.progressValue() == 50 && tasks.progressText().contains("50%"));
        tasks.setProgress(-1, "Checking device access");
        CHECK(tasks.progressStage() == "Checking device access");
        tasks.resetProgress();
        CHECK(tasks.progressStage().isEmpty() && tasks.progressText().isEmpty());
    }

	{
		platform::LogService log;
        gui::QtObserver observer(log);
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
		controller.language()->setMode("en");
		int submissions = 0, finishes = 0;
		core::TaskJob captured;
		QObject::connect(&controller, &gui::AppController::requestExecute, &application, [&](const core::TaskJob &job) { ++submissions; captured = job; });
		QObject::connect(controller.tasks(), &gui::TaskController::busyChanged, &application, [&] { if (!controller.tasks()->busy()) ++finishes; });
		// Invalid numbers are rejected before submitting a request, regardless of devices.
		controller.rockchip()->runOperation(gui::Operations::EraseLba, {{"begin", "-1"}, {"count", "4"}});
		controller.rockchip()->runOperation(gui::Operations::EraseLba, {{"begin", "0x100000000"}, {"count", "4"}});
		controller.rockchip()->runOperation(gui::Operations::EraseLba, {{"begin", "invalid"}, {"count", "4"}});
		CHECK(submissions == 0 && !controller.tasks()->busy());
		QFile parameter(temporary.filePath("parameter with spaces.txt")); CHECK(parameter.open(QIODevice::WriteOnly));
		parameter.write("TYPE: GPT\nCMDLINE:mtdparts=rk29xxnand:0x1000@0x2000(boot)\n"); parameter.close();
		QString output = temporary.filePath("result with spaces.gpt");
		controller.rockchip()->createGpt(parameter.fileName(), output);
		CHECK(submissions == 1 && controller.tasks()->busy()); CHECK(captured.steps.size() == 1 && captured.targets.empty());
		CHECK(Await([&] { return !controller.tasks()->busy(); })); CHECK(finishes == 1); CHECK(QFile(output).size() == 0x4400);
		// Cancelling immediately must survive queued task startup.
		controller.rockchip()->createGpt(parameter.fileName(), temporary.filePath("cancel.gpt")); controller.cancel();
		CHECK(Await([&] { return !controller.tasks()->busy(); })); CHECK(finishes == 2); CHECK(captured.cancelled->stop_requested());
		CHECK(controller.rockchip()->advancedHint().contains(QString::fromUtf8("Cancelled")));
		// A new task gets a new cancellation token.
		controller.rockchip()->createGpt(parameter.fileName(), output);
		CHECK(Await([&] { return !controller.tasks()->busy(); })); CHECK(finishes == 3 && !captured.cancelled->stop_requested());
		// Batch selections distinguish duplicate and empty serials using USB location keys.
		QVariantList devices;
		for (unsigned int i = 1; i <= 3; ++i) {
			QVariantMap device;
			device["index"] = i; device["serial"] = i < 3 ? "duplicate" : "";
			device["locationId"] = 0xffff0000u + i; device["mode"] = "Loader";
			device["key"] = QString::number(i); devices.append(device);
		}
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection, Q_ARG(QVariantList, SnapshotDevices(devices))));
		controller.setBatchTargets({"1", "2", "3"}, true);
		CHECK(controller.batchSelected() == 3);
		controller.rockchip()->runOperation(gui::Operations::TestDevice);
		CHECK(captured.targets.size() == 3 && captured.continueOnError);
		CHECK(core::DeviceAccess::Identity(captured.targets[0]).location != core::DeviceAccess::Identity(captured.targets[1]).location);
		controller.cancel(); CHECK(Await([&] { return !controller.tasks()->busy(); }));
		// Local work never receives the selected batch targets.
		controller.rockchip()->createGpt(parameter.fileName(), output);
		CHECK(captured.targets.empty()); CHECK(Await([&] { return !controller.tasks()->busy(); }));

        // Vendor filters and local Allwinner tasks must not inherit Rockchip targets.
        controller.clearBatch(); controller.setVendor(1);
        QVariantMap aw; aw["vendor"]=1; aw["index"]=1; aw["mode"]="FEL/FES";
        aw["key"]="allwinner:1-5.4"; aw["bus"]=1; aw["ports"]=QVariantList{5,4};
        devices.append(aw);
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(devices))));
        CHECK(controller.devices().size()==1 && controller.devices()[0].toMap()["vendor"].toInt()==1);
        int before=submissions;
        controller.allwinner()->run(gui::Operations::AllwinnerConvert,{{"logicOffset","-1"}});
        CHECK(submissions==before && !controller.tasks()->busy());
        controller.rockchip()->runOperation(gui::Operations::EraseFlash); CHECK(submissions==before);
        controller.allwinner()->run(gui::Operations::AllwinnerDeviceInfo);
        CHECK(captured.targets.size()==1 && core::DeviceAccess::Identity(captured.targets[0]).ports==std::vector<uint8_t>({5,4}));
        CHECK(captured.steps[0].vendor()==core::Vendor::Allwinner);
        controller.setVendor(0); CHECK(controller.vendor()==1); // Locked while queued/running.
        controller.cancel(); CHECK(Await([&] { return !controller.tasks()->busy(); }));
        int awBefore=submissions;
        controller.allwinner()->setFirmware(temporary.filePath("missing-allwinner.img"));
        CHECK(!controller.tasks()->busy() && submissions==awBefore); // Synchronous silent inspection, no submitted job.
        CHECK(!controller.allwinner()->valid());
        controller.allwinner()->saveSettings({{"page2/logicOffset","0x2000"}});
        CHECK(controller.allwinner()->settings()["page2/logicOffset"].toString()=="0x2000");
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(devices))));
        controller.setVendor(0); CHECK(controller.devices().size()==3);
        controller.setVendor(1); CHECK(controller.devices().size()==1);

	}
	// Closing immediately with a queued job must join the thread without a deadlock.
	{
		gui::AppController controller(NULL);
		controller.rockchip()->runOperation(gui::Operations::FirmwareInfo, {{"path", temporary.filePath("missing.img")}});
	}
    return failures;
}
