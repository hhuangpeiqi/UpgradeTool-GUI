#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/QtObserver.h"
#include "gui/OperationSpec.h"
#include "Config.h"
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
		// Status uses the same ADB discovery for both vendors; flash modes take priority.
		gui::AppController controller(nullptr);
		for (int vendor : {0, 1}) {
			controller.setVendor(vendor);
			CHECK(controller.deviceStatusText() == QString::fromUtf8("Detecting ADB…"));
		}
		auto availability = [&](bool available) {
			CHECK(QMetaObject::invokeMethod(&controller, "onAdbAvailabilityChanged", Qt::DirectConnection,
				Q_ARG(bool, available), Q_ARG(QString, QString())));
		};
		auto adbDevices = [&](const QStringList &devices) {
			CHECK(QMetaObject::invokeMethod(&controller, "onAdbDevicesChanged", Qt::DirectConnection,
				Q_ARG(QStringList, devices)));
		};
		for (int vendor : {0, 1, 0}) {
			controller.setVendor(vendor);
			availability(false);
			CHECK(controller.deviceStatusText() == QString::fromUtf8("No ADB environment detected"));
			adbDevices({});
			availability(true);
			CHECK(controller.deviceStatusText() == QString::fromUtf8("No device found"));
			adbDevices({"board"});
			CHECK(controller.deviceStatusText() == QString::fromUtf8("ADB device found"));
			adbDevices({"board", "second-board"});
			CHECK(controller.deviceStatusText() == QString::fromUtf8("Found 2 ADB device(s)"));
		}
		QVariantList devices{
			QVariantMap{{"vendor", 0}, {"index", 1}, {"mode", "Loader"}, {"key", "rk:1-1"}},
			QVariantMap{{"vendor", 1}, {"index", 1}, {"mode", "FEL/FES"}, {"key", "aw:1-2"}}
		};
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
			Q_ARG(QVariantList, devices)));
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found Loader device"));
		controller.setVendor(1);
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found FEL/FES device"));
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
			Q_ARG(QVariantList, QVariantList())));
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found 2 ADB device(s)"));
		controller.setVendor(0);
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found 2 ADB device(s)"));
	}
	// Vendor changes must refresh empty lists even when ADB never emits again.
	{
		gui::AppController controller(nullptr);
		controller.language()->setMode("en");
		const QVariantMap diag{{"vendor", 2}, {"index", 1}, {"qualcommMode", QTQ_DIAG},
			{"mode", "DIAG / USB IF 0"}, {"key", "qualcomm:0::1-3:if0:alt0"}};
		auto update = [&](const QVariantList &devices) {
			CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
				Q_ARG(QVariantList, devices)));
		};
		auto availability = [&](bool available) {
			CHECK(QMetaObject::invokeMethod(&controller, "onAdbAvailabilityChanged", Qt::DirectConnection,
				Q_ARG(bool, available), Q_ARG(QString, QString())));
		};
		auto adbDevices = [&](const QStringList &devices) {
			CHECK(QMetaObject::invokeMethod(&controller, "onAdbDevicesChanged", Qt::DirectConnection,
				Q_ARG(QStringList, devices)));
		};
		int notifications = 0;
		QObject::connect(&controller, &gui::AppController::devicesChanged, [&]{++notifications;});
		auto cycle = [&](const QString &adbStatus, bool flashDevices = false, bool qualcomm = true) {
			for (int vendor : {1, 2, 0, 1, 2, 0}) {
				const int before = notifications;
				controller.setVendor(vendor);
				CHECK(controller.vendor() == vendor);
				CHECK(notifications > before);
				const QString expected = vendor == 2
					? (qualcomm ? QStringLiteral("Interfaces found: DIAG *1") : QStringLiteral("No device found"))
					: flashDevices ? (vendor == 0 ? QStringLiteral("Found Loader device")
						: QStringLiteral("Found FEL/FES device")) : adbStatus;
				CHECK(controller.deviceStatusText() == expected);
			}
		};
		update({diag});
		cycle(QStringLiteral("Detecting ADB…"));
		availability(false);
		cycle(QStringLiteral("No ADB environment detected"));
		availability(true); adbDevices({});
		cycle(QStringLiteral("No device found"));
		adbDevices({"board"});
		cycle(QStringLiteral("ADB device found"));
		adbDevices({"board", "second-board"});
		cycle(QStringLiteral("Found 2 ADB device(s)"));
		update({});
		cycle(QStringLiteral("Found 2 ADB device(s)"), false, false);
		update({diag,
			QVariantMap{{"vendor", 0}, {"index", 1}, {"mode", "Loader"}, {"key", "rk:1-1"}},
			QVariantMap{{"vendor", 1}, {"index", 1}, {"mode", "FEL/FES"}, {"key", "aw:1-2"}}});
		cycle(QStringLiteral("Found 2 ADB device(s)"), true);
		controller.language()->setMode("auto");
	}
