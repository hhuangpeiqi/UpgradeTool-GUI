#pragma once
#include <QMetaType>

namespace gui {
// Independent of diagnostic wording; access failures require different recovery.
enum class ModeSwitchFailure { Generic, DeviceAccess };
}
Q_DECLARE_METATYPE(gui::ModeSwitchFailure)
