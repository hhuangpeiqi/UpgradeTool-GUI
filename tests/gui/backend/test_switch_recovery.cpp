#include "gui/AdbRunner.h"
#include "gui/backend/AppController.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <libusb.h>

int main(int argc, char **argv) {
    // Fake ADB keeps selection pending; no reboot reaches real hardware.
    if (argc > 1) {
        const auto command = QString::fromLocal8Bit(argv[1]);
        if (command == "version") std::puts("Android Debug Bridge version 1.0.41");
        else if (command == "devices") std::puts("List of devices attached\nboard device\nsecond device");
        else if (command != "start-server") return 42;
        return 0;
    }
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QGuiApplication application(argc, argv);
    qputenv("ADB", application.applicationFilePath().toUtf8());
    if (libusb_init(nullptr) < 0) return 2;
    int failures = 0;
    {
        enum Scenario { CommandFailure, Timeout, Success, Cancel, Shutdown };
        std::atomic<Scenario> scenario{CommandFailure};
        gui::EdlEndpoint source;
        source.id = "adb:board"; source.kind = gui::EdlEndpoint::Adb;
        source.serial = "board"; source.location = "usb:1-2";
        auto target = source;
        target.id = "edl"; target.kind = gui::EdlEndpoint::Usb;
        target.vid = 0x05c6; target.pid = 0x9008; target.downloadInterface = true;
        gui::EdlHooks hooks;
        hooks.discover = [source](const QString &, const gui::EdlCancel &, QString &) {
            return QVector<gui::EdlEndpoint>{source};
        };
        hooks.reboot = [&](const QString &, const QString &, const gui::EdlCancel &cancel, QString &error) {
            if (scenario == Cancel || scenario == Shutdown) {
                while (!cancel->stop_requested()) QThread::msleep(1);
                return false;
            }
            error = "Test command failure";
            return scenario != CommandFailure;
        };
        hooks.scan = [&] {
            return scenario == Success ? QVector<gui::EdlEndpoint>{target} : QVector<gui::EdlEndpoint>{};
        };
        gui::AppController controller(nullptr, std::move(hooks), 15, nullptr, gui::DriverHooks{}, false);
        controller.ignorePermission();
        CHECK(Await([&] { return controller.adbReady(); }));
        auto *adb = controller.findChild<gui::AdbRunner *>();
        CHECK(adb);
        int recoveries = 0, choices = 0;
        QObject::connect(&controller, &gui::AppController::switchRecoveryRequested, [&] {
            ++recoveries;
            CHECK(!controller.switching() && !controller.tasks()->busy());
            CHECK(controller.tasks()->progressValue() == 0);
        });
        QObject::connect(&controller, &gui::AppController::adbDeviceChoiceRequested,
                         [&](const QStringList &) { ++choices; });
        if (adb) for (int vendor : {0, 1}) {
            controller.setVendor(vendor);
            for (int outcome : {0, 1, 2}) {
                const auto before = recoveries;
                const auto previousChoices = choices;
                if (vendor == 0) controller.switchToLoader();
                else controller.switchToFel();
                CHECK(Await([&] { return choices > previousChoices; }));
                CHECK(controller.switching() && controller.tasks()->busy());
                if (outcome == 2) controller.cancel();
                else {
                    // Failure classification must not depend on diagnostic wording.
                    adb->finished(outcome == 1, "Original diagnostic: cancelled USB transfer");
                    CHECK(controller.tasks()->progressText() == "Original diagnostic: cancelled USB transfer");
                    adb->CancelSwitch();
                }
                CHECK(!controller.switching() && !controller.tasks()->busy());
                CHECK(recoveries == before + (outcome == 0 ? 1 : 0));
                adb->finished(false, "late failure");
                CHECK(recoveries == before + (outcome == 0 ? 1 : 0));
            }
        }
        const auto beforeValidation = recoveries;
        controller.setVendor(1);
        controller.switchToFel(99);
        CHECK(!controller.switching() && recoveries == beforeValidation);
        CHECK(QMetaObject::invokeMethod(&controller, "onAdbAvailabilityChanged", Qt::DirectConnection,
            Q_ARG(bool, false), Q_ARG(QString, QString())));
        controller.switchToFel();
        controller.setVendor(0);
        controller.switchToLoader();
        CHECK(!controller.switching() && recoveries == beforeValidation);

        controller.setVendor(2);
        controller.edl()->setMethod(gui::EdlRunner::Adb);
        controller.switchToEdl("missing", gui::EdlRunner::Adb, "missing");
        CHECK(!controller.switching() && recoveries == beforeValidation);
        for (Scenario value : {CommandFailure, Timeout, Success, Cancel, Shutdown}) {
            scenario = value;
            controller.refreshEdl();
            CHECK(Await([&] { return controller.edl()->ready(); }));
            const auto before = recoveries;
            controller.requestEdlSwitch();
            CHECK(controller.switching() && controller.tasks()->busy());
            if (value == Cancel) controller.cancel();
            if (value == Shutdown) {
                controller.requestQuit();
                controller.confirmQuit(true);
                CHECK(controller.shuttingDown());
            }
            CHECK(Await([&] { return !controller.edl()->running(); }));
            CHECK(recoveries == before + (value == CommandFailure || value == Timeout ? 1 : 0));
            if (value != Shutdown) {
                CHECK(!controller.switching() && !controller.tasks()->busy());
                controller.edl()->finished(false, "late failure", {});
                CHECK(recoveries == before + (value == CommandFailure || value == Timeout ? 1 : 0));
            }
        }
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
