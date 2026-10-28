#pragma once
#include <QMetaType>
#include <QString>

namespace gui {
struct ModeSwitchFailure {
    enum class Kind { Generic, DeviceAccess };
    enum class Reason { Unknown, Missing, Incompatible, RebootRequired, DeviceError };
    Kind kind = Kind::Generic;
    Reason reason = Reason::Unknown;
    QString instance, detail;
    bool canInstall = false, sourceInterface = false;
    static const ModeSwitchFailure Generic, DeviceAccess;
    bool operator==(const ModeSwitchFailure &other) const { return kind == other.kind; }
};
inline const ModeSwitchFailure ModeSwitchFailure::Generic{};
inline const ModeSwitchFailure ModeSwitchFailure::DeviceAccess{ModeSwitchFailure::Kind::DeviceAccess};
}
Q_DECLARE_METATYPE(gui::ModeSwitchFailure)
