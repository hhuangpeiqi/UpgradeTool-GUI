#include "tests/support/qt/TestDevices.h"
#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/QtObserver.h"
#include "gui/OperationSpec.h"
#include "gui/AdbRunner.h"
#include "gui/UsbAccess.h"
#include "gui/backend/QualcommController.h"
#include "backends/qualcomm/QudError.h"
#include "platform/FileLog.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QSettings>
#include <QScopeGuard>
#include <libusb.h>
#include <thread>
#include <atomic>
#include <cstdio>
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include "BackendTests.h"

int TestDeviceStatus([[maybe_unused]] QGuiApplication &application) {
    int failures = 0;
    QTemporaryDir temporary;
    const bool hadAdb = qEnvironmentVariableIsSet("ADB");
    const QByteArray previousAdb = qgetenv("ADB");
    const auto restoreAdb = qScopeGuard([&] {
        if (hadAdb) qputenv("ADB", previousAdb);
        else qunsetenv("ADB");
    });
    // Keep initial availability asynchronous even when the host has no ADB.
    // Availability and device lists below are supplied explicitly by the test.
    qputenv("ADB", temporary.filePath("missing-adb").toUtf8());
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());

	{
		// Status uses the same ADB discovery for both vendors; flash modes take priority.
		QTemporaryDir logDirectory;
		platform::FileLog disk(logDirectory.path().toStdString(), "adb-", true);
		gui::AppController controller(&disk);
		auto *adb = controller.findChild<gui::AdbRunner *>();
		CHECK(adb != nullptr);
		if (adb) adb->Stop();
		if (adb) adb->traceMessage(QStringLiteral("test-disk-trace"));
		controller.language()->setMode("en");
		// Stop clears recovery state without publishing a new availability event.
		// Supply the initial state explicitly, as with subsequent updates below.
		CHECK(QMetaObject::invokeMethod(&controller, "onAdbAvailabilityChanged", Qt::DirectConnection,
			Q_ARG(bool, false), Q_ARG(QString, QString())));
		for (int vendor : {0, 1}) {
			controller.setVendor(vendor);
			CHECK(controller.deviceStatusText() == QString::fromUtf8("No device found"));
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
			CHECK(controller.deviceStatusText() == QString::fromUtf8("No device found"));
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
			Q_ARG(QVariantList, SnapshotDevices(devices))));
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found Loader device"));
		controller.setVendor(1);
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found FEL/FES device"));
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
			Q_ARG(QVariantList, SnapshotDevices(QVariantList()))));
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found 2 ADB device(s)"));
		controller.setVendor(0);
		CHECK(controller.deviceStatusText() == QString::fromUtf8("Found 2 ADB device(s)"));
		QByteArray diskContent;
		for (const auto &name : QDir(logDirectory.path()).entryList(QDir::Files)) {
			QFile file(logDirectory.filePath(name));
			CHECK(file.open(QIODevice::ReadOnly));
			diskContent += file.readAll();
		}
		CHECK(diskContent.contains("ADB monitoring: app_version="));
		CHECK(diskContent.contains("[ADB] test-disk-trace"));
		CHECK(diskContent.contains("ADB availability: available=0"));
		CHECK(diskContent.contains("ADB availability: available=1"));
	}
	// Vendor changes must refresh empty lists even when ADB never emits again.
	{
		gui::AppController controller(nullptr);
		controller.findChild<gui::AdbRunner *>()->Stop();
		controller.language()->setMode("en");
		const QVariantMap diag{{"vendor", 2}, {"index", 1}, {"qualcommMode", QTQ_DIAG},
			{"mode", "DIAG / USB IF 0"}, {"key", "qualcomm:0::1-3:if0:alt0"}};
		auto update = [&](const QVariantList &devices) {
			CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
				Q_ARG(QVariantList, SnapshotDevices(devices))));
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
					? (qualcomm ? QStringLiteral("Interfaces found: DIAG *1") : adbStatus)
					: flashDevices ? (vendor == 0 ? QStringLiteral("Found Loader device")
						: QStringLiteral("Found FEL/FES device")) : adbStatus;
				CHECK(controller.deviceStatusText() == expected);
			}
		};
		update({diag});
		cycle(QStringLiteral("No device found"));
		availability(false);
		cycle(QStringLiteral("No device found"));
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
	{
		// Startup/recovery is a status, not an error toast. Translate it on demand.
		gui::AppController controller(nullptr);
		controller.language()->setMode("en");
		controller.setVendor(0);
		CHECK(controller.deviceStatusText() == QStringLiteral("Starting ADB…"));
		controller.language()->setMode("zh_CN");
		CHECK(controller.deviceStatusText() == QStringLiteral("正在启动 ADB…"));
		controller.language()->setMode("en");
		int errors = 0;
		QObject::connect(&controller, &gui::AppController::toastRequested,
		                 [&](const QString &, bool error) { if (error) ++errors; });
		CHECK(Await([&] { return errors == 1; }));
		CHECK(controller.deviceStatusText() == QStringLiteral("ADB connection interrupted; retrying automatically…"));
		controller.language()->setMode("zh_CN");
		CHECK(controller.deviceStatusText() == QStringLiteral("ADB 连接异常，正在自动重试…"));
		auto availability = [&](bool available, const QString &message) {
			CHECK(QMetaObject::invokeMethod(&controller, "onAdbAvailabilityChanged", Qt::DirectConnection,
				Q_ARG(bool, available), Q_ARG(QString, message)));
		};
		availability(false, {});
		availability(false, QStringLiteral("connection reset"));
		CHECK(errors == 1);
		availability(true, {});
		availability(false, QStringLiteral("another outage"));
		CHECK(errors == 2);
		controller.language()->setMode("auto");
	}

    return failures;
}

