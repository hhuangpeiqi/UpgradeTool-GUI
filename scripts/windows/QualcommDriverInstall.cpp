#include "gui/WindowsUsbDriver.h"
#include "application/QualcommDriverPackage.h"
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace gui {
DriverInstallResult RunQualcommDriverElevated(DriverOperation operation, const DriverDevice &target,
                                             const DriverProgress &progress) {
    DriverInstallResult result;
    if (!IsUserAnAdmin()) { result.error = QStringLiteral("Administrator permission is required."); return result; }
    if (operation != DriverOperation::QualcommInstall && operation != DriverOperation::QualcommUninstall) {
        result.error = QStringLiteral("Invalid Qualcomm driver operation."); return result;
    }
    if (operation == DriverOperation::QualcommInstall) {
        const auto current = ScanWindowsUsbDrivers();
        const auto *device = FindDriverDevice(current.devices, target);
        if (!current.error.isEmpty() || !device || device->vendor != 2 || !device->canInstall() ||
            device->fingerprint() != target.fingerprint()) {
            result.error = QStringLiteral("Device or driver changed; refresh and select it again."); return result;
        }
    } else if (!QualcommDriverInstalled()) {
        result.error = QStringLiteral("The official Qualcomm driver installation was not found."); return result;
    }
    const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath(qualcommPackageRelativePath);
    result.error = QualcommDriverPackageError(executable);
    if (!result.error.isEmpty()) return result;
    if (progress) progress(DriverPhase::Installing);
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        // Let the official tool's children inherit a hidden console as well.
        args->flags &= ~CREATE_NO_WINDOW;
        args->flags |= CREATE_NEW_CONSOLE;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
    process.start(executable, QualcommDriverArguments(operation));
    if (!process.waitForStarted()) { result.error = process.errorString(); return result; }
    // Drain continuously; bound diagnostics without filling the child's output pipe.
    const auto drain = [&] {
        const auto bytes = process.readAll();
        if (result.log.size() < 1024 * 1024)
            result.log += QString::fromLocal8Bit(bytes).left(1024 * 1024 - result.log.size());
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200); drain();
    }
    drain();
    result.log += QStringLiteral("\nQualcomm driver tool exit code: %1\nOfficial logs: %2/Qualcomm/QUD\n")
        .arg(process.exitCode()).arg(qEnvironmentVariable("ProgramData"));
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    result.rebootRequired = process.exitStatus() == QProcess::NormalExit &&
        (process.exitCode() == ERROR_SUCCESS_REBOOT_REQUIRED || process.exitCode() == ERROR_SUCCESS_REBOOT_INITIATED);
    if (!result.ok && !result.rebootRequired)
        result.error = QStringLiteral("Qualcomm driver tool failed (exit code %1). See the driver log for details.").arg(process.exitCode());
    return result;
}
}
