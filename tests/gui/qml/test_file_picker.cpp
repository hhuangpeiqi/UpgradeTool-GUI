#include "gui/backend/DirModel.h"
#include "gui/backend/FileLocations.h"
#include "gui/backend/LanguageManager.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QTest>
#include <memory>

static int failures = 0;

class PickerApp : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList standardPlaces READ standardPlaces NOTIFY standardPlacesChanged)
    Q_PROPERTY(gui::LanguageManager *language READ language CONSTANT)
public:
    explicit PickerApp(QString home) : m_home(std::move(home)),
        // List all temporary mount roots without requiring privileged mounts.
        m_locations(gui::FileLocations::Platform::Windows, [this] { return volumes; }) {}
    QList<gui::FileLocations::Volume> volumes;
    int scans = 0;
    QString remembered;
    gui::LanguageManager *language() { return &m_language; }
    QVariantList standardPlaces() const {
        QVariantList places{QVariantMap{{"name", "Home"}, {"path", m_home}}};
        places.append(m_locations.places());
        return places;
    }
    Q_INVOKABLE void refreshStandardPlaces() {
        ++scans;
        const auto old = standardPlaces();
        const bool changed = m_locations.refresh();
        if (old != standardPlaces()) emit standardPlacesChanged();
        if (changed) emit storageVolumesChanged();
    }
    Q_INVOKABLE bool directoryAvailable(const QString &path) const { return m_locations.directoryAvailable(path); }
    Q_INVOKABLE QString homeDir() const { return m_home; }
    Q_INVOKABLE QString lastDir(const QString &) const { return remembered; }
    Q_INVOKABLE void rememberPath(const QString &, const QString &path) { remembered = path; }
    Q_INVOKABLE QString toLocalPath(const QString &url) const { return QUrl(url).toLocalFile(); }
    Q_INVOKABLE QString pathUrl(const QString &path) const { return QUrl::fromLocalFile(path).toString(); }
signals:
    void standardPlacesChanged();
    void storageVolumesChanged();
private:
    QString m_home;
    gui::LanguageManager m_language;
    gui::FileLocations m_locations;
};

