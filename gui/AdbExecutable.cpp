#include "gui/AdbExecutable.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace gui {
AdbExecutable ResolveAdbExecutable(const QString &overridePath, const QString &bundledPath,
                                   const QStringList &searchDirectories)
{
    auto absolute = [](const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); };
    if (!overridePath.isEmpty()) {
        // A name is searched in PATH, but an invalid explicit override never falls back.
        if (!overridePath.contains('/') && !overridePath.contains('\\') && !searchDirectories.isEmpty()) {
            const auto found = QStandardPaths::findExecutable(overridePath, searchDirectories);
            if (!found.isEmpty()) return {found, QStringLiteral("ADB")};
        }
        return {absolute(overridePath), QStringLiteral("ADB")};
    }
    const QFileInfo bundled(bundledPath);
    // Keep a broken/non-executable bundled installation selected so its error is visible.
    if (!bundledPath.isEmpty() && (bundled.exists() || bundled.isSymLink()))
        return {absolute(bundledPath), QStringLiteral("bundled")};
    if (!searchDirectories.isEmpty()) {
        const auto found = QStandardPaths::findExecutable(QStringLiteral("adb"), searchDirectories);
        if (!found.isEmpty()) return {found, QStringLiteral("PATH")};
    }
    return {};
}

AdbExecutable FindAdbExecutable()
{
    QString bundled;
    auto directories = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
#ifdef Q_OS_WIN
    bundled = QCoreApplication::applicationDirPath() + "/tools/adb/adb.exe";
#elif defined(Q_OS_MACOS)
    bundled = QCoreApplication::applicationDirPath() + "/../Helpers/adb/adb";
    directories << "/opt/homebrew/bin" << "/usr/local/bin";
#endif
    return ResolveAdbExecutable(qEnvironmentVariable("ADB"), bundled, directories);
}
}
