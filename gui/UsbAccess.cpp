#include "gui/UsbAccess.h"
#include "gui/UsbAccess_p.h"
#include "platform/UsbLocation.h"
#include <algorithm>
#include "backends/DeviceIdentity.h"
#include "application/BuildFeatures.h"
#if UPGRADE_TOOL_ROCKCHIP
#include "backends/rockchip/UsbTransport.h"
#endif
#include "backends/qualcomm/DevicePolicy.h"
#include <QCoreApplication>
#include <QScopeGuard>
#include <libusb.h>

namespace gui {
ModeSwitchFailure DiagnoseWindowsSwitchAccess(const QVector<DriverDevice> &devices, int vendor,
    const QString &instance, const QString &location, int interfaceNumber) {
    auto result = ModeSwitchFailure::DeviceAccess;
    const DriverDevice *found = nullptr;
    const bool exact = !instance.isEmpty() && std::any_of(devices.begin(), devices.end(), [&](const auto &d) {
        return d.vendor == vendor && d.verifiedInterface &&
            (interfaceNumber < 0 || d.interfaceNumber == interfaceNumber) &&
            !d.instance.compare(instance, Qt::CaseInsensitive);
    });
    for (const auto &d : devices) {
        if (d.vendor != vendor || !d.verifiedInterface ||
            (interfaceNumber >= 0 && d.interfaceNumber != interfaceNumber)) continue;
        const bool sameInstance = !instance.isEmpty() && !d.instance.compare(instance, Qt::CaseInsensitive);
        const bool sameLocation = !location.isEmpty() && !d.location.isEmpty() &&
            platform::NormalizeUsbLocation(location.toStdString()) == platform::NormalizeUsbLocation(d.location.toStdString());
        if (!sameInstance && !sameLocation) continue;
        if (exact && !sameInstance) continue;
        if (found) return result;
        found = &d;
    }
    if (!found) return result;
    result.instance = found->instance;
    result.detail = found->diagnostic;
    result.canInstall = found->canInstall();
    using Reason = ModeSwitchFailure::Reason;
    switch (found->state) {
    case DriverState::Missing: result.reason = Reason::Missing; break;
    case DriverState::Incompatible: result.reason = Reason::Incompatible; break;
    case DriverState::RebootRequired: result.reason = Reason::RebootRequired; break;
    case DriverState::DeviceError:
        result.reason = Reason::DeviceError;
        result.detail = QString::number(found->problem); break;
    default: break;
    }
    return result;
}
ModeSwitchFailure ScanSwitchAccess(int vendor, const QString &instance, const QString &location, int interfaceNumber) {
#ifdef Q_OS_WIN
    const auto scan = ScanWindowsUsbDrivers(false);
    if (scan.error.isEmpty()) return DiagnoseWindowsSwitchAccess(scan.devices, vendor, instance, location, interfaceNumber);
    auto failure = ModeSwitchFailure::DeviceAccess;
    failure.detail = scan.error;
    return failure;
#else
    Q_UNUSED(vendor); Q_UNUSED(instance); Q_UNUSED(location); Q_UNUSED(interfaceNumber);
#endif
    return ModeSwitchFailure::DeviceAccess;
}

QString CheckUsbAccess(const core::DeviceRef &reference) {
#ifdef Q_OS_WIN
    std::lock_guard<std::recursive_mutex> lock(UsbProbeMutex());
    const auto &target = core::DeviceAccess::Identity(reference);
    // Allwinner validates access when acquiring its backend-owned session.
    if (target.vendor == core::Vendor::Allwinner) return {};
#if UPGRADE_TOOL_ROCKCHIP
    if (target.vendor == core::Vendor::Rockchip) {
        const int error = core::rockchip::ProbeRockusb(target.deviceInstance, target.devicePath);
        return error ? QCoreApplication::translate("gui::AppController", "Cannot access the selected USB interface: %1")
                           .arg(QString::fromStdString(core::rockchip::UsbError(error))) : QString{};
    }
#endif
    // QUD validates the selected identity and opens the port for the actual
    // operation. A separate open/close probe can disturb the serial driver.
    if (target.vendor == core::Vendor::Qualcomm && target.transport == core::QualcommTransport::Qud) return {};
    libusb_context *context = nullptr;
    int rc = libusb_init(&context);
    if (rc) return QString::fromLatin1(libusb_error_name(rc));
    auto releaseContext = qScopeGuard([&] { libusb_exit(context); });
    libusb_device **list = nullptr;
    const auto count = libusb_get_device_list(context, &list);
    if (count < 0) return QString::fromLatin1(libusb_error_name(int(count)));
    auto releaseList = qScopeGuard([&] { libusb_free_device_list(list, 1); });
    libusb_device *selected = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(list[i], &descriptor) || descriptor.idVendor != target.vid || descriptor.idProduct != target.pid) continue;
        uint8_t ports[16]; const int depth = libusb_get_port_numbers(list[i], ports, sizeof(ports));
        if (depth <= 0) continue;
        if (libusb_get_bus_number(list[i]) != target.bus || std::vector<uint8_t>(ports, ports + depth) != target.ports ||
            (target.usbAddress && libusb_get_device_address(list[i]) != target.usbAddress)) continue;
        if (selected) return QCoreApplication::translate("gui::AppController", "The selected device is ambiguous; refresh and select it again");
        selected = list[i];
    }
    if (!selected) return QCoreApplication::translate("gui::AppController", "The selected device disconnected; refresh and select it again");
    libusb_device_handle *handle = nullptr;
    rc = libusb_open(selected, &handle);
    if (rc) return QCoreApplication::translate("gui::AppController", "Cannot access the selected USB interface: %1").arg(libusb_error_name(rc));
    auto close = qScopeGuard([&] { libusb_close(handle); });
    libusb_config_descriptor *configuration = nullptr;
    rc = libusb_get_active_config_descriptor(selected, &configuration);
    if (rc) return QString::fromLatin1(libusb_error_name(rc));
    auto freeConfig = qScopeGuard([&] { libusb_free_config_descriptor(configuration); });
    for (int i = 0; i < configuration->bNumInterfaces; ++i) {
        const auto &usbInterface = configuration->interface[i];
        for (int j = 0; j < usbInterface.num_altsetting; ++j) {
            const auto &alt = usbInterface.altsetting[j];
            if (target.interfaceNumber >= 0 && alt.bInterfaceNumber != target.interfaceNumber) continue;
            if (target.vendor == core::Vendor::Qualcomm && !qtq_download_interface(alt.bInterfaceClass, alt.bInterfaceSubClass, alt.bInterfaceProtocol, 0x81, 1, 1, 1)) continue;
            bool in = false, out = false;
            for (int p = 0; p < alt.bNumEndpoints; ++p) {
                const auto &endpoint = alt.endpoint[p];
                if ((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                in |= (endpoint.bEndpointAddress & 0x80) != 0; out |= (endpoint.bEndpointAddress & 0x80) == 0;
            }
            if (!in || !out) continue;
            rc = libusb_claim_interface(handle, alt.bInterfaceNumber);
            if (rc) return QCoreApplication::translate("gui::AppController", "Cannot access the selected USB interface: %1").arg(libusb_error_name(rc));
            libusb_release_interface(handle, alt.bInterfaceNumber);
            return {};
        }
    }
    return QCoreApplication::translate("gui::AppController", "No compatible flashing interface was found");
#else
    Q_UNUSED(reference); return {};
#endif
}
}
