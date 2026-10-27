#include "backends/DeviceIdentity.h"
#include "gui/AllwinnerSwitchMonitor.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include <QCoreApplication>
#include <stdexcept>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    int failures = 0;
    for (const QString scenario :
         {"fel", "fes", "native-topology", "native-windows", "driver-delay", "reenumeration", "disappeared",
          "absent", "wrong-port", "ambiguous", "failed-handshake", "driver-missing", "unsupported", "cancel-during-handshake",
          "cancel-before-ready"}) {
        int scans = 0, opens = 0, releases = 0, completed = 0, ready = 0;
        bool ok = false;
        QString message;
        core::DeviceRef result;
        core::DeviceIdentity target;
        target.vendor = core::Vendor::Allwinner;
        target.bus = 1;
        target.ports = {2, 3};
        target.usbAddress = 7;
        auto cancel = std::make_shared<std::stop_source>();
        gui::AllwinnerSwitchMonitor::Hooks hooks;
        hooks.scan = [&] {
            ++scans;
            if (scenario == "absent" || (scenario == "reenumeration" && scans == 2) ||
                (scenario == "disappeared" && scans > 1))
                return std::vector<core::DeviceDescriptor>{};
            auto identity = target;
            if (scenario == "wrong-port")
                identity.ports = {2, 4};
            if (scenario == "reenumeration" && scans > 2)
                identity.usbAddress = 8;
            if (scenario == "native-windows")
                identity.physicalLocation = "pciroot(0)#pci(0801)#usbroot(0)#usb(2)#usb(3)";
            auto device = core::Describe(identity);
            if (scenario == "driver-missing" || (scenario == "driver-delay" && scans < 3))
                device.accessError = 5;
            if (scenario == "ambiguous")
                return std::vector<core::DeviceDescriptor>{device, device};
            return std::vector<core::DeviceDescriptor>{device};
        };
        hooks.connect = [&](const core::DeviceRef &reference, const core::Cancellation &token,
                            auto deadline) {
            ++opens;
            CHECK(token == cancel);
            const auto now = std::chrono::steady_clock::now();
            CHECK(deadline > now && deadline <= now + std::chrono::milliseconds(1500));
            if (scenario == "failed-handshake" || scenario == "disappeared" ||
                (scenario == "reenumeration" && opens == 1))
                throw std::runtime_error("handshake failed");
            if (scenario == "reenumeration")
                CHECK(core::DeviceAccess::Identity(reference).usbAddress == 8);
            if (scenario == "cancel-during-handshake")
                cancel->request_stop();
            core::allwinner::DeviceDetails details;
            details.mode = scenario == "unsupported" ? 99 : scenario == "fel" ? 1 : 2;
            return details;
        };
        hooks.release = [&](const core::DeviceRef &) { ++releases; };
        gui::AllwinnerSwitchMonitor monitor(std::move(hooks));
        QObject::connect(
            &monitor, &gui::AllwinnerSwitchMonitor::completed,
            [&](quint64 id, bool success, const QString &text, const core::DeviceRef &reference) {
                CHECK(id == 12);
                ++completed;
                ok = success;
                message = text;
                result = reference;
            });
        QObject::connect(&monitor, &gui::AllwinnerSwitchMonitor::ready, [&](quint64 id) {
            CHECK(id == 12);
            ++ready;
        });
        gui::FelSwitchRequest request{12, {"", 1, {2, 3}}, cancel, 650};
        if (scenario == "native-topology")
            request.target = {platform::UsbTopologyLocation(1, {2, 3}), 0, {}};
        if (scenario == "native-windows")
            request.target = {"PCIROOT(0)#PCI(0801)#USBROOT(0)#USB(2)#USB(3)#USBMI(1)", 0, {}};
        if (scenario == "cancel-before-ready")
            cancel->request_stop();
        monitor.prepare(request);
        CHECK(ready == (scenario == "cancel-before-ready" ? 0 : 1));
        if (scenario.startsWith("cancel-")) {
            CHECK(Await([&] { return cancel->stop_requested() && scans > 0; }, 200) ||
                  scenario == "cancel-before-ready");
            if (scenario == "cancel-before-ready")
                QCoreApplication::processEvents();
            monitor.cancel(12);
            CHECK(completed == 0);
            if (scenario == "cancel-during-handshake")
                CHECK(releases == 1);
        } else {
            CHECK(Await([&] { return completed == 1; }, 1500));
            const bool expected = scenario == "fel" || scenario == "fes" || scenario == "driver-delay" ||
                                  scenario == "reenumeration" || scenario.startsWith("native-");
            CHECK(ok == expected && !message.isEmpty());
            CHECK(bool(result) == expected);
            if (scenario == "wrong-port" || scenario == "absent" || scenario == "ambiguous")
                CHECK(opens == 0);
            if (scenario == "driver-delay")
                CHECK(opens == 1 && scans == 3);
            if (scenario == "reenumeration")
                CHECK(opens == 2 && scans == 3);
            if (scenario == "disappeared")
                CHECK(message.contains("disconnected"));
            if (scenario == "failed-handshake")
                CHECK(message.contains("handshake failed"));
            if (scenario == "driver-missing") {
                CHECK(message.contains("driver or permissions"));
                CHECK(opens == 0);
            }
            monitor.cancel(11);
            CHECK(completed == 1);
            monitor.cancel(12);
            CHECK(completed == 1);
            if (expected)
                CHECK(releases == 1);
        }
    }
    return failures ? 1 : 0;
}
