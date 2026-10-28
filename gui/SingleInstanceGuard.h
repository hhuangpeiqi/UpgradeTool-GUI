#pragma once
#include <QLockFile>
#include <QString>

namespace gui {
class SingleInstanceGuard {
public:
    enum class Result { Acquired, AlreadyRunning, Error };
    explicit SingleInstanceGuard(const QString &directory = QString());
    Result acquire();
private:
    QString m_directory;
    QLockFile m_lock;
};
// Runs only for rejected startups, before controllers or device services exist.
int ShowStartupNotice(bool alreadyRunning);
}
