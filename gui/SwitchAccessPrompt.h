#pragma once
#include <QCoreApplication>
#include <QMetaType>
#include <QString>
#include "gui/ModeSwitchFailure.h"

namespace gui {
// A complete presentation decision; button visibility is derived from one action.
struct SwitchAccessPrompt {
    Q_GADGET
    Q_PROPERTY(QString title MEMBER title)
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
    QString title;
    bool offersDrivers() const { return action != Action::Acknowledge; }
    bool offersAcknowledgement() const {
        return action == Action::Acknowledge || action == Action::ViewDriversAndAcknowledge;
    }
    bool installsDriver() const { return action == Action::InstallDriver; }
};
inline SwitchAccessPrompt MakeSwitchAccessPrompt(const ModeSwitchFailure &failure,
                                               bool driversAvailable) {
    using Action = SwitchAccessPrompt::Action;
    using Reason = ModeSwitchFailure::Reason;
    auto action = Action::Acknowledge;
    if (driversAvailable && failure.reason != Reason::RebootRequired) {
        if (failure.reason == Reason::Missing && failure.canInstall) action = Action::InstallDriver;
        else if (failure.reason == Reason::Incompatible) action = Action::ViewDrivers;
        else action = Action::ViewDriversAndAcknowledge;
    }
    QString advice;
    switch (failure.reason) {
    case Reason::Missing:
        advice = driversAvailable && failure.canInstall
            ? QCoreApplication::translate("gui::SwitchAccessPrompt", "Open USB Drivers to install the interface driver.")
            : QCoreApplication::translate("gui::SwitchAccessPrompt", "Install the device manufacturer's interface driver."); break;
    case Reason::Incompatible:
        advice = driversAvailable
            ? QCoreApplication::translate("gui::SwitchAccessPrompt", "Driver incompatible; check USB Drivers for details.")
            : QCoreApplication::translate("gui::SwitchAccessPrompt", "Driver incompatible; install the device manufacturer's driver."); break;
    case Reason::RebootRequired:
        advice = QCoreApplication::translate("gui::SwitchAccessPrompt", "Restart Windows to finish driver setup."); break;
    case Reason::DeviceError:
        advice = driversAvailable
            ? QCoreApplication::translate("gui::SwitchAccessPrompt", "Device error; check USB Drivers for details.")
            : QCoreApplication::translate("gui::SwitchAccessPrompt", "Device error; check its status in Device Manager."); break;
    case Reason::Unknown:
        advice = QCoreApplication::translate("gui::SwitchAccessPrompt", "Check the USB connection, access permissions, and other device tools."); break;
    }
    const auto title = failure.sourceInterface
        ? QCoreApplication::translate("gui::SwitchAccessPrompt", "Switch request not sent")
        : QCoreApplication::translate("gui::SwitchAccessPrompt", "Device access unavailable");
    return {advice, failure.instance, action, title};
}
}
Q_DECLARE_METATYPE(gui::SwitchAccessPrompt)