#ifdef Q_OS_UNIX
	{
		const QByteArray originalAdb = qgetenv("ADB");
		const QString fakeAdb = temporary.filePath("fake-adb");
		QFile file(fakeAdb); CHECK(file.open(QIODevice::WriteOnly));
		file.write("#!/bin/sh\nif [ \"$1\" = devices ]; then printf 'List of devices attached\\nboard device usb:1-5.4\\n'; else printf 'AW_ANDROID=\\nAW_BUILDROOT=1\\nAW_SOC=sun50i\\nAW_UID=0\\nAW_DEVMEM=1\\nAW_REBOOT=/sbin/reboot\\nAW_REBOOT_LINK=reboot -> busybox\\n'; fi\n");
		file.close(); CHECK(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
		qputenv("ADB", fakeAdb.toUtf8());
		{
			gui::AppController controller(nullptr); controller.setVendor(1); controller.ignorePermission();
			CHECK(Await([&] { return controller.adbReady(); }));
			CHECK(!controller.allwinnerValid());
			controller.switchToFel(); CHECK(controller.busy()); CHECK(controller.switching());
			controller.setVendor(0); CHECK(controller.vendor() == 1);
			CHECK(Await([&] { return !controller.busy(); }));
			CHECK(!controller.switching());
			CHECK(controller.felSwitchError() && controller.felSwitchHint().contains("BusyBox"));
			controller.switchToFel(1); CHECK(controller.busy()); CHECK(controller.switching());
			controller.cancel(); CHECK(!controller.busy()); CHECK(!controller.switching());
			CHECK(controller.felSwitchError());
		}
		if (originalAdb.isNull()) qunsetenv("ADB"); else qputenv("ADB", originalAdb);
	}
#endif
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
		CHECK(controller.advancedHint().contains(QString::fromUtf8("Cancelled")));
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

        // Vendor filters and local Allwinner tasks must not inherit Rockchip targets.
        controller.clearBatch(); controller.setVendor(1);
        QVariantMap aw; aw["vendor"]=1; aw["index"]=1; aw["mode"]="FEL/FES";
        aw["key"]="allwinner:1-5.4"; aw["bus"]=1; aw["ports"]=QVariantList{5,4};
        devices.append(aw);
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList,devices)));
        CHECK(controller.devices().size()==1 && controller.devices()[0].toMap()["vendor"].toInt()==1);
        int before=submissions;
        controller.runAllwinner(gui::Operations::AllwinnerConvert,{{"logicOffset","-1"}});
        CHECK(submissions==before && !controller.busy());
        controller.runOperation(gui::Operations::EraseFlash); CHECK(submissions==before);
        controller.runAllwinner(gui::Operations::AllwinnerDeviceInfo);
        CHECK(captured.targets.size()==1 && captured.targets[0].ports==std::vector<uint8_t>({5,4}));
        CHECK(captured.steps[0].vendor==core::Vendor::Allwinner);
        controller.setVendor(0); CHECK(controller.vendor()==1); // Locked while queued/running.
        controller.cancel(); CHECK(Await([&] { return !controller.busy(); }));
        int awBefore=submissions;
        controller.setAllwinnerFirmware(temporary.filePath("missing-allwinner.img"));
        CHECK(!controller.busy() && submissions==awBefore); // Synchronous silent inspection, no submitted job.
        CHECK(!controller.allwinnerValid());
        controller.saveAllwinnerSettings({{"page2/logicOffset","0x2000"}});
        CHECK(controller.allwinnerSettings()["page2/logicOffset"].toString()=="0x2000");
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList,devices)));
        controller.setVendor(0); CHECK(controller.devices().size()==3);
        controller.setVendor(1); CHECK(controller.devices().size()==1);

	}
    {
        // Exercise result routing without issuing any USB requests.
        gui::AppController controller(NULL);
        QObject::disconnect(&controller, &gui::AppController::requestExecute, nullptr, nullptr);
        controller.setVendor(1);
        core::TaskJob captured;
        int submissions=0;
        QObject::connect(&controller, &gui::AppController::requestExecute, &application,
            [&](const core::TaskJob &job) { captured=job; ++submissions; });
        auto state=[&](int context) { return controller.allwinnerStates()[context].toMap(); };
        auto finish=[&](core::OperationStatus status=core::OperationStatus::Succeeded) {
            core::OperationResult result;
            result.operation=captured.steps[0].operation; result.status=status;
            result.items={{"test", "preserved"}};
            result.diagnostic="expected failure";
            if(result.operation==core::Operation::AllwinnerInspect) {
                result.firmware.valid=status==core::OperationStatus::Succeeded;
                result.partitions.push_back({"rootfs",128,4});
            }
            CHECK(QMetaObject::invokeMethod(&controller,"onOperationResult",Qt::DirectConnection,
                Q_ARG(core::OperationResult,result)));
            core::TaskSummary summary; summary.status=status;
            CHECK(QMetaObject::invokeMethod(&controller,"onTaskFinished",Qt::DirectConnection,
                Q_ARG(core::TaskSummary,summary)));
        };
        controller.setAllwinnerContextFirmware(1,"flash.img");
        controller.setAllwinnerContextFirmware(0,"boot.img");
        controller.setAllwinnerContextFirmware(2,"tools.img");
        // Automatic firmware inspection is synchronous and silent: no job, no busy state.
        CHECK(!controller.busy() && submissions==0);
        CHECK(state(0)["firmware"]=="boot.img" && state(1)["firmware"]=="flash.img" && state(2)["firmware"]=="tools.img");
        CHECK(!state(0)["valid"].toBool() && !state(1)["valid"].toBool() && !state(2)["valid"].toBool());
        const auto summary=controller.allwinnerResults();
        CHECK(summary->count()==0);
        // Explicit inspection still routes results through the worker.
        controller.runAllwinner(gui::Operations::AllwinnerInspect,{{"context",1}});
        CHECK(controller.busy() && captured.targets.empty()); finish();
        controller.runAllwinner(gui::Operations::AllwinnerInspect,{{"context",0}}); finish();
        controller.runAllwinner(gui::Operations::AllwinnerInspect,{{"context",2}}); finish();
        CHECK(state(0)["valid"].toBool() && state(1)["valid"].toBool() && state(2)["valid"].toBool());
        CHECK(summary->count()==1);
        QVariantMap unrelated{{"context",2},{"logicOffset","-1"},{"norMiB","bad"},{"target",99},{"mode",99}};
        unrelated["output"]=temporary.filePath("unpacked");
        controller.runAllwinner(gui::Operations::AllwinnerExtract,unrelated);
        CHECK(controller.busy() && captured.targets.empty()); finish(core::OperationStatus::Failed);
        CHECK(state(2)["valid"].toBool() && state(2)["error"].toBool() && summary->count()==1);
        const QString flashHint=state(1)["hint"].toString();
        QVariantMap device{{"vendor",1},{"index",1},{"key","allwinner:1-5"},{"bus",1},{"ports",QVariantList{5}}};
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList,QVariantList{device})));
        controller.runAllwinner(gui::Operations::AllwinnerDeviceInfo,unrelated);
        CHECK(controller.busy() && captured.targets.size()==1); finish();
        CHECK(state(1)["hint"].toString()==flashHint && summary->count()==1);
        CHECK(state(3)["results"].value<gui::KeyValueModel *>()->count()==1);
        // Reboot needs no firmware path and targets the selected device.
        controller.runAllwinner(gui::Operations::AllwinnerReboot);
        CHECK(controller.busy() && captured.targets.size()==1
            && captured.steps[0].operation==core::Operation::AllwinnerReboot); finish();
        int before=submissions;
        controller.runAllwinner(gui::Operations::AllwinnerFlash,{{"mode",0}});
        CHECK(submissions==before && !controller.busy()); // Selection is mandatory in the GUI API.
        controller.runAllwinner(gui::Operations::AllwinnerFlash,{{"mode",0},{"partitions",QStringList{"rootfs"}},{"logicOffset","bad"}});
        CHECK(controller.busy() && captured.steps[0].allwinner.partitions==std::vector<std::string>{"rootfs"}); finish();
        controller.runAllwinner(gui::Operations::AllwinnerRaw,{{"context",0},{"rawImage","disk.img"},{"rawMode",0},{"logicOffset","bad"}});
        CHECK(controller.busy() && !captured.steps[0].allwinner.hasLogicOffset); finish(core::OperationStatus::Cancelled);
        CHECK(state(0)["hint"].toString()==QString::fromUtf8("Cancelled") && !state(0)["error"].toBool());
        before=submissions;
        controller.runAllwinner(gui::Operations::AllwinnerRaw,{{"context",0},{"rawImage","disk.img"},{"rawMode",1},{"logicOffset","bad"}});
        controller.runAllwinner(gui::Operations::AllwinnerConvert,{{"context",2},{"output","out.img"},{"logicOffset","-1"}});
        CHECK(submissions==before && !controller.busy());
        controller.runAllwinner(gui::Operations::AllwinnerConvert,{{"context",2},{"output","out.img"},{"target",2},{"norMiB","bad"},{"ubootOffset","bad"}});
        CHECK(controller.busy() && captured.targets.empty()); finish();
        CHECK(state(2)["summary"].value<gui::KeyValueModel *>()->count()==1);
        CHECK(summary->count()==1 && state(1)["valid"].toBool());
        controller.setAllwinnerContextFirmware(2,"replacement.img");
        CHECK(!state(2)["valid"].toBool() && state(2)["partitions"].toList().empty());
        CHECK(state(2)["summary"].value<gui::KeyValueModel *>()->count()==0);
        CHECK(state(2)["results"].value<gui::KeyValueModel *>()->count()==0);
        CHECK(state(1)["valid"].toBool() && state(0)["firmware"]=="boot.img");
        before=submissions;
        controller.setAllwinnerContextFirmware(2,"replacement.img");
        CHECK(!controller.busy() && submissions==before); // Same missing path is re-inspected synchronously.
        CHECK(state(2)["firmware"]=="replacement.img" && !state(2)["valid"].toBool());
        gui::AppController restored(NULL);
        CHECK(restored.allwinnerStates()[0].toMap()["firmware"]=="boot.img");
        CHECK(restored.allwinnerStates()[1].toMap()["firmware"]=="flash.img");
        CHECK(restored.allwinnerStates()[2].toMap()["firmware"]=="replacement.img");
    }
    {
        QSettings settings(QSettings::IniFormat,QSettings::UserScope,"upgrade_tool","upgrade_tool");
        settings.beginGroup("allwinner"); settings.remove("");
        settings.setValue("firmware","legacy.img");
        settings.setValue("page0/rawImage","legacy-disk.img");
        settings.setValue("page1/verify",false);
        settings.setValue("page2/logicOffset","0x2000"); settings.sync();
        gui::AppController legacy(NULL);
        for(const auto &value:legacy.allwinnerStates().mid(0,3)) CHECK(value.toMap()["firmware"]=="legacy.img");
        CHECK(legacy.allwinnerSettings()["page0/rawImage"]=="legacy-disk.img");
        CHECK(!legacy.allwinnerSettings()["page1/verify"].toBool());
        CHECK(legacy.allwinnerSettings()["page2/logicOffset"]=="0x2000");
    }

	// Status counts interfaces by semantic mode, independently of their details.
	{
		gui::AppController controller(nullptr);
		controller.language()->setMode("en");
		controller.setVendor(2);
		auto update = [&](const QVariantList &devices) {
			CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
				Q_ARG(QVariantList, devices)));
		};
		QVariantMap diag{{"vendor", 2}, {"index", 1}, {"qualcommMode", QTQ_DIAG},
			{"mode", "DIAG / USB IF 0"}, {"key", "qualcomm:0::1-3:if0:alt0"},
			{"vid", 0x05c6}, {"pid", 0x9091}, {"serial", "45B448E1"}};
		auto second = diag;
		second["index"] = 2; second["mode"] = "DIAG / /dev/ttyUSB1";
		second["key"] = "qualcomm:2:/dev/ttyUSB1:1-3:if1:alt0";
		update({diag, second});
		CHECK(controller.deviceStatusText() == QStringLiteral("Interfaces found: DIAG *2"));
		CHECK(controller.deviceLabels()[0].contains("USB IF 0"));
		CHECK(controller.deviceLabels()[0].contains("45B448E1"));
		CHECK(controller.deviceLabels()[1].contains("/dev/ttyUSB1"));
		auto adb = diag; adb["key"] = "adb"; adb["qualcommMode"] = QTQ_ADB;
		auto unknown = diag; unknown["key"] = "unknown"; unknown.remove("qualcommMode");
		update({adb, diag, second, unknown});
		CHECK(controller.deviceStatusText() == QStringLiteral(
			"Interfaces found: ADB *1, DIAG *2, Unknown mode *1"));
		controller.language()->setMode("zh");
		CHECK(controller.deviceStatusText() == QStringLiteral("发现接口：ADB *1、DIAG *2、未知模式 *1"));
		controller.language()->setMode("en");
		CHECK(controller.deviceStatusText() == QStringLiteral(
			"Interfaces found: ADB *1, DIAG *2, Unknown mode *1"));
		auto edl = diag; edl["qualcommMode"] = QTQ_EDL; edl["pid"] = 0x9008;
		update({edl});
		CHECK(controller.deviceStatusText() == QStringLiteral("Interfaces found: EDL *1"));
		auto dump = edl; dump["qualcommMode"] = QTQ_DUMP;
		update({dump});
		CHECK(controller.deviceStatusText() == QStringLiteral("Interfaces found: Crash-dump candidate *1"));
		update({diag, second});
		controller.setBatchTargets({diag["key"].toString(), second["key"].toString()}, true);
		CHECK(controller.deviceStatusText() == QStringLiteral("Batch: 2 selected / 2 current"));
		controller.setDeviceIndex(0);
		update({});
		CHECK(controller.deviceStatusText() == QStringLiteral("No device found"));
		controller.language()->setMode("auto");
	}

	// A Qualcomm device can re-enumerate into another mode (DIAG 0x9091 -> EDL
	// 0x9008) on the same USB port, keeping index/mode/key but changing
	// vid/pid/serial. The cached list must be rebuilt so the next task does not
	// target the pre-switch identity and get rejected as disconnected.
	{
		gui::AppController controller(NULL);
		controller.setVendor(2);
		QVariantMap diag{{"vendor", 2}, {"index", 1}, {"mode", "EDL / USB"},
		                 {"key", "qualcomm:0::1-3"}, {"vid", 0x05c6}, {"pid", 0x9091}, {"qualcommMode", QTQ_DIAG},
		                 {"serial", "45B448E1"}, {"bus", 1}, {"ports", QVariantList{3}}};
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
			Q_ARG(QVariantList, QVariantList{diag})));
		CHECK(controller.devices().size() == 1);
		CHECK(controller.devices()[0].toMap()["serial"].toString() == "45B448E1");
		// A DIAG-mode unit is listed but is not an EDL target yet.
		CHECK(!controller.qualcommEligibility()[QString::number(int(core::Operation::QualcommFlash))].toMap()["allowed"].toBool());
		// The dropdown shows the identity up front so the switch is visible.
		CHECK(controller.deviceLabels().first().contains("05c6:9091"));
		CHECK(controller.deviceLabels().first().contains("SN:45B448E1"));
		QVariantMap edl = diag; edl["pid"] = 0x9008; edl["qualcommMode"] = QTQ_EDL; edl["serial"] = QString();
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
			Q_ARG(QVariantList, QVariantList{edl})));
		CHECK(controller.devices().size() == 1);
		CHECK(controller.devices()[0].toMap()["serial"].toString().isEmpty());
		CHECK(controller.devices()[0].toMap()["pid"].toUInt() == 0x9008);
		CHECK(controller.qualcommEligibility()[QString::number(int(core::Operation::QualcommFlash))].toMap()["allowed"].toBool());
		CHECK(controller.deviceLabels().first().contains("05c6:9008"));
		CHECK(!controller.deviceLabels().first().contains("SN:"));
	}

    {
        gui::AppController controller(nullptr); controller.setVendor(2);
        auto allowed=[&](core::Operation operation) {
            return controller.qualcommEligibility()[QString::number(int(operation))].toMap()["allowed"].toBool();
        };
        auto update=[&](const QVariantList &devices) {
            CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList,devices)));
        };
        QVariantMap diag{{"vendor",2},{"index",1},{"key","diag"},{"vid",0x05c6},{"pid",0x9091},{"qualcommMode",QTQ_DIAG}};
        auto edl=diag; edl["key"]="edl"; edl["index"]=2; edl["pid"]=0x9008; edl["qualcommMode"]=QTQ_EDL;
        update({diag,edl}); controller.setDeviceIndex(0);
        CHECK(!allowed(core::Operation::QualcommFlash));
        int requests=0;
        QObject::connect(&controller,&gui::AppController::requestExecute,[&](const core::TaskJob &){++requests;});
        core::OperationRequest request; request.vendor=core::Vendor::Qualcomm; request.operation=core::Operation::QualcommFlash;
        controller.qualcomm()->executeRequested(request);
        CHECK(requests==0 && !controller.busy());
        controller.setDeviceIndex(1); CHECK(allowed(core::Operation::QualcommFlash));
        controller.setBatchTargets({"diag","edl"},true);
        CHECK(!allowed(core::Operation::QualcommFlash));
        controller.qualcomm()->executeRequested(request); CHECK(requests==0);
        update({edl}); CHECK(controller.batchMode()); CHECK(!allowed(core::Operation::QualcommFlash));
        controller.qualcomm()->executeRequested(request); CHECK(requests==0);
        update({diag,edl});
        controller.setDeviceIndex(1);
        edl["accessError"]=-3; update({diag,edl});
        CHECK(!allowed(core::Operation::QualcommFlash));
        edl["accessError"]=0; update({diag,edl}); CHECK(allowed(core::Operation::QualcommFlash));
        auto dump=edl; dump["pid"]=0x900e; dump["qualcommMode"]=QTQ_DUMP;
        update({diag,dump}); CHECK(!allowed(core::Operation::QualcommFlash));
        CHECK(allowed(core::Operation::QualcommRamdump));
        CHECK(!allowed(core::Operation::QualcommReset));
        update({diag}); CHECK(controller.deviceIndex()==-1); CHECK(!allowed(core::Operation::QualcommRamdump));
    }

    {
        gui::AppController controller(nullptr); controller.setVendor(2);
        auto update=[&](const QVariantList &devices) {
            CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList,devices)));
        };
        QVariantMap diag{{"vendor",2},{"key","diag"},{"qualcommMode",QTQ_DIAG},{"vid",0x05c6},{"pid",0x9091}};
        auto edl=diag; edl["key"]="edl"; edl["qualcommMode"]=QTQ_EDL; edl["pid"]=0x9008;
        edl["transport"]=0; edl["bus"]=1; edl["ports"]=QVariantList{2};
        edl["usbAddress"]=9; edl["interfaceNumber"]=0; edl["alternate"]=0;
        gui::EdlEndpoint expected; expected.vid=0x05c6; expected.pid=0x9008; expected.downloadInterface=true;
        expected.bus=1; expected.ports={2}; expected.address=9; expected.interfaceNumber=0;
        // A Linux serial alias must match the USB interface through topology.
        expected.path="/dev/ttyUSB0";
        update({diag}); controller.edl()->finished(true,"detected",expected);
        auto stale=edl; stale["usbAddress"]=8; update({diag,stale}); CHECK(controller.deviceIndex()==0);
        update({diag,edl}); CHECK(controller.deviceIndex()==1);
        controller.setDeviceIndex(0);
        // An ambiguous follow-up scan must not select the first candidate.
        controller.edl()->finished(true,"detected",expected);
        auto duplicate=edl; duplicate["key"]="duplicate";
        update({diag,edl,duplicate}); CHECK(controller.deviceIndex()==0);
        expected.path="COM8"; expected.instance="USB-instance";
        expected.ports.clear(); expected.address=0; expected.interfaceNumber=-1;
        edl["transport"]=2; edl["devicePath"]="\\\\.\\COM8"; edl["deviceInstance"]="USB-instance";
        update({diag}); controller.edl()->finished(true,"detected",expected);
        auto recycled=edl; recycled["deviceInstance"]="replacement";
        update({diag,recycled}); CHECK(controller.deviceIndex()==0);
        update({diag,edl}); CHECK(controller.deviceIndex()==1);
    }

	// Changing the effective language retranslates the UI, which rebuilds the
	// language combo box model and resets its currentIndex. The settings dialog
	// restores the selection from modeChanged, so it must be emitted after
	// languageChanged.
	{
		gui::AppController controller(NULL);
		QStringList order;
		QObject::connect(controller.language(), &gui::LanguageManager::languageChanged,
		                 &application, [&order] { order << QStringLiteral("language"); });
		QObject::connect(controller.language(), &gui::LanguageManager::modeChanged,
		                 &application, [&order] { order << QStringLiteral("mode"); });
		controller.language()->setMode("en");
		controller.language()->setMode("zh_CN");
		CHECK(order.endsWith(QStringLiteral("mode")));
		CHECK(order.indexOf(QStringLiteral("language")) < order.lastIndexOf(QStringLiteral("mode")));
		controller.language()->setMode("auto");
	}

	// Every cached C++ string (page hints, firmware table, config status) is
	// rebuilt through LanguageManager's registry, so switching the language
	// updates them immediately rather than only on the next action.
	{
		gui::AppController controller(NULL);
		controller.language()->setMode("zh_CN");
		const QString zhAdvanced = controller.advancedHint();
		controller.setImageDir(temporary.path());                       // no parameter.txt -> error hint
		const QString zhImage = controller.imageHint();
		controller.setFirmware(temporary.filePath("missing.img"));      // unparsable -> invalid hint
		const QString zhFirmware = controller.firmwareHint();
		controller.config()->load();
		const QString zhStatus = controller.config()->status();
		const QVariantList zhPlaces = controller.standardPlaces();
		CHECK(!zhAdvanced.isEmpty() && !zhImage.isEmpty() && !zhFirmware.isEmpty() && !zhStatus.isEmpty());
		CHECK(!zhPlaces.isEmpty());

		controller.language()->setMode("en");
		CHECK(controller.advancedHint() != zhAdvanced);
		CHECK(controller.imageHint() != zhImage);
		CHECK(controller.firmwareHint() != zhFirmware);
		CHECK(controller.config()->status() != zhStatus);
		CHECK(controller.standardPlaces().first().toMap()["name"]
		      != zhPlaces.first().toMap()["name"]);
		controller.language()->setMode("auto");
	}

	// DirModel is created in QML; it registers itself the same way and rebuilds
	// its cached error text through the same registry.
	{
		gui::LanguageManager language;
		gui::DirModel dir;
		language.registerRetranslatable(&dir);
		language.setMode("zh_CN");
		CHECK(!dir.makeDirectory(".."));                                // invalid name -> translated error
		const QString zhError = dir.errorText();
		CHECK(!zhError.isEmpty());
		language.setMode("en");
		CHECK(!dir.errorText().isEmpty());
		CHECK(dir.errorText() != zhError);
		language.setMode("auto");
	}

	// Closing immediately with a queued job must join the thread without a deadlock.
	{
		gui::AppController controller(NULL);
		controller.runOperation(gui::Operations::FirmwareInfo, {{"path", temporary.filePath("missing.img")}});
	}
	libusb_exit(NULL);
	std::printf("test_gui_backend: %s\n", failures ? "FAIL" : "PASS"); return failures ? 1 : 0;
}
