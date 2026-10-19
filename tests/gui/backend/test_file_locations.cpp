#include "gui/backend/FileLocations.h"
#include "gui/backend/DirModel.h"
#include "tests/support/Checks.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>
#include <algorithm>

static int failures = 0;
using Locations = gui::FileLocations;
using Platform = Locations::Platform;

static void TestVolumes()
{
    QList<Locations::Volume> snapshot{
        {"/", "System"}, {"/boot", "Boot"}, {"/media/user/USB", "移动磁盘"},
        {"/media/user/USB/", "Duplicate"}, {"/run/media/user/SSD", ""},
        {"/mnt/data", "Data"}, {"/mnt", "Mount root"}, {"/mnt-other/no", "Hidden"},
        {"/media/user/not-ready", "", {}, true, false},
        {"/media/user/invalid", "", {}, false, true}
    };
    Locations linux(Platform::Linux, [&] { return snapshot; });
    CHECK(linux.refresh());
    const auto places = linux.places();
    CHECK(places.size() == 4);
    CHECK(places.first().toMap().value("name") == "移动磁盘");
    CHECK(places.last().toMap().value("name") == "SSD");
    CHECK(!linux.refresh());
    std::reverse(snapshot.begin(), snapshot.end());
    // Removing a duplicate does not change the resulting mount snapshot.
    snapshot.removeIf([](const auto &v) { return v.label == "Duplicate"; });
    CHECK(!linux.refresh());
    for (auto &v : snapshot) if (v.label == "移动磁盘") v.label = "Renamed";
    CHECK(linux.refresh());
    CHECK(linux.places().first().toMap().value("name") == "Renamed");

    snapshot = {{"/", "System"}, {"/System/Volumes/Data", "Internal"},
                {"/Volumes/外接 SSD", "外接 SSD"}, {"/Volumes/Untitled", ""}};
    Locations mac(Platform::MacOS, [&] { return snapshot; });
    CHECK(mac.refresh());
    CHECK(mac.places().size() == 2);
    CHECK(mac.places().first().toMap().value("name") == "Untitled");

    snapshot = {{"C:/", "System"}, {"e:\\", "Backup"}, {"E:/", "Duplicate"},
                {"F:/", ""}, {"G:/", "Empty reader", {}, true, false}};
    Locations windows(Platform::Windows, [&] { return snapshot; });
    CHECK(windows.refresh());
    CHECK(windows.places().size() == 3);
    CHECK(windows.places().first().toMap().value("name") == "System (C:)");
    CHECK(windows.places()[1].toMap().value("name") == "Backup (E:)");
    CHECK(windows.places().last().toMap().value("name") == "F:");
}

static void TestRemoval()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const QString root = temporary.filePath("mount");
    CHECK(QDir().mkpath(root + "/sub"));
    QList<Locations::Volume> snapshot{{root, "Disk"}};
    Locations locations(Locations::hostPlatform(), [&] { return snapshot; });
    CHECK(locations.refresh());
    CHECK(locations.directoryAvailable(root + "/sub"));
    snapshot.clear();
    CHECK(locations.refresh());
    CHECK(QDir(root).exists()); // Unmounting need not remove the mount directory.
    CHECK(!locations.directoryAvailable(root));
    CHECK(!locations.directoryAvailable(root + "/sub"));
    CHECK(locations.directoryAvailable(temporary.path()));
    CHECK(!locations.refresh());
    snapshot.append({root, "Disk"});
    CHECK(locations.refresh());
    CHECK(locations.directoryAvailable(root + "/sub"));
    snapshot[0].ready = false;
    CHECK(locations.refresh());
    CHECK(!locations.directoryAvailable(root));
    CHECK(!locations.directoryAvailable(temporary.filePath("missing")));
}

static void TestPaths()
{
    CHECK(Locations::parentPath("/", Platform::Linux) == "/");
    CHECK(Locations::parentPath("/media", Platform::Linux) == "/");
    CHECK(Locations::parentPath("E:/", Platform::Windows) == "E:/");
    CHECK(Locations::parentPath("E:/folder", Platform::Windows) == "E:/");
    CHECK(Locations::parentPath("E:\\folder\\sub", Platform::Windows) == "E:/folder");
    CHECK(Locations::parentPath("//server/share", Platform::Windows) == "//server/share");
    CHECK(Locations::parentPath("//server/share/folder", Platform::Windows) == "//server/share");
    CHECK(Locations::containsPath("e:/", "E:/文件夹/image.bin", Platform::Windows));
    CHECK(!Locations::containsPath("/mnt/data", "/mnt/database", Platform::Linux));
    const auto crumbs = Locations::breadcrumbs("E:/文件夹/image", Platform::Windows);
    CHECK(crumbs.size() == 3);
    CHECK(crumbs.first().toMap().value("path") == "E:/");
    CHECK(crumbs.last().toMap().value("path") == "E:/文件夹/image");
    const auto share = Locations::breadcrumbs("//server/share/folder", Platform::Windows);
    CHECK(share.size() == 2);
    CHECK(share.first().toMap().value("path") == "//server/share");

    QTemporaryDir temporary;
    gui::DirModel model;
    model.setRootPath(temporary.path());
    CHECK(model.parentPath() == QDir::cleanPath(QDir(temporary.path()).filePath("..")));
    CHECK(model.filePath("firmware.bin") == temporary.filePath("firmware.bin"));
    CHECK(model.breadcrumbs().last().toMap().value("path") == temporary.path());
#ifdef Q_OS_WIN
    model.setRootPath(QDir::rootPath());
    CHECK(model.parentPath() == model.rootPath());
    CHECK(model.filePath("firmware.bin") == QDir::rootPath() + "firmware.bin");
#endif
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    TestVolumes();
    TestRemoval();
    TestPaths();
    return failures ? 1 : 0;
}
