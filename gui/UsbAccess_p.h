#pragma once
// Private Windows snapshot adapter, shared with diagnostic unit tests.
#include "gui/ModeSwitchFailure.h"
#include "gui/WindowsUsbDriver.h"

namespace gui {
ModeSwitchFailure DiagnoseWindowsSwitchAccess(const QVector<DriverDevice> &, int vendor,
    const QString &instance, const QString &location, int interfaceNumber = -1);
}
