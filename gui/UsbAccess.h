#pragma once
#include "domain/Device.h"
#include "gui/ModeSwitchFailure.h"
#include <QString>

namespace gui {
QString CheckUsbAccess(const core::DeviceRef &);
ModeSwitchFailure ScanSwitchAccess(int vendor, const QString &instance, const QString &location,
    int interfaceNumber = -1);
}
