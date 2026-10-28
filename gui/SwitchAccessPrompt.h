#pragma once
#include <QMetaType>
#include <QString>
#include "gui/ModeSwitchFailure.h"

namespace gui {
// A complete presentation decision; button visibility is derived from one action.
struct SwitchAccessPrompt {
    Q_GADGET
    Q_PROPERTY(QString message MEMBER message)
    Q_PROPERTY(QString instance MEMBER instance)
    Q_PROPERTY(bool offersDrivers READ offersDrivers)
    Q_PROPERTY(bool offersAcknowledgement READ offersAcknowledgement)
    Q_PROPERTY(bool installsDriver READ installsDriver)
public:
    enum class Action { InstallDriver, ViewDrivers, Acknowledge, ViewDriversAndAcknowledge };
    Q_ENUM(Action)
    QString message, instance;
    Action action = Action::ViewDriversAndAcknowledge;
    bool offersDrivers() const { return action != Action::Acknowledge; }
    bool offersAcknowledgement() const {
        return action == Action::Acknowledge || action == Action::ViewDriversAndAcknowledge;
    }
    bool installsDriver() const { return action == Action::InstallDriver; }
};
inline SwitchAccessPrompt MakeSwitchAccessPrompt(const QString &message, const ModeSwitchFailure &failure,
                                                bool driversAvailable) {
    using Action = SwitchAccessPrompt::Action;
    using Reason = ModeSwitchFailure::Reason;
    auto action = Action::Acknowledge;
    if (driversAvailable && failure.reason != Reason::RebootRequired) {
        if (failure.reason == Reason::Missing && failure.canInstall) action = Action::InstallDriver;
        else if (failure.reason == Reason::Incompatible) action = Action::ViewDrivers;
        else action = Action::ViewDriversAndAcknowledge;
    }
    return {message, failure.instance, action};
}
}
Q_DECLARE_METATYPE(gui::SwitchAccessPrompt)