int TestQualcommDevices([[maybe_unused]] QGuiApplication &application) {
    int failures = 0;
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());

	// Status counts interfaces by semantic mode, independently of their details.
	{
		gui::AppController controller(nullptr);
		controller.findChild<gui::AdbRunner *>()->Stop();
		controller.language()->setMode("en");
		controller.setVendor(2);
		auto update = [&](const QVariantList &devices) {
			CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
				Q_ARG(QVariantList, SnapshotDevices(devices))));
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
			Q_ARG(QVariantList, SnapshotDevices(QVariantList{diag}))));
		CHECK(controller.devices().size() == 1);
		CHECK(controller.devices()[0].toMap()["serial"].toString() == "45B448E1");
		// A DIAG-mode unit is listed but is not an EDL target yet.
		CHECK(!controller.qualcommEligibility()[QString::number(int(core::Operation::QualcommFlash))].toMap()["allowed"].toBool());
		// The dropdown shows the identity up front so the switch is visible.
		CHECK(controller.deviceLabels().first().contains("05c6:9091"));
		CHECK(controller.deviceLabels().first().contains("SN:45B448E1"));
		QVariantMap edl = diag; edl["pid"] = 0x9008; edl["qualcommMode"] = QTQ_EDL; edl["serial"] = QString();
		CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
			Q_ARG(QVariantList, SnapshotDevices(QVariantList{edl}))));
		CHECK(controller.devices().size() == 1);
		CHECK(controller.devices()[0].toMap()["serial"].toString().isEmpty());
		CHECK(controller.devices()[0].toMap()["pid"].toUInt() == 0x9008);
		CHECK(controller.qualcommEligibility()[QString::number(int(core::Operation::QualcommFlash))].toMap()["allowed"].toBool());
		CHECK(controller.deviceLabels().first().contains("05c6:9008"));
		CHECK(!controller.deviceLabels().first().contains("SN:"));
	}

    {
        gui::AppController controller(nullptr); controller.setVendor(2);
        controller.language()->setMode("en");
        auto update = [&](const QVariantList &devices) {
            CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                Q_ARG(QVariantList, SnapshotDevices(devices))));
        };
        QVariantMap diag{{"vendor",2},{"key","diag"},{"vid",0x05c6},{"pid",0x9091},{"qualcommMode",QTQ_DIAG}};
        auto usb = diag; usb["key"] = "usb"; usb["pid"] = 0x9008; usb["qualcommMode"] = QTQ_EDL;
        usb["transport"] = 0; usb["accessError"] = LIBUSB_ERROR_NOT_SUPPORTED;
        auto com = usb; com["key"] = "com"; com["transport"] = 2; com["accessError"] = 0;
        com["devicePath"] = "\\\\.\\COM52"; com["deviceInstance"] = "test-QDLoader";
        update({diag, usb, com});
#ifdef Q_OS_WIN
        CHECK(controller.deviceIndex() == 1);
        CHECK(controller.deviceModel()->keys() == QStringList({"diag", "com"}));
        CHECK(controller.deviceModel()->allRows().size() == 3);
        update({com, usb, diag}); CHECK(controller.deviceIndex() == 0);
        controller.setDeviceIndex(0); // Keep this target if it disappears.
        update({diag}); CHECK(controller.deviceIndex() == -1);
        auto other = com; other["key"] = "other";
        update({diag, other}); CHECK(controller.deviceIndex() == -1);
        update({diag, com}); CHECK(controller.deviceIndex() == 1);
#else
        CHECK(controller.deviceIndex() == 2);
        update({com, usb, diag}); CHECK(controller.deviceIndex() == 0);
        // An explicit selection is retained even when its transport cannot open.
        controller.setDeviceIndex(1); update({diag, com, usb}); CHECK(controller.deviceIndex() == 2);
        const auto eligibility = controller.qualcommEligibility()[QString::number(int(core::Operation::QualcommFlash))].toMap();
        CHECK(!eligibility["allowed"].toBool()); CHECK(eligibility["reason"].toString().contains("QDLoader"));
        update({diag}); CHECK(controller.deviceIndex() == -1);
        update({diag, com}); CHECK(controller.deviceIndex() == -1);