static void TestPicker(QQmlEngine &engine, PickerApp &app, const QString &disk, const QString &other)
{
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import UpgradeTool
Window {
    width: 1000; height: 800; visible: true
    property int acceptedCount: 0
    property string selectionFailure: ""
    AppFilePicker {
        objectName: "picker"
        persistKey: "test"
        validateSelection: function(paths) { return selectionFailure; }
        onAccepted: ++acceptedCount
    }
}
)QML", QUrl());
    CHECK(AwaitUi([&] { return component.status() != QQmlComponent::Loading; }));
    std::unique_ptr<QObject> window(component.create());
    CHECK(window);
    if (!window) {
        std::fprintf(stderr, "%s\n", component.errorString().toUtf8().constData());
        return;
    }
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    CHECK(quickWindow);
    if (!quickWindow) return;
    CHECK(QTest::qWaitForWindowExposed(quickWindow));
    // Xvfb has no window manager to activate the window for keyboard shortcuts.
    quickWindow->requestActivate();
    CHECK(QTest::qWaitForWindowActive(quickWindow));
    auto *picker = window->findChild<QObject *>("picker");
    auto *timer = window->findChild<QObject *>("storageRefreshTimer");
    auto *model = window->findChild<gui::DirModel *>("filePickerDirModel");
    auto *field = window->findChild<QObject *>("filePickerFileName");
    auto *refresh = window->findChild<QObject *>("filePickerRefresh");
    auto *error = window->findChild<QObject *>("filePickerErrorDialog");
    auto *message = window->findChild<QObject *>("filePickerErrorMessage");
    auto *ok = window->findChild<QObject *>("filePickerErrorOk");
    auto *newFolder = window->findChild<QObject *>("filePickerNewFolderName");
    CHECK(picker && timer && model && field && refresh && error && message && ok && newFolder);
    if (!picker || !timer || !model || !field || !refresh || !error || !message || !ok || !newFolder) return;
    auto invoke = [&](const char *method) { CHECK(QMetaObject::invokeMethod(picker, method)); };
    auto navigate = [&](const QString &path) {
        CHECK(QMetaObject::invokeMethod(picker, "navigateTo", Q_ARG(QVariant, path)));
    };
    auto open = [&] {
        const int before = app.scans;
        invoke("open");
        CHECK(AwaitUi([&] { return picker->property("visible").toBool(); }));
        CHECK(app.scans > before);
        CHECK(timer->property("running").toBool());
    };
    auto close = [&] {
        invoke("close");
        CHECK(AwaitUi([&] { return !picker->property("visible").toBool(); }));
        CHECK(!timer->property("running").toBool());
        CHECK(!error->property("visible").toBool());
    };
    auto expectError = [&](const QString &text) {
        CHECK(AwaitUi([&] { return error->property("opened").toBool(); }));
        CHECK(message->property("text").toString() == text);
        CHECK(error->property("modal").toBool());
        CHECK(picker->property("visible").toBool());
        CHECK(picker->property("hintText").toString().isEmpty());
        CHECK(!picker->property("hintError").toBool());
        CHECK(window->property("acceptedCount").toInt() == 0);
        CHECK(ok->property("activeFocus").toBool());
    };
    auto dismissError = [&] {
        auto *button = qobject_cast<QQuickItem *>(ok);
        CHECK(button);
        if (button) {
            const auto point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
            QTest::mouseClick(qobject_cast<QQuickWindow *>(window.get()), Qt::LeftButton, Qt::NoModifier, point.toPoint());
        }
        CHECK(AwaitUi([&] { return !error->property("visible").toBool(); }));
        CHECK(picker->property("visible").toBool());
    };

    open();
    for (int mode : {0, 1}) {
        picker->setProperty("mode", mode);
        for (int attempt = 0; attempt < 2; ++attempt) {
            invoke("confirmAccept");
            expectError("Enter a file name");
            dismissError();
            CHECK(AwaitUi([&] { return field->property("activeFocus").toBool(); }));
        }
        field->setProperty("text", "bad/name.img");
        invoke("confirmAccept");
        expectError("File name cannot contain \"/\"");
        // Escape dismisses only the topmost popup.
        QTest::keyClick(qobject_cast<QQuickWindow *>(window.get()), Qt::Key_Escape);
        CHECK(AwaitUi([&] { return !error->property("visible").toBool(); }));
        CHECK(picker->property("visible").toBool());
        field->setProperty("text", "");
    }
    picker->setProperty("creatingFolder", true);
    CHECK(QMetaObject::invokeMethod(newFolder, "forceActiveFocus"));
    invoke("createFolder");
    expectError("Directory name cannot be empty");
    dismissError();
    CHECK(AwaitUi([&] { return newFolder->property("activeFocus").toBool(); }));
    newFolder->setProperty("text", "created");
    invoke("createFolder");
    CHECK(QDir(app.homeDir() + "/created").exists());
    CHECK(picker->property("hintText").toString() == "Directory created");
    CHECK(!error->property("visible").toBool());

    // Both single and multiple selection validators use the same popup.
    navigate(app.homeDir());
    window->setProperty("selectionFailure", "Selection rejected");
    for (bool multiple : {false, true}) {
        picker->setProperty("multiple", multiple);
        picker->setProperty("mode", 0);
        field->setProperty("text", multiple ? "" : "pending.img");
        picker->setProperty("selectedPaths", QStringList{disk + "/pending.img"});
        for (int attempt = 0; attempt < 2; ++attempt) {
            invoke("confirmAccept");
            expectError("Selection rejected");
            dismissError();
        }
    }
    picker->setProperty("multiple", false);
    window->setProperty("selectionFailure", "");
    close();
    open();
    CHECK(!error->property("visible").toBool());
    const int firstScan = app.scans;
    app.volumes = {{disk, "USB"}};
    CHECK(AwaitUi([&] { return app.scans > firstScan && app.standardPlaces().size() == 2; }));
    navigate(disk);
    field->setProperty("text", "pending.img");
    picker->setProperty("selectedPath", disk + "/pending.img");
    model->setSearchText("pending");
    app.volumes.append({other, "Other disk"});
    CHECK(QMetaObject::invokeMethod(refresh, "clicked"));
    CHECK(app.standardPlaces().size() == 3);
    CHECK(model->rootPath() == disk);
    CHECK(field->property("text") == "pending.img");
    CHECK(model->searchText() == "pending");
    const int unchangedScan = app.scans;
    CHECK(AwaitUi([&] { return app.scans > unchangedScan; }));
    CHECK(field->property("text") == "pending.img");
    CHECK(picker->property("selectedPath") == disk + "/pending.img");

    // All modes must recover without accepting the new Home location. Exercise
    // removal just before confirmation as well as removal found by polling.
    for (int mode : {0, 1, 2}) {
        picker->setProperty("mode", mode);
        navigate(disk);
        field->setProperty("text", "pending.img");
        picker->setProperty("selectedPaths", QStringList{disk + "/pending.img"});
        picker->setProperty("selectedFiles", QVariantList{QUrl::fromLocalFile(disk + "/pending.img")});
        picker->setProperty("creatingFolder", true);
        app.volumes = {{other, "Other disk"}};
        if (mode == 0) CHECK(AwaitUi([&] { return model->rootPath() == app.homeDir(); }));
        else invoke("confirmAccept");
        CHECK(model->rootPath() == app.homeDir());
        CHECK(picker->property("visible").toBool());
        CHECK(window->property("acceptedCount").toInt() == 0);
        CHECK(picker->property("selectedPath").toString().isEmpty());
        CHECK(picker->property("selectedPaths").toList().isEmpty());
        CHECK(picker->property("selectedFiles").toList().isEmpty());
        CHECK(field->property("text").toString().isEmpty());
        CHECK(model->searchText().isEmpty());
        CHECK(!picker->property("creatingFolder").toBool());
        expectError("The original path is no longer available. Returned to Home.");
        dismissError();
        const int beforeRefresh = app.scans;
        CHECK(AwaitUi([&] { return app.scans > beforeRefresh; }));
        CHECK(!error->property("visible").toBool());
        CHECK(QDir(disk).exists());
        app.volumes.append({disk, "USB"});
        invoke("refreshLocations");
    }

    // Removal while closed must be detected on reopening, even if its mount
    // directory remains on disk. No background polling while closed.
    navigate(disk);
    close();
    const int closedScan = app.scans;
    app.volumes.clear();
    QTest::qWait(1100);
    CHECK(app.scans == closedScan);
    open();
    CHECK(model->rootPath() == app.homeDir());
    expectError("The original path is no longer available. Returned to Home.");
    close();

    // Reinsert and verify normal acceptance in open/save/folder modes.
    app.volumes.append({disk, "USB"});
    for (int mode : {0, 1, 2}) {
        picker->setProperty("mode", mode);
        picker->setProperty("initialFolder", QUrl::fromLocalFile(disk));
        open();
        CHECK(model->rootPath() == disk);
        if (mode != 2) field->setProperty("text", "pending.img");
        invoke("confirmAccept");
        CHECK(AwaitUi([&] { return !picker->property("visible").toBool(); }));
        const QString value = picker->property(mode == 2 ? "selectedFolder" : "selectedFile").toUrl().toLocalFile();
        CHECK(value == (mode == 2 ? disk : disk + "/pending.img"));
        CHECK(window->property("acceptedCount").toInt() == mode + 1);
    }
    picker->setProperty("initialFolder", QUrl::fromLocalFile(disk + "/missing"));
    open();
    CHECK(model->rootPath() == app.homeDir());
    CHECK(AwaitUi([&] { return error->property("visible").toBool(); }));
    close();
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<gui::DirModel>("UpgradeTool", 1, 0, "DirModel");
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    for (const auto &folder : {"home", "disk", "other"}) CHECK(QDir(temporary.path()).mkdir(folder));
    QFile file(temporary.filePath("disk/pending.img"));
    CHECK(file.open(QIODevice::WriteOnly));
    file.close();
    PickerApp app(temporary.filePath("home"));
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QObject::connect(&engine, &QQmlEngine::warnings, &application, [](const QList<QQmlError> &errors) {
        for (const auto &error : errors) {
            std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData());
            ++failures;
        }
    });
    TestPicker(engine, app, temporary.filePath("disk"), temporary.filePath("other"));
    return failures ? 1 : 0;
}

#include "test_file_picker.moc"
