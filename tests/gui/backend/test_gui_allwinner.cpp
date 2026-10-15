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

int TestAllwinnerSwitching([[maybe_unused]] QGuiApplication &application) {
    int failures = 0;
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
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
			CHECK(!controller.allwinner()->valid());
			controller.switchToFel(); CHECK(controller.tasks()->busy()); CHECK(controller.switching());
			controller.setVendor(0); CHECK(controller.vendor() == 1);
			CHECK(Await([&] { return !controller.tasks()->busy(); }));
			CHECK(!controller.switching());
			CHECK(controller.felSwitchError() && controller.felSwitchHint().contains("BusyBox"));
			controller.switchToFel(1); CHECK(controller.tasks()->busy()); CHECK(controller.switching());
			controller.cancel(); CHECK(!controller.tasks()->busy()); CHECK(!controller.switching());
			CHECK(controller.felSwitchError());
		}
		if (originalAdb.isNull()) qunsetenv("ADB"); else qputenv("ADB", originalAdb);
	}
#endif
    return failures;
}

int TestAllwinnerState([[maybe_unused]] QGuiApplication &application) {
    int failures = 0;
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());

    {
        // Exercise result routing without issuing any USB requests.
        gui::AppController controller(NULL);
        QObject::disconnect(&controller, &gui::AppController::requestExecute, nullptr, nullptr);
        controller.setVendor(1);
        core::TaskJob captured;
        int submissions=0;
        QObject::connect(&controller, &gui::AppController::requestExecute, &application,
            [&](const core::TaskJob &job) { captured=job; ++submissions; });
        auto state=[&](int context) { return controller.allwinner()->states()[context].toMap(); };
        auto finish=[&](core::OperationStatus status=core::OperationStatus::Succeeded) {
            core::OperationResult result;
            result.operation=captured.steps[0].operation(); result.status=status;
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
        controller.allwinner()->setContextFirmware(1,"flash.img");
        controller.allwinner()->setContextFirmware(0,"boot.img");
        controller.allwinner()->setContextFirmware(2,"tools.img");
        // Automatic firmware inspection is synchronous and silent: no job, no busy state.
        CHECK(!controller.tasks()->busy() && submissions==0);
        CHECK(state(0)["firmware"]=="boot.img" && state(1)["firmware"]=="flash.img" && state(2)["firmware"]=="tools.img");
        CHECK(!state(0)["valid"].toBool() && !state(1)["valid"].toBool() && !state(2)["valid"].toBool());
        const auto summary=controller.allwinner()->results();
        CHECK(summary->count()==0);
        // Explicit inspection still routes results through the worker.
        controller.allwinner()->run(gui::Operations::AllwinnerInspect,{{"context",1}});
        CHECK(controller.tasks()->busy() && captured.targets.empty()); finish();
        controller.allwinner()->run(gui::Operations::AllwinnerInspect,{{"context",0}}); finish();
        controller.allwinner()->run(gui::Operations::AllwinnerInspect,{{"context",2}}); finish();
        CHECK(state(0)["valid"].toBool() && state(1)["valid"].toBool() && state(2)["valid"].toBool());
        CHECK(summary->count()==1);
        QVariantMap unrelated{{"context",2},{"logicOffset","-1"},{"norMiB","bad"},{"target",99},{"mode",99}};
        unrelated["output"]=temporary.filePath("unpacked");
        controller.allwinner()->run(gui::Operations::AllwinnerExtract,unrelated);
        CHECK(controller.tasks()->busy() && captured.targets.empty()); finish(core::OperationStatus::Failed);
        CHECK(state(2)["valid"].toBool() && state(2)["error"].toBool() && summary->count()==1);
        const QString flashHint=state(1)["hint"].toString();
        QVariantMap device{{"vendor",1},{"index",1},{"key","allwinner:1-5"},{"bus",1},{"ports",QVariantList{5}}};
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(QVariantList{device}))));
        controller.allwinner()->run(gui::Operations::AllwinnerDeviceInfo,unrelated);
        CHECK(controller.tasks()->busy() && captured.targets.size()==1); finish();
        CHECK(state(1)["hint"].toString()==flashHint && summary->count()==1);
        CHECK(state(3)["results"].value<gui::KeyValueModel *>()->count()==1);
        // Reboot needs no firmware path and targets the selected device.
        controller.allwinner()->run(gui::Operations::AllwinnerReboot);
        CHECK(controller.tasks()->busy() && captured.targets.size()==1
            && captured.steps[0].operation()==core::Operation::AllwinnerReboot); finish();
        int before=submissions;
        controller.allwinner()->run(gui::Operations::AllwinnerFlash,{{"mode",0}});
        CHECK(submissions==before && !controller.tasks()->busy()); // Selection is mandatory in the GUI API.
        controller.allwinner()->run(gui::Operations::AllwinnerFlash,{{"mode",0},{"partitions",QStringList{"rootfs"}},{"logicOffset","bad"}});
        CHECK(controller.tasks()->busy() && captured.steps[0].allwinner().partitions==std::vector<std::string>{"rootfs"}); finish();
        controller.allwinner()->run(gui::Operations::AllwinnerRaw,{{"context",0},{"rawImage","disk.img"},{"rawMode",0},{"logicOffset","bad"}});
        CHECK(controller.tasks()->busy() && !captured.steps[0].allwinner().hasLogicOffset); finish(core::OperationStatus::Cancelled);
        CHECK(state(0)["hint"].toString()==QString::fromUtf8("Cancelled") && !state(0)["error"].toBool());
        before=submissions;
        controller.allwinner()->run(gui::Operations::AllwinnerRaw,{{"context",0},{"rawImage","disk.img"},{"rawMode",1},{"logicOffset","bad"}});
        controller.allwinner()->run(gui::Operations::AllwinnerConvert,{{"context",2},{"output","out.img"},{"logicOffset","-1"}});
        CHECK(submissions==before && !controller.tasks()->busy());
        controller.allwinner()->run(gui::Operations::AllwinnerConvert,{{"context",2},{"output","out.img"},{"target",2},{"norMiB","bad"},{"ubootOffset","bad"}});
        CHECK(controller.tasks()->busy() && captured.targets.empty()); finish();
        CHECK(state(2)["summary"].value<gui::KeyValueModel *>()->count()==1);
        CHECK(summary->count()==1 && state(1)["valid"].toBool());
        controller.allwinner()->setContextFirmware(2,"replacement.img");
        CHECK(!state(2)["valid"].toBool() && state(2)["partitions"].toList().empty());
        CHECK(state(2)["summary"].value<gui::KeyValueModel *>()->count()==0);
        CHECK(state(2)["results"].value<gui::KeyValueModel *>()->count()==0);
        CHECK(state(1)["valid"].toBool() && state(0)["firmware"]=="boot.img");
        before=submissions;
        controller.allwinner()->setContextFirmware(2,"replacement.img");
        CHECK(!controller.tasks()->busy() && submissions==before); // Same missing path is re-inspected synchronously.
        CHECK(state(2)["firmware"]=="replacement.img" && !state(2)["valid"].toBool());
        gui::AppController restored(NULL);
        CHECK(restored.allwinner()->states()[0].toMap()["firmware"]=="boot.img");
        CHECK(restored.allwinner()->states()[1].toMap()["firmware"]=="flash.img");
        CHECK(restored.allwinner()->states()[2].toMap()["firmware"]=="replacement.img");
    }
    {
        QSettings settings(QSettings::IniFormat,QSettings::UserScope,"upgrade_tool","upgrade_tool");
        settings.beginGroup("allwinner"); settings.remove("");
        settings.setValue("firmware","legacy.img");
        settings.setValue("page0/rawImage","legacy-disk.img");
        settings.setValue("page1/verify",false);
        settings.setValue("page2/logicOffset","0x2000"); settings.sync();
        gui::AppController legacy(NULL);
        for(const auto &value:legacy.allwinner()->states().mid(0,3)) CHECK(value.toMap()["firmware"]=="legacy.img");
        CHECK(legacy.allwinner()->settings()["page0/rawImage"]=="legacy-disk.img");
        CHECK(!legacy.allwinner()->settings()["page1/verify"].toBool());
        CHECK(legacy.allwinner()->settings()["page2/logicOffset"]=="0x2000");
    }

    return failures;
}
