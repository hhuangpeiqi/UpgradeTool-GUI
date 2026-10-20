#ifndef GUI_ADB_EXECUTABLE_H
#define GUI_ADB_EXECUTABLE_H

#include <QString>
#include <QStringList>

namespace gui {
struct AdbExecutable {
    QString path;
    QString source;
};

// Empty search directories mean no PATH fallback, which also keeps tests hermetic.
AdbExecutable ResolveAdbExecutable(const QString &overridePath, const QString &bundledPath,
                                   const QStringList &searchDirectories);
AdbExecutable FindAdbExecutable();
}
#endif
