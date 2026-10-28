#include "gui/SingleInstanceGuard.h"
#include <QCoreApplication>
#include <QFile>
#include <QDateTime>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>
#include <array>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using Guard = gui::SingleInstanceGuard;
    const auto args = app.arguments();
    if (args.size() == 3) {
        Guard guard(args[2]);
        const auto result = guard.acquire();
        if (args[1] == "hold" && result == Guard::Result::Acquired) {
            std::puts("locked"); std::fflush(stdout);
            return app.exec();
        }
        return result == Guard::Result::AlreadyRunning ? 2 : result == Guard::Result::Acquired ? 0 : 3;
    }
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString lockDirectory = directory.path() + "/locks";
    auto probe = [&] {
        QProcess child;
        child.start(app.applicationFilePath(), {"probe", lockDirectory});
        if (!child.waitForFinished(5000)) return -1;
        return child.exitCode();
    };
    CHECK(probe() == 0);
    {
        Guard guard(lockDirectory);
        CHECK(guard.acquire() == Guard::Result::Acquired);
        CHECK(probe() == 2);
        QFile lockFile(lockDirectory + "/instance.lock");
        CHECK(lockFile.open(QIODevice::ReadWrite));
        CHECK(lockFile.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        lockFile.close();
        CHECK(probe() == 2); // A live owner must not expire by lock age.
    }
    CHECK(probe() == 0);
    QProcess owner;
    owner.start(app.applicationFilePath(), {"hold", lockDirectory});
    CHECK(owner.waitForReadyRead(5000));
    CHECK(owner.readAllStandardOutput().contains("locked"));
    CHECK(probe() == 2);
    owner.kill();
    CHECK(owner.waitForFinished(5000));
    CHECK(probe() == 0); // Crash recovery must not require removing the lock manually.
    std::array<QProcess, 6> contenders;
    for (auto &child : contenders)
        child.start(app.applicationFilePath(), {"hold", lockDirectory});
    int winners = 0;
    for (auto &child : contenders) {
        child.waitForReadyRead(5000);
        if (child.readAllStandardOutput().contains("locked")) ++winners;
        else {
            if (child.state() != QProcess::NotRunning) CHECK(child.waitForFinished(5000));
            CHECK(child.exitCode() == 2);
        }
    }
    CHECK(winners == 1);
    for (auto &child : contenders) {
        if (child.state() != QProcess::NotRunning) { child.kill(); CHECK(child.waitForFinished(5000)); }
    }
    CHECK(probe() == 0);
    QFile file(directory.path() + "/file");
    CHECK(file.open(QIODevice::WriteOnly));
    file.close();
    Guard invalid(file.fileName() + "/locks");
    CHECK(invalid.acquire() == Guard::Result::Error);
    return 0;
}