#endif
        // No serial-port probe: an intentionally nonexistent COM path is left
        // to the real transport's identity validation/open, without touching hardware.
        const auto reference = SnapshotDevices({com}).first().toMap()["reference"].value<core::DeviceRef>();
        CHECK(gui::CheckUsbAccess(reference).isEmpty());
        core::OperationResult result; result.operation = core::Operation::QualcommFlash;
        result.diagnosticMessage = core::qualcomm::QudErrorMessage("\\\\.\\COM52", false, 5, "Access is denied");
        result.diagnostic = result.diagnosticMessage.English();
        CHECK(QMetaObject::invokeMethod(&controller, "onOperationResult", Qt::DirectConnection, Q_ARG(core::OperationResult, result)));
        CHECK(controller.qualcomm()->operationErrorFor("flash").contains("COM52"));
        CHECK(controller.qualcomm()->operationErrorFor("flash").contains("Windows error 5"));
        controller.language()->setMode("zh_CN");
        CHECK(controller.qualcomm()->operationErrorFor("flash").contains(QStringLiteral("访问被拒绝")));
        CHECK(controller.qualcomm()->operationErrorFor("flash").contains("COM52"));
        controller.language()->setMode("en");
        CHECK(controller.qualcomm()->operationErrorFor("flash").contains("opening failed"));
    }
    {
        gui::AppController controller(nullptr); controller.setVendor(2);
        QVariantMap usb{{"vendor",2},{"key","usb"},{"vid",0x05c6},{"pid",0x9008},
            {"qualcommMode",QTQ_EDL},{"transport",0},{"accessError",0}};
        auto other = usb; other["key"] = "other";
        CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
            Q_ARG(QVariantList, SnapshotDevices({usb, other}))));
#ifdef Q_OS_WIN
        CHECK(controller.deviceIndex() == -1);
        controller.setDeviceIndex(0);
#else
        CHECK(controller.deviceIndex() == 0);
#endif
        CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
            Q_ARG(QVariantList, SnapshotDevices({other}))));
        CHECK(controller.deviceIndex() == -1);
        // Further updates must not silently target another physical device.
        other["index"] = 2;
        CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
            Q_ARG(QVariantList, SnapshotDevices({other}))));
        CHECK(controller.deviceIndex() == -1);
    }
    {
        gui::AppController controller(nullptr); controller.setVendor(2);
        auto allowed=[&](core::Operation operation) {
            return controller.qualcommEligibility()[QString::number(int(operation))].toMap()["allowed"].toBool();
        };
        auto update=[&](const QVariantList &devices) {
            CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(devices))));
        };
        QVariantMap diag{{"vendor",2},{"index",1},{"key","diag"},{"vid",0x05c6},{"pid",0x9091},{"qualcommMode",QTQ_DIAG}};
        auto edl=diag; edl["key"]="edl"; edl["index"]=2; edl["pid"]=0x9008; edl["qualcommMode"]=QTQ_EDL;
        update({diag,edl}); controller.setDeviceIndex(0);
        CHECK(!allowed(core::Operation::QualcommFlash));
        int requests=0;
        QObject::connect(&controller,&gui::AppController::requestExecute,[&](const core::TaskJob &){++requests;});
        core::OperationRequest request;  request=core::OperationRequest(core::Operation::QualcommFlash);
        controller.qualcomm()->executeRequested(request);
        CHECK(requests==0 && !controller.tasks()->busy());
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
            CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(devices))));
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
        update({diag,edl,duplicate}); CHECK(controller.deviceIndex()==0);
        update({diag,edl}); CHECK(controller.deviceIndex()==1);
        controller.setDeviceIndex(0);
        expected.path="COM8"; expected.instance="USB-instance";
        expected.ports.clear(); expected.address=0; expected.interfaceNumber=-1;
        edl["transport"]=2; edl["devicePath"]="\\\\.\\COM8"; edl["deviceInstance"]="USB-instance";
        update({diag}); controller.edl()->finished(true,"detected",expected);
        auto recycled=edl; recycled["deviceInstance"]="replacement";
        update({diag,recycled}); CHECK(controller.deviceIndex()==0);
        update({diag,edl}); CHECK(controller.deviceIndex()==1);
    }

