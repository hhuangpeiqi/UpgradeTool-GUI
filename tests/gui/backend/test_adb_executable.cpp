#include "gui/AdbExecutable.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    const auto directory = temporary.filePath(QString::fromUtf8("固件 tools"));
    CHECK(QDir().mkpath(directory));
#ifdef Q_OS_WIN
    const QString name = "adb.exe";
#else
    const QString name = "adb";
#endif
    const auto system = directory + '/' + name;
    const auto bundled = temporary.filePath("bundle/" + name);
    CHECK(QDir().mkpath(QFileInfo(bundled).absolutePath()));
    auto create = [](const QString &path) {
        QFile file(path); CHECK(file.open(QIODevice::WriteOnly));
        file.write("test fixture\n"); file.close();
        CHECK(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    };
    create(system);
    const auto missing = temporary.filePath("missing/" + name);
    CHECK(gui::ResolveAdbExecutable({}, missing, {directory}).path == system);
    CHECK(gui::ResolveAdbExecutable({}, missing, {}).path.isEmpty());
    create(bundled);
    auto selected = gui::ResolveAdbExecutable({}, bundled, {directory});
    CHECK(selected.path == bundled && selected.source == "bundled");
    selected = gui::ResolveAdbExecutable(system, bundled, {directory});
    CHECK(selected.path == system && selected.source == "ADB");
    CHECK(gui::ResolveAdbExecutable(name, bundled, {directory}).path == system);
    CHECK(gui::ResolveAdbExecutable(missing, bundled, {directory}).path == missing);
    CHECK(gui::ResolveAdbExecutable("missing-command", bundled, {}).source == "ADB");
#ifndef Q_OS_WIN
    CHECK(QFile::setPermissions(bundled, QFileDevice::ReadOwner));
    CHECK(gui::ResolveAdbExecutable({}, bundled, {directory}).path == bundled);
    CHECK(QFile::remove(bundled));
    CHECK(QFile::link(missing, bundled));
    CHECK(gui::ResolveAdbExecutable({}, bundled, {directory}).path == bundled);
#endif
    return failures ? 1 : 0;
}
