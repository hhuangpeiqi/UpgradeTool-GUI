#include "gui/WindowsUsbDriver.h"
#include "gui/DriverRecovery.h"
#include "application/RockusbDriverPackage.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <windows.h>
#include <shlobj.h>

namespace gui {
DriverInstallResult RunRockusbDriverElevated(const DriverDevice &target, const DriverProgress &progress) {
    DriverInstallResult result;
    if (!IsUserAnAdmin()) { result.error = QStringLiteral("Administrator permission is required."); return result; }
    const auto current = ScanWindowsUsbDrivers();
    const auto *device = FindDriverDevice(current.devices, target);
    if (!current.error.isEmpty() || !device || device->vendor != 0 || !device->canInstall() ||
        device->fingerprint() != target.fingerprint()) {
        result.error = QStringLiteral("Device or driver changed; refresh and select it again."); return result;
    }
    const QDir source(QDir(QCoreApplication::applicationDirPath()).filePath(rockusbPackageRelativePath));
    result.error = RockusbDriverPackageError(source.path());
    if (!result.error.isEmpty()) return result;
    QTemporaryDir directory(QDir::tempPath() + "/upgrade-rockusb-XXXXXX");
    if (!directory.isValid()) { result.error = QStringLiteral("Cannot create the Rockusb driver directory."); return result; }
    const QDir destination(directory.path());
    for (const auto &[relative, checksum] : rockusbPackageFiles) {
        Q_UNUSED(checksum);
        const QString path = destination.filePath(QString::fromUtf8(relative));
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !QFile::copy(source.filePath(QString::fromUtf8(relative)), path)) {
            result.error = QStringLiteral("Cannot prepare Rockusb driver file: %1").arg(QString::fromUtf8(relative)); return result;
        }
    }
    result.error = RockusbDriverPackageError(directory.path());
    if (!result.error.isEmpty()) return result;
    if (progress) progress(DriverPhase::Installing);
    QProcess process;
    process.setWorkingDirectory(directory.path());
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags &= ~(CREATE_NO_WINDOW | CREATE_NEW_CONSOLE);
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_SHOWNORMAL;
    });
    process.start(destination.filePath("DriverInstall.exe"), {});
    if (!process.waitForStarted()) { result.error = process.errorString(); return result; }
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(200);
        process.readAll();
    }
    result.log = QStringLiteral("Rockusb driver assistant closed: exit=%1 status=%2\n")
        .arg(process.exitCode()).arg(int(process.exitStatus()));
    QDirIterator logs(destination.filePath("Log"), QDir::Files, QDirIterator::Subdirectories);
    while (logs.hasNext() && result.log.size() < 1024 * 1024) {
        QFile file(logs.next());
        if (file.open(QIODevice::ReadOnly))
            result.log += file.fileName() + '\n' + QString::fromLocal8Bit(file.read(1024 * 1024 - result.log.size()));
    }
    // The GUI's exit code does not say whether the user actually installed a
    // driver. The controller must verify the target after every normal close.
    result.ok = process.exitStatus() == QProcess::NormalExit;
    if (!result.ok) result.error = QStringLiteral("The Rockusb driver assistant terminated unexpectedly.");
    else {
        DriverRecoveryHooks hooks;
        hooks.scan = [] { return ScanWindowsUsbDrivers(true); };
        hooks.restart = RestartWindowsUsbDevice;
        hooks.wait = [](int ms) { QThread::msleep(ms); };
        hooks.progress = progress;
        const auto recovery = RecoverRockusbDriver(target, hooks);
        result.log += recovery.log;
        result.rebootRequired = recovery.rebootRequired;
    }
    return result;
}
}
