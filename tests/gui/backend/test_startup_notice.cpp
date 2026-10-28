#include "gui/SingleInstanceGuard.h"
#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 2) {
        bool visible = false;
        QTimer::singleShot(0, &app, [&] {
            for (auto *window : QGuiApplication::topLevelWindows()) {
                if (window->isVisible() && window->title() == QStringLiteral("UpgradeTool-GUI")) {
                    visible = true;
                    window->close();
                    break;
                }
            }
        });
        QTimer::singleShot(3000, &app, [] { QCoreApplication::exit(99); });
        const int result = gui::ShowStartupNotice(args[1] == "already-running");
        return visible ? result : 98;
    }

    QTemporaryDir directory;
    CHECK(directory.isValid());
    // QProcess captures stderr in a pipe by default. Also cover file redirection,
    // since neither output destination should suppress the startup window.
    for (bool redirectToFile : {false, true}) {
        for (const auto &mode : {QStringLiteral("already-running"), QStringLiteral("lock-error")}) {
            QProcess child;
            if (redirectToFile)
                child.setStandardErrorFile(directory.filePath("stderr.log"));
            child.start(app.applicationFilePath(), {mode});
            if (!child.waitForFinished(10000)) {
                child.kill();
                child.waitForFinished(5000);
                CHECK(false);
            }
            CHECK(child.exitStatus() == QProcess::NormalExit);
            CHECK(child.exitCode() == (mode == "already-running" ? 0 : 1));
        }
    }
    return 0;
}