#ifdef Q_OS_WIN
    // Exercise polling sequences, not just complete initial snapshots.
    {
        const QVariantMap diag{{"vendor",2},{"key","diag"},{"vid",0x05c6},{"pid",0x9091},
            {"qualcommMode",QTQ_DIAG},{"transport",0},{"accessError",0}};
        auto usb = diag; usb["key"]="usb"; usb["pid"]=0x9008; usb["qualcommMode"]=QTQ_EDL;
        usb["accessError"]=LIBUSB_ERROR_NOT_SUPPORTED;
        auto com = usb; com["key"]="com"; com["transport"]=2; com["accessError"]=0;
        com["devicePath"]="COM8"; com["deviceInstance"]="instance-COM8";
        auto other = com; other["key"]="other"; other["devicePath"]="COM9";
        other["deviceInstance"]="instance-COM9";

        for (int scenario = 0; scenario < 10; ++scenario) {
            gui::AppController controller(nullptr); controller.setVendor(2);
            auto update = [&](const QVariantList &devices) {
                CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,
                    Q_ARG(QVariantList, SnapshotDevices(devices))));
            };
            auto selected = [&] {
                return controller.deviceModel()->keys().value(controller.deviceIndex());
            };
            if (scenario == 0) {
                // A rejected first row must never become a remembered target.
                for (int error : {LIBUSB_ERROR_NOT_SUPPORTED, LIBUSB_ERROR_ACCESS, LIBUSB_ERROR_NO_DEVICE}) {
                    auto denied = usb; denied["accessError"]=error;
                    update({denied});
                    CHECK(controller.devices().isEmpty() && controller.deviceIndex() == -1);
                    CHECK(!controller.hasDevice() && controller.deviceLabels().isEmpty());
                    CHECK(controller.deviceModel()->allRows().size() == 1);
                }
                update({usb, com}); CHECK(selected() == "com");
            } else if (scenario == 1) {
                auto adb = diag; adb["key"]="adb"; adb["qualcommMode"]=QTQ_ADB;
                auto unknown = diag; unknown["key"]="unknown"; unknown["qualcommMode"]=QTQ_UNKNOWN;
                update({usb, diag, adb, unknown});
                CHECK(controller.deviceModel()->keys() == QStringList({"diag","adb","unknown"}));
                CHECK(selected() == "diag");
                update({usb, diag, adb, unknown, com}); CHECK(selected() == "com");
                update({com, unknown, diag, adb, usb}); CHECK(selected() == "com");
            } else if (scenario == 2) {
                update({diag}); CHECK(selected() == "diag");
                controller.setDeviceIndex(0); // Activating the same row is manual.
                update({diag, com}); CHECK(selected() == "diag");
                update({com, diag}); CHECK(selected() == "diag");
                controller.setVendor(0); controller.setVendor(2); CHECK(selected() == "diag");
            } else if (scenario == 3) {
                update({diag}); controller.setVendor(0); controller.setVendor(2);
                update({diag, com}); CHECK(selected() == "com");
            } else if (scenario == 4) {
                update({com}); CHECK(selected() == "com");
                update({other}); CHECK(controller.deviceIndex() == -1);
                controller.setVendor(0); controller.setVendor(2);
                update({diag, other}); CHECK(controller.deviceIndex() == -1);
                update({other, com}); CHECK(selected() == "com");
            } else if (scenario == 5) {
                update({diag});
                controller.tasks()->setBusy(true);
                update({com, diag}); CHECK(selected() == "diag");
                CHECK(controller.deviceModel()->allRows().size() == 2);
                controller.tasks()->setBusy(false); CHECK(selected() == "com");
            } else if (scenario == 6) {
                update({com, other}); CHECK(controller.deviceIndex() == -1);
                update({other, com}); CHECK(controller.deviceIndex() == -1);
                update({com}); CHECK(selected() == "com");
            } else if (scenario == 7) {
                update({diag});
                update({diag, com, other}); CHECK(selected() == "diag");
                update({diag, other}); CHECK(selected() == "other");
            } else if (scenario == 8) {
                update({com}); controller.setDeviceIndex(0);
                auto denied = com; denied["accessError"]=LIBUSB_ERROR_ACCESS;
                update({denied, other}); CHECK(controller.deviceIndex() == -1);
                controller.setVendor(0); controller.setVendor(2);
                CHECK(controller.deviceIndex() == -1);
                update({other, com}); CHECK(selected() == "com");
            } else {
                gui::EdlEndpoint expected; expected.vid=0x05c6; expected.pid=0x9008;
                expected.downloadInterface=true; expected.path="COM8"; expected.instance="instance-COM8";
                controller.edl()->finished(true,"detected",expected);
                auto denied = com; denied["accessError"]=LIBUSB_ERROR_NOT_SUPPORTED;
                update({denied, other}); CHECK(controller.deviceIndex() == -1);
                update({other}); CHECK(controller.deviceIndex() == -1);
                update({com, other}); CHECK(selected() == "com");
            }
        }
    }
#endif

    return failures;
}
