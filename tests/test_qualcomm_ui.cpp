#include "tests/FakeDiagSession.h"
#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QImage>
#include <QColor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QtTest/QTest>
#include <memory>
#include <QSettings>
#include <QVariantList>
#include <libusb.h>
#include <cstdio>
class CRKLog;
CRKLog *g_pLogObject = nullptr;
static int failures;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(0)
template<class F> bool Await(F predicate) {
    QElapsedTimer timer; timer.start();
    while(!predicate() && timer.elapsed()<8000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    return predicate();
}
// Repeater delegates belong to the visual tree, not QObject::children().
static QQuickItem *FindVisualItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems())
        if (auto *match = FindVisualItem(child, name)) return match;
    return nullptr;
}
static void MovePointer(QQuickWindow *window, const QPointF &position) {
    QMouseEvent event(QEvent::MouseMove, position, window->mapToGlobal(position.toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
}
// Exercise the shared controls without submitting device operations.
static void TestFocusBorders(QQmlEngine &engine) {
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import UpgradeTool
Window {
    width: 520; height: 520; visible: true
    property color focusBorder: Theme.primary
    property color normalBorder: Theme.border
    property int activations: 0
    Column {
        x: 20; y: 20; spacing: 8
        AppTextField { objectName: "focusStart"; width: 250 }
        PrimaryButton { objectName: "focusPrimary"; focusPolicy: Qt.StrongFocus; text: "Primary"; onClicked: ++activations }
        SecondaryButton { objectName: "focusSecondary"; focusPolicy: Qt.StrongFocus; text: "Secondary"; onClicked: ++activations }
        AppCheckBox { objectName: "focusCheck"; focusPolicy: Qt.StrongFocus; text: "Check" }
        AppComboBox { objectName: "focusCombo"; model: ["First", "Second"] }
        DeviceComboBox { objectName: "focusDevice"; model: ["Device 1", "Device 2"] }
        SegmentedControl { objectName: "focusSegments"; model: ["First", "Second"] }
        DisclosureSection { objectName: "focusDisclosure"; width: 250; title: "Advanced" }
        AppTextField { objectName: "focusInput"; width: 250 }
    }
}
)QML", QUrl());
    std::unique_ptr<QObject> instance(component.create());
    CHECK(instance);
    if (!instance) {
        std::fprintf(stderr, "%s\n", component.errorString().toUtf8().constData());
        return;
    }
    auto *window = qobject_cast<QQuickWindow *>(instance.get());
    CHECK(window);
    if (!window) return;
    window->requestActivate();
    CHECK(Await([&] { return window->isActive() && !window->grabWindow().isNull(); }));
    auto find = [&](const char *name) { return window->findChild<QQuickItem *>(name); };
    auto *disclosure = find("focusDisclosure");
    QQuickItem *header = nullptr;
    for (auto *child : disclosure->childItems())
        if (child->property("visualFocus").isValid()) header = child;
    CHECK(header);
    if (!header) return;
    const QList<QQuickItem *> controls{find("focusPrimary"), find("focusSecondary"), find("focusCheck"),
        find("focusCombo"), find("focusDevice"), find("focusSegments"), header};
    auto border = [](QQuickItem *item, const char *property) {
        return QQmlProperty(item->property("background").value<QObject *>(), QString::fromLatin1(property)).read();
    };
    auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers, code == Qt::Key_A ? "a" : ""), release(QEvent::KeyRelease, code, modifiers);
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
        QCoreApplication::processEvents();
    };
    auto click = [&](QQuickItem *item) {
        const QPointF point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
        auto *popup = item->property("popup").value<QObject *>();
        if (popup) {
            CHECK(popup->property("visible").toBool());
            CHECK(QMetaObject::invokeMethod(popup, "close"));
        }
        QTest::mouseMove(window, QPoint(500, 500));
        QCoreApplication::processEvents();
    };
    // Real Tab traversal keeps all existing keyboard entry points reachable.
    find("focusStart")->forceActiveFocus(Qt::TabFocusReason);
    for (auto *control : controls) {
        CHECK(control);
        if (!control) return;
        key(Qt::Key_Tab);
        CHECK(control->hasActiveFocus());
        CHECK(control->property("visualFocus").toBool());
    }
    key(Qt::Key_Tab);
    CHECK(find("focusInput")->hasActiveFocus());
    key(Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK(header->hasActiveFocus() && header->property("visualFocus").toBool());
    for (int i = 0; i < controls.size(); ++i) {
        auto *control = controls[i];
        control->forceActiveFocus(Qt::TabFocusReason);
        CHECK(control->property("visualFocus").toBool());
        if (i < 2) CHECK(border(control, "border.width").toInt() == 2);
        else CHECK(border(control, "border.color").value<QColor>() == window->property("focusBorder").value<QColor>());
        click(control);
        CHECK(control->hasActiveFocus());
        if (control->property("visualFocus").toBool()) std::fprintf(stderr, "Mouse focus still visible: %s\n", control->objectName().toUtf8().constData());
        CHECK(!control->property("visualFocus").toBool());
        if (i < 3 || control == header)
            CHECK(border(control, "border.width").toInt() == (i == 1 ? 1 : 0));
        else
            CHECK(border(control, "border.color").value<QColor>() == window->property("normalBorder").value<QColor>());
        key(Qt::Key_Tab);
        key(Qt::Key_Backtab, Qt::ShiftModifier);
        CHECK(control->hasActiveFocus() && control->property("visualFocus").toBool());
    }
    CHECK(window->property("activations").toInt() == 2);
    auto *input = find("focusInput");
    click(input);
    CHECK(input->hasActiveFocus());
    CHECK(border(input, "border.color").value<QColor>() == window->property("focusBorder").value<QColor>());
    key(Qt::Key_A);
    CHECK(input->property("text").toString() == "a");
    controls[0]->forceActiveFocus(Qt::TabFocusReason);
    const int activations = window->property("activations").toInt();
    key(Qt::Key_Space);
    CHECK(window->property("activations").toInt() == activations + 1);
    controls[2]->forceActiveFocus(Qt::TabFocusReason);
    const bool checked = controls[2]->property("checked").toBool();
    key(Qt::Key_Space);
    CHECK(controls[2]->property("checked").toBool() != checked);
    controls[3]->forceActiveFocus(Qt::TabFocusReason);
    key(Qt::Key_Down);
    CHECK(controls[3]->property("currentIndex").toInt() == 1);
    controls[5]->forceActiveFocus(Qt::TabFocusReason);
    key(Qt::Key_Left);
    CHECK(controls[5]->property("currentIndex").toInt() == 0);
    key(Qt::Key_Right);
    CHECK(controls[5]->property("currentIndex").toInt() == 1);
    header->forceActiveFocus(Qt::TabFocusReason);
    const bool expanded = disclosure->property("expanded").toBool();
    key(Qt::Key_Return);
    CHECK(disclosure->property("expanded").toBool() != expanded);
}
static void Write(const QString &path,const QByteArray &data) { QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(data)==data.size()); }
int main(int argc,char **argv) {
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME",temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME",temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QGuiApplication application(argc,argv);
    QCoreApplication::setApplicationName("UpgradeTool-GUI");
    QQuickStyle::setStyle("Basic");
    qmlRegisterUncreatableMetaObject(gui::Operations::staticMetaObject,"UpgradeTool",1,0,"Operations","Operation IDs");
    qmlRegisterType<gui::DirModel>("UpgradeTool",1,0,"DirModel");
    if(libusb_init(nullptr)<0) return 2;
    QDir fixture(temporary.filePath("build with spaces")); CHECK(QDir().mkpath(fixture.path()));
    Write(fixture.filePath("prog_firehose.mbn"),QByteArray(512,'L'));
    Write(fixture.filePath("boot.bin"),QByteArray(512,'B'));
    QByteArray raw="<data><program SECTOR_SIZE_IN_BYTES=\"512\" filename=\"boot.bin\" label=\"boot\" num_partition_sectors=\"1\" physical_partition_number=\"0\" sparse=\"false\" file_sector_offset=\"0\" start_sector=\"34\"/></data>";
    Write(fixture.filePath("rawprogram_CN.xml"),raw); Write(fixture.filePath("rawprogram_US.xml"),raw);
    Write(fixture.filePath("patch0.xml"),"<patches/>");
    {
        // Device discovery and switching are fully simulated; never reboot host devices.
        std::atomic<int> endpointMask(0), diagSwitches(0), adbSwitches(0);
        std::atomic_bool requestSent(false), showEdl(true);
        gui::EdlEndpoint diag;
        diag.id = "diag:board"; diag.label = "Board diagnostic interface";
        diag.location = "usb:1-2"; diag.vid = 0x05c6; diag.pid = 0x9091; diag.suggested = true;
        auto adb = diag; adb.kind = gui::EdlEndpoint::Adb; adb.id = "adb:board"; adb.serial = "board";
        auto edl = diag; edl.downloadInterface = true; edl.vid=0x05c6; edl.pid = 0x9008;
        gui::EdlHooks hooks;
        hooks.discover = [&](const QString &, const gui::EdlCancel &, QString &) {
            QVector<gui::EdlEndpoint> endpoints;
            const int mask = endpointMask.load();
            if (mask & 1) endpoints.append(diag);
            if (mask & 2) endpoints.append(adb);
            if (mask & 4) { auto second = diag; second.id = "diag:second-port"; endpoints.append(second); }
            if (mask & 8) { auto second = diag; second.id = "diag:other-board"; second.location = "usb:1-3"; endpoints.append(second); }
            if (mask & 16) { auto unknown = diag; unknown.suggested = false; endpoints.append(unknown); }
            return endpoints;
        };
        hooks.openDiag = FakeDiagFactory([&](const gui::EdlEndpoint &, const QByteArray &request, const gui::EdlCancel &, QString &, bool &sent) {
            sent = true;
            if (request == QByteArray(1, 0)) return QByteArray(55, 0);
            ++diagSwitches; requestSent = true; return request;
        });
        hooks.reboot = [&](const QString &, const QString &serial, const gui::EdlCancel &, QString &) {
            CHECK(serial == "board"); ++adbSwitches; requestSent = true; return true;
        };
        hooks.scan = [&] { return requestSent.load() && showEdl.load() ? QVector<gui::EdlEndpoint>{edl} : QVector<gui::EdlEndpoint>{}; };
        gui::AppController controller(nullptr, hooks, 5000);
        // Keep periodic and vendor-triggered hardware scans from replacing the
        // device snapshots injected by these UI tests.
        CHECK(QObject::disconnect(&controller, SIGNAL(requestScan()), nullptr, nullptr));
        CHECK(Await([&]{return !controller.busy();})); controller.setVendor(2); controller.ignorePermission();
        auto *qc=controller.qualcomm();
        controller.language()->setMode("zh");
        CHECK(QCoreApplication::translate("QualcommBackend","Selected device does not use the requested transport")==QStringLiteral("所选设备与指定传输方式不匹配"));
        qc->addXml(temporary.filePath("missing.xml")); CHECK(qc->error().startsWith(QStringLiteral("无法打开")));
        controller.language()->setMode("en"); CHECK(qc->error().startsWith("Cannot open"));
        qc->removeXml(0);
        qc->loadFirmware(fixture.path()); CHECK(qc->xmlFiles().isEmpty());
        CHECK(qc->settings().value("programmer").toString()==fixture.filePath("prog_firehose.mbn"));
        qc->addXml(fixture.filePath("rawprogram_CN.xml")); qc->addXml(fixture.filePath("patch0.xml"));
        CHECK(qc->xmlFiles().size()==2 && qc->preview().size()==1); CHECK(qc->error().isEmpty());
        qc->moveXml(1,-1); CHECK(qc->xmlFiles().first().endsWith("patch0.xml")); qc->moveXml(0,1);
        qc->addXml(fixture.filePath("rawprogram_CN.xml")); CHECK(qc->xmlFiles().size()==2);
        QQmlApplicationEngine engine;
        QObject::connect(controller.language(), &gui::LanguageManager::languageChanged, &engine, &QQmlApplicationEngine::retranslate);
        QObject::connect(&engine,&QQmlEngine::warnings,&application,[&](const QList<QQmlError> &errors) {
            for(const auto &error:errors) { std::fprintf(stderr,"QML: %s\n",error.toString().toUtf8().constData()); ++failures; }
        });
        engine.rootContext()->setContextProperty("app",&controller); engine.rootContext()->setContextProperty("startTab",1);
        engine.load(QUrl("qrc:/qt/qml/UpgradeTool/Main.qml")); CHECK(!engine.rootObjects().isEmpty());
        if(engine.rootObjects().isEmpty()) return 1;
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); CHECK(window);
        auto *download=window->findChild<QQuickItem *>("qcDownload");
        CHECK(download); if(!download) return 1;
        // Settings are complete, but no EDL device is enumerated, so the
        // device operations stay disabled (the backend would refuse them).
        CHECK(!download->isEnabled());
        auto *rawStart = window->findChild<QQuickItem *>("qcRawStart");
        CHECK(rawStart && !rawStart->isEnabled());
        auto *toolStart = FindVisualItem(window->contentItem(), "qcToolStart-chipinfo");
        CHECK(toolStart && !toolStart->isEnabled());
        {
            auto update=[&](const QVariantList &devices) {
                CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList,devices)));
            };
            QVariantMap diag{{"vendor",2},{"index",1},{"key","diag"},{"vid",0x05c6},{"pid",0x9091},{"qualcommMode",QTQ_DIAG}};
            auto edl=diag; edl["key"]="edl"; edl["index"]=2; edl["pid"]=0x9008; edl["qualcommMode"]=QTQ_EDL;
            update({diag,edl}); controller.setDeviceIndex(0);
            CHECK(!download->isEnabled());
            controller.setDeviceIndex(1); CHECK(download->isEnabled());
            edl["qualcommMode"]=QTQ_DUMP; edl["pid"]=0x900e; update({diag,edl});
            CHECK(!download->isEnabled());
            edl["qualcommMode"]=QTQ_EDL; edl["pid"]=0x9008; edl["accessError"]=-3; update({diag,edl});
            CHECK(!download->isEnabled());
            update({});
        }
        auto *loadXml = window->findChild<QObject *>("qcLoadXml");
        auto *programPicker = window->findChild<QObject *>("qcProgramPicker");
        auto *patchPicker = window->findChild<QObject *>("qcPatchPicker");
        CHECK(loadXml && programPicker && patchPicker);
        auto *qcPage = window->findChild<QObject *>("QualcommFlashPage"); CHECK(qcPage);
        auto *switchEdl = qcPage->findChild<QQuickItem *>("qcSwitchEdl"); CHECK(switchEdl && !switchEdl->isEnabled());
        CHECK(!window->findChild<QObject *>("qcSwitchConfig"));
        auto *switchMethod = qcPage->findChild<QQuickItem *>("qcSwitchMethod"); CHECK(switchMethod);
        auto *edlDialog = window->findChild<QObject *>("qcEdlDialog"); CHECK(edlDialog);
        auto *edlDevice = edlDialog->findChild<QObject *>("qcEdlDevice"); CHECK(edlDevice);
        auto *edlPort = edlDialog->findChild<QObject *>("qcEdlPort"); CHECK(edlPort);
        auto *edlStart = edlDialog->findChild<QQuickItem *>("qcEdlStart"); CHECK(edlStart);
        auto refreshSwitch = [&] {
            CHECK(Await([&]{ return !controller.edl()->discovering(); }));
            controller.refreshEdl();
            CHECK(Await([&]{ return !controller.edl()->discovering(); }));
            QCoreApplication::processEvents();
        };
        refreshSwitch();
        CHECK(!controller.edl()->ready());
        QString switchError;
        auto switchErrorConnection = QObject::connect(&controller, &gui::AppController::toastRequested, &application,
            [&](const QString &text, bool error) { if (error) switchError = text; });
        controller.requestEdlSwitch();
        CHECK(!controller.busy() && !switchError.isEmpty());
        QObject::disconnect(switchErrorConnection);
        auto *switchTooltip = qcPage->findChild<QObject *>("qcSwitchTooltip"); CHECK(switchTooltip);
        auto *startTooltip = edlDialog->findChild<QObject *>("qcEdlStartTooltip"); CHECK(startTooltip);
        auto hoverButton = [&](QQuickItem *button, QObject *tooltip, const QString &expected) {
            QElapsedTimer layout; layout.start(); Await([&]{return layout.elapsed() > 60;});
            CHECK(!button->isEnabled());
            MovePointer(window, button->mapToScene(QPointF(button->width()/2, button->height()/2)));
            CHECK(Await([&]{ return tooltip->property("visible").toBool(); }));
            CHECK(tooltip->property("text").toString().contains(expected));
            MovePointer(window, QPointF(2, 2));
            CHECK(Await([&]{ return !tooltip->property("visible").toBool(); }));
        };
        hoverButton(switchEdl, switchTooltip, QStringLiteral("No diagnostic interface"));
        controller.edl()->setMethod(gui::EdlRunner::Adb);
        hoverButton(switchEdl, switchTooltip, QStringLiteral("No authorized ADB device"));
        controller.edl()->setMethod(gui::EdlRunner::Diag);
        controller.language()->setMode("zh");
        hoverButton(switchEdl, switchTooltip, QStringLiteral("未检测到诊断接口"));
        controller.language()->setMode("en");
        // First entry discovers candidates without ever opening a settings dialog.
        endpointMask = 1;
        CHECK(Await([&]{ return switchEdl->isEnabled(); })); // automatic polling
        MovePointer(window, switchEdl->mapToScene(QPointF(switchEdl->width()/2, switchEdl->height()/2)));
        QElapsedTimer hover; hover.start(); Await([&]{return hover.elapsed() > 60;});
        CHECK(!switchTooltip->property("visible").toBool());
        MovePointer(window, QPointF(2, 2));
        QMetaObject::invokeMethod(switchEdl, "clicked");
        CHECK(controller.busy() && !switchEdl->isEnabled());
        CHECK(!edlDialog->property("visible").toBool());
        CHECK(Await([&]{ return !controller.busy(); }));
        CHECK(diagSwitches == 1 && adbSwitches == 0);
        refreshSwitch();
        // ADB availability is independent of the diagnostic candidates.
        endpointMask = 2; refreshSwitch();
        CHECK(!switchEdl->isEnabled());
        controller.edl()->setMethod(gui::EdlRunner::Adb);
        CHECK(switchEdl->isEnabled());
        CHECK(switchMethod->property("selectedIndex").toInt() == 1);
        QMetaObject::invokeMethod(switchEdl, "clicked");
        CHECK(Await([&]{ return !controller.busy(); }));
        CHECK(diagSwitches == 1 && adbSwitches == 1);
        controller.edl()->setMethod(gui::EdlRunner::Diag);
        // Ambiguous ports require explicit selection, which unplugging invalidates.
        endpointMask = 5; refreshSwitch();
        QMetaObject::invokeMethod(switchEdl, "clicked");
        CHECK(Await([&]{ return edlDialog->property("visible").toBool(); }));
        CHECK(!edlStart->isEnabled());
        hoverButton(edlStart, startTooltip, QStringLiteral("Select one diagnostic interface"));
        edlPort->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(edlPort, "activated", Q_ARG(int, 1));
        CHECK(edlStart->isEnabled());
        refreshSwitch();
        CHECK(edlStart->isEnabled() && edlPort->property("currentIndex").toInt() == 1);
        // Losing the chosen port cannot select the other port automatically.
        endpointMask = 4; refreshSwitch();
        CHECK(!edlStart->isEnabled());
        endpointMask = 0; refreshSwitch();
        CHECK(!edlStart->isEnabled() && !switchEdl->isEnabled());
        QMetaObject::invokeMethod(edlDialog, "close");
        // Multiple devices never silently choose the first one.
        endpointMask = 9; refreshSwitch();
        QMetaObject::invokeMethod(switchEdl, "clicked");
        CHECK(Await([&]{ return edlDialog->property("visible").toBool(); }));
        CHECK(edlDevice->property("currentIndex").toInt() == -1 && !edlStart->isEnabled());
        hoverButton(edlStart, startTooltip, QStringLiteral("Select a device first"));
        edlDevice->setProperty("currentIndex", 0);
        QMetaObject::invokeMethod(edlDevice, "activated", Q_ARG(int, 0));
        CHECK(edlStart->isEnabled());
        QMetaObject::invokeMethod(edlDialog, "close");
        CHECK(diagSwitches == 1); // closing a selection never switches
        // An unknown USB interface must be explicitly chosen even when unique.
        endpointMask = 16; refreshSwitch();
        QMetaObject::invokeMethod(switchEdl, "clicked");
        CHECK(Await([&]{ return edlDialog->property("visible").toBool(); }));
        CHECK(!edlStart->isEnabled());
        edlPort->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(edlPort, "activated", Q_ARG(int, 1));
        CHECK(edlStart->isEnabled());
        QMetaObject::invokeMethod(edlStart, "clicked");
        CHECK(Await([&]{ return !controller.busy(); }));
        CHECK(diagSwitches == 2);
        // Switching must preserve action geometry without additional status rows.
        endpointMask = 1; showEdl = false; refreshSwitch();
        window->setWidth(1024); window->setHeight(640);
        for (const auto &language : {"en", "zh"}) for (const auto &theme : {"light", "dark"}) {
            controller.language()->setMode(language);
            controller.theme()->setMode(theme);
            for (int page = 0; page < 3; ++page) {
                window->setProperty("currentPage", page);
                auto *workspace = window->findChild<QQuickItem *>(page == 0 ? "QualcommPartitionsPage" : page == 1 ? "QualcommFlashPage" : "QualcommToolsPage");
                auto *button = workspace->findChild<QQuickItem *>("qcSwitchEdl");
                auto *primary = FindVisualItem(workspace, page == 0 ? "qcRawStart" : page == 1 ? "qcDownload" : "qcToolStart-chipinfo");
                QElapsedTimer layout; layout.start(); Await([&]{return layout.elapsed() > 60;});
                const QPointF before = button->mapToScene(QPointF());
                const QPointF primaryBefore = primary->mapToScene(QPointF());
                requestSent = false;
                QMetaObject::invokeMethod(button, "clicked");
                CHECK(controller.busy());
                CHECK(Await([&]{ return requestSent.load(); }));
                layout.restart(); Await([&]{return layout.elapsed() > 60;});
                CHECK(qAbs(before.x() - button->mapToScene(QPointF()).x()) < 1);
                CHECK(qAbs(before.y() - button->mapToScene(QPointF()).y()) < 1);
                CHECK(qAbs(primaryBefore.x() - primary->mapToScene(QPointF()).x()) < 1);
                if (page < 2) CHECK(button->mapToScene(QPointF(button->width(), 0)).x() <= primary->mapToScene(QPointF()).x());
                CHECK(primary->mapToScene(QPointF(primary->width(), 0)).x() <= workspace->mapToScene(QPointF(workspace->width(), 0)).x());
                CHECK(!primary->isEnabled() && !button->isEnabled());
                if(page==2) {
                    CHECK(QMetaObject::invokeMethod(workspace,"selectTool",Q_ARG(QVariant,QVariant(4))));
                    CHECK(workspace->property("selectedTool").toInt()==0);
                    auto *selector=workspace->findChild<QQuickItem *>("qcToolSelector");
                    CHECK(selector && !selector->isEnabled());
                    for(const auto &context : {"chipinfo", "reset", "ramdump", "kickstart", "zip", "archive", "digests", "provision"}) {
                        auto *action=FindVisualItem(workspace, "qcToolStart-"+QString::fromLatin1(context));
                        CHECK(action && !action->isEnabled());
                    }
                }
                if (argc > 2) {
                    QDir output(QString::fromLocal8Bit(argv[2])); CHECK(QDir().mkpath(output.path()));
                    CHECK(window->grabWindow().save(output.filePath(QString("qualcomm-switching-%1-%2-%3.png").arg(language, theme).arg(page))));
                }
                controller.cancel(); CHECK(Await([&]{ return !controller.busy(); }));
                refreshSwitch();
            }
        }
        controller.language()->setMode("en");
        controller.theme()->setMode("light");
        window->setProperty("currentPage", 1);
        window->setWidth(1300); window->setHeight(780);
        showEdl = true; endpointMask = 0; refreshSwitch();
        if (loadXml && programPicker && patchPicker) {
            controller.rememberPath("qualcomm/flash/rawprogram", temporary.path());
            const auto oldFiles = qc->xmlFiles();
            QMetaObject::invokeMethod(loadXml, "clicked");
            CHECK(Await([&]{return programPicker->property("visible").toBool();}));
            CHECK(programPicker->property("currentFolder").toString() == fixture.path());
            programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("rawprogram_US.xml")});
            QMetaObject::invokeMethod(programPicker, "confirmAccept");
            CHECK(Await([&]{return patchPicker->property("visible").toBool();}));
            CHECK(patchPicker->property("currentFolder").toString() == fixture.path());
            CHECK(qc->xmlFiles() == oldFiles);
            QMetaObject::invokeMethod(patchPicker, "close");
            CHECK(Await([&]{return !patchPicker->property("visible").toBool();}));
            CHECK(qc->xmlFiles() == oldFiles);
            QMetaObject::invokeMethod(loadXml, "clicked");
            CHECK(Await([&]{return programPicker->property("visible").toBool();}));
            programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("rawprogram_US.xml")});
            QMetaObject::invokeMethod(programPicker, "confirmAccept");
            CHECK(Await([&]{return patchPicker->property("visible").toBool();}));
            patchPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("patch0.xml")});
            QMetaObject::invokeMethod(patchPicker, "confirmAccept");
            CHECK(Await([&]{return !patchPicker->property("visible").toBool();}));
            CHECK(qc->xmlFiles() == QStringList({fixture.filePath("rawprogram_US.xml"),fixture.filePath("patch0.xml")}));
            // The Download list is read-only (re-run Load XML to replace) and
            // its pickers stay multi-select for UFS builds with several LUNs.
            CHECK(programPicker->property("multiple").toBool());
            if (auto *flashList = window->findChild<QObject *>("qcFlashXmlList"))
                CHECK(!flashList->property("showRowActions").toBool());
            else
                CHECK(false);
            const auto selected = qc->xmlFiles();
            QMetaObject::invokeMethod(loadXml, "clicked");
            CHECK(Await([&]{return programPicker->property("visible").toBool();}));
            QMetaObject::invokeMethod(programPicker, "close");
            CHECK(Await([&]{return !programPicker->property("visible").toBool();}));
            CHECK(!patchPicker->property("visible").toBool()); CHECK(qc->xmlFiles() == selected);
            QMetaObject::invokeMethod(loadXml, "clicked");
            CHECK(Await([&]{return programPicker->property("visible").toBool();}));
            programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("missing.xml")});
            QMetaObject::invokeMethod(programPicker, "confirmAccept");
            CHECK(programPicker->property("visible").toBool());
            CHECK(!programPicker->property("selectionError").toString().isEmpty()); CHECK(qc->xmlFiles() == selected);
            programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("rawprogram_CN.xml")});
            QMetaObject::invokeMethod(programPicker, "confirmAccept");
            CHECK(Await([&]{return patchPicker->property("visible").toBool();}));
            QMetaObject::invokeMethod(patchPicker, "skipped"); QMetaObject::invokeMethod(patchPicker, "close");
            CHECK(Await([&]{return !patchPicker->property("visible").toBool();}));
            CHECK(qc->xmlFiles() == QStringList({fixture.filePath("rawprogram_CN.xml")}));
            // Double-clicking a row confirms the selection, mirroring QFIL.
            QMetaObject::invokeMethod(loadXml, "clicked");
            CHECK(Await([&]{return programPicker->property("visible").toBool();}));
            QMetaObject::invokeMethod(programPicker, "activateEntry",
                Q_ARG(QVariant, QStringLiteral("rawprogram_CN.xml")),
                Q_ARG(QVariant, fixture.filePath("rawprogram_CN.xml")), Q_ARG(QVariant, false));
            CHECK(Await([&]{return patchPicker->property("visible").toBool();}));
            QMetaObject::invokeMethod(patchPicker, "close");
            CHECK(Await([&]{return !patchPicker->property("visible").toBool();}));
            CHECK(qc->xmlFiles() == QStringList({fixture.filePath("rawprogram_CN.xml")}));
            CHECK(qc->replaceFlashXml({fixture.filePath("rawprogram_CN.xml")},{fixture.filePath("patch0.xml")}));
        }
        core::TaskJob submitted;
        QObject::connect(&controller,&gui::AppController::requestExecute,&application,[&](const core::TaskJob &job){submitted=job;});
        qc->setValue("dryRun",true);
        qc->run(static_cast<int>(core::Operation::QualcommFlash)); CHECK(controller.busy());
        CHECK(!download->isEnabled()); controller.setVendor(0); CHECK(controller.vendor()==2);
        auto prior=qc->xmlFiles(); qc->removeXml(0); CHECK(qc->xmlFiles()==prior);
        CHECK(Await([&]{return !controller.busy();})); CHECK(submitted.targets.empty()); CHECK(controller.progressValue()==100);
        CHECK(controller.progressText().startsWith("Simulation:"));
        CHECK(submitted.steps.front().qualcomm.xml.size()==2);
        auto *flashPage=window->findChild<QObject *>("QualcommFlashPage"); CHECK(flashPage);
        auto *dryRun=flashPage ? flashPage->findChild<QQuickItem *>("qcDryRun") : nullptr; CHECK(dryRun);
        CHECK(!window->findChild<QObject *>("qcActionSummary"));
        CHECK(!window->findChild<QObject *>("qcSwitchHint"));
        CHECK(edlDialog->property("hintText").toString().isEmpty());
        // The target and the common options now share the Advanced options
        // disclosure, so it has to be expanded before they are visible.
        CHECK(flashPage && !flashPage->findChild<QObject *>("qcOptions"));
        auto *flashAdvanced=flashPage ? flashPage->findChild<QObject *>("qcAdvanced") : nullptr; CHECK(flashAdvanced);
        if(flashAdvanced) flashAdvanced->setProperty("expanded",true);
        auto *debug=flashPage ? flashPage->findChild<QQuickItem *>("qcDebug") : nullptr;
        CHECK(debug && debug->isVisible());
        // Visible-from-start rows must still resolve a Browse context.
        auto *programmerPicker=flashPage ? flashPage->findChild<QObject *>("qcPathPicker-programmer") : nullptr;
        CHECK(programmerPicker && programmerPicker->property("requestContext").toString()==QStringLiteral("flash"));
        qc->setValue("dryRun",QStringLiteral("false"));
        CHECK(dryRun && !dryRun->property("checked").toBool()); CHECK(!download->isEnabled());
        qc->setValue("dryRun",true); CHECK(dryRun && dryRun->property("checked").toBool()); CHECK(download->isEnabled());
        qc->setValue("dryRun",false);
        qc->setValue("dryRun",true);
        qc->setValue("images","13:/missing/stale.mbn"); CHECK(download->isEnabled());
        {
            // Invalid input disables the primary action; the backend still
            // refuses it and surfaces the reason as a toast.
            QString toastText; bool toastError=false;
            auto connection=QObject::connect(&controller,&gui::AppController::toastRequested,&application,
                [&](const QString &text,bool error){toastText=text;toastError=error;});
            qc->setValue("useImages",true); CHECK(!download->isEnabled());
            QMetaObject::invokeMethod(download,"clicked");
            CHECK(toastError && !toastText.isEmpty() && !controller.busy());
            QObject::disconnect(connection);
            qc->setValue("useImages",false); CHECK(download->isEnabled());
        }
        qc->setValue("dryRun",false);
        qc->setValue("allowFusing",true); qc->setValue("finalize",true);
        {
            // Qualcomm inputs are session-only: a fresh controller starts clean.
            gui::QualcommController restored;
            CHECK(restored.xmlFiles().isEmpty());
            CHECK(restored.settings().value("programmer").toString().isEmpty());
            CHECK(!restored.settings().value("allowFusing").toBool()); CHECK(!restored.settings().value("finalize").toBool());
        }
        for(int page=0;page<3;++page) { window->setProperty("currentPage",page); QCoreApplication::processEvents(); }
        auto *toolsPage=window->findChild<QQuickItem *>("QualcommToolsPage"); CHECK(toolsPage);
        CHECK(!window->findChild<QObject *>("qcTool"));
        const QStringList toolContexts = {"chipinfo", "reset", "ramdump", "kickstart", "zip", "archive", "digests", "provision"};
        const core::Operation toolOperations[] = {core::Operation::QualcommChipInfo, core::Operation::QualcommReset,
            core::Operation::QualcommRamdump, core::Operation::QualcommKickstart, core::Operation::QualcommCreateZip,
            core::Operation::QualcommCreateArchive, core::Operation::QualcommCreateDigests, core::Operation::QualcommProvision};
        auto selectTool = [&](int index) {
            CHECK(QMetaObject::invokeMethod(toolsPage, "selectTool", Q_ARG(QVariant, QVariant(index))));
            QCoreApplication::processEvents();
            CHECK(toolsPage->property("selectedTool").toInt() == index);
        };
        auto *toolNavigation = toolsPage->findChild<QQuickItem *>("qcToolNavigation");
        auto *toolSelector = toolsPage->findChild<QQuickItem *>("qcToolSelector");
        CHECK(toolNavigation && toolNavigation->isVisible());
        CHECK(toolSelector && !toolSelector->isVisible());
        CHECK(toolsPage->property("selectedTool").toInt() == 0);
        if (argc>2) {
            // Let earlier validation toasts close before capturing the tool forms.
            QElapsedTimer toast; toast.start(); Await([&]{return toast.elapsed()>2800;});
            controller.log()->clear();
        }
        auto *resetNavigation=FindVisualItem(toolsPage, "qcToolSelect-reset");
        CHECK(resetNavigation);
        if (resetNavigation) {
            resetNavigation->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
            CHECK(toolsPage->property("selectedTool").toInt() == 1);
            selectTool(0);
        }
        for (int i=0; i<toolContexts.size(); ++i) {
            auto *navigation = FindVisualItem(toolsPage, "qcToolSelect-" + toolContexts[i]);
            CHECK(navigation && navigation->isVisible());
            if (navigation) QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                navigation->mapToScene(QPointF(navigation->width()/2, navigation->height()/2)).toPoint());
            QCoreApplication::processEvents();
            CHECK(toolsPage->property("selectedTool").toInt() == i);
            CHECK(toolsPage->property("context").toString() == toolContexts[i]);
            CHECK(toolsPage->property("showEdlSwitch").toBool() == (i < 4 || i == 7));
            for (int other=0; other<toolContexts.size(); ++other) {
                auto *otherPanel=FindVisualItem(toolsPage, "qcToolPanel-" + toolContexts[other]);
                auto *otherButton=FindVisualItem(toolsPage, "qcToolStart-" + toolContexts[other]);
                CHECK(otherPanel && otherPanel->isVisible() == (other == i));
                CHECK(otherButton && otherButton->isVisible() == (other == i));
            }
            auto *panel=FindVisualItem(toolsPage, "qcToolPanel-" + toolContexts[i]); CHECK(panel && panel->isVisible());
            auto *button=FindVisualItem(toolsPage, "qcToolStart-" + toolContexts[i]); CHECK(button && button->isVisible());
            if (panel) {
                CHECK(panel->property("operation").toInt()==int(toolOperations[i]));
                CHECK(panel->property("context").toString()==toolContexts[i]);
                auto *advanced=panel->findChild<QObject *>("qcAdvanced"); CHECK(advanced && !advanced->property("expanded").toBool());
            }
            if (argc>2) {
                QElapsedTimer layout; layout.start(); Await([&]{return layout.elapsed()>40;});
                CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[2])).filePath("tools-wide-"+toolContexts[i]+".png")));
            }
        }
        {
            selectTool(1);
            auto *resetPanel = FindVisualItem(toolsPage, "qcToolPanel-reset"); CHECK(resetPanel);
            CHECK(resetPanel && !resetPanel->property("showProgrammer").toBool());
            CHECK(!toolsPage->findChild<QObject *>("qcResetProgrammerHint"));
            auto *resetMethod = resetPanel ? resetPanel->findChild<QQuickItem *>("qcResetMethod") : nullptr;
            CHECK(resetMethod && resetMethod->isVisible() && resetMethod->property("currentIndex").toInt() == 0);
            auto *resetPicker = resetPanel ? resetPanel->findChild<QObject *>("qcPathPicker-programmer") : nullptr;
            CHECK(resetPicker && resetPicker->property("requestContext").toString() == "reset");
            auto *resetInput = resetPicker ? qobject_cast<QQuickItem *>(resetPicker->parent()) : nullptr;
            CHECK(resetInput && !resetInput->isVisible());
            auto *resetAdvanced = resetPanel ? resetPanel->findChild<QObject *>("qcAdvanced") : nullptr;
            CHECK(resetAdvanced && !resetAdvanced->property("storage").toBool());
            if (resetMethod) {
                resetMethod->setProperty("currentIndex", 1);
                CHECK(QMetaObject::invokeMethod(resetMethod, "activated", Q_ARG(int, 1)));
            }
            CHECK(qc->settingsFor("reset").value("resetMethod").toString() == "firehose");
            CHECK(resetInput && resetInput->isVisible());
            CHECK(resetAdvanced && resetAdvanced->property("storage").toBool());
            CHECK(!qc->canRun(int(core::Operation::QualcommReset)));
            qc->setContextValue("reset", "programmer", fixture.filePath("prog_firehose.mbn"));
            CHECK(qc->canRun(int(core::Operation::QualcommReset)));
            controller.language()->setMode("zh");
            CHECK(QCoreApplication::translate("QualcommToolsPage", "Reset method") == QStringLiteral("重启方式"));
            CHECK(resetMethod && resetMethod->property("currentIndex").toInt() == 1);
            controller.language()->setMode("en");
            if (resetMethod) {
                resetMethod->setProperty("currentIndex", 0);
                CHECK(QMetaObject::invokeMethod(resetMethod, "activated", Q_ARG(int, 0)));
            }
            CHECK(qc->settingsFor("reset").value("resetMethod").toString() == "sahara");
            CHECK(resetInput && !resetInput->isVisible());
            CHECK(resetAdvanced && !resetAdvanced->property("storage").toBool());
            CHECK(qc->settingsFor("reset").value("programmer").toString() == fixture.filePath("prog_firehose.mbn"));
            qc->setContextValue("reset", "programmer", temporary.filePath("missing-reset.mbn"));
            CHECK(qc->canRun(int(core::Operation::QualcommReset)));
        }
        {
            window->setProperty("currentPage",2);
            selectTool(7);
            auto *ufsPanel=FindVisualItem(toolsPage, "qcToolPanel-provision"); CHECK(ufsPanel);
            auto *ufsAdvanced=ufsPanel ? ufsPanel->findChild<QObject *>("qcAdvanced") : nullptr; CHECK(ufsAdvanced);
            if(ufsAdvanced) ufsAdvanced->setProperty("expanded",true);
            auto *ufsDry=ufsPanel ? ufsPanel->findChild<QQuickItem *>("qcDryRun") : nullptr; CHECK(ufsDry && ufsDry->isVisible());
            auto *ufsStart=FindVisualItem(toolsPage, "qcToolStart-provision"); CHECK(ufsStart);
            selectTool(0); selectTool(7);
            CHECK(ufsAdvanced && ufsAdvanced->property("expanded").toBool());
            qc->setValue("dryRun",true); CHECK(ufsDry && !ufsDry->property("checked").toBool());
            qc->setContextValue("provision","dryRun",true); CHECK(ufsDry && ufsDry->property("checked").toBool());
            CHECK(ufsStart && ufsStart->property("text").toString()==QStringLiteral("Simulate UFS provisioning"));
            auto *kickstart=FindVisualItem(toolsPage, "qcToolPanel-kickstart"); CHECK(kickstart);
            CHECK(kickstart && !kickstart->property("settings").toMap().value("dryRun").toBool());
            auto *buildType=flashPage->findChild<QObject *>("qcBuildType"); CHECK(buildType);
            auto *inputType=flashPage->findChild<QObject *>("qcProgrammerInput"); CHECK(inputType);
            auto *rawAction=window->findChild<QObject *>("qcRawAction"); CHECK(rawAction);
            if(rawAction) { rawAction->setProperty("currentIndex",2); QMetaObject::invokeMethod(rawAction,"activated",Q_ARG(int,2)); }
            qc->setValue("packageBuild",true); qc->setValue("useImages",true);
            auto *vendorSelector=window->findChild<QObject *>("vendorSelector"); CHECK(vendorSelector);
            controller.language()->setMode("zh"); CHECK(Await([&]{return ufsStart && ufsStart->property("text").toString()==QStringLiteral("模拟 UFS 配置");}));
            CHECK(Await([&]{return vendorSelector && vendorSelector->property("currentIndex").toInt()==2;}));
            CHECK(buildType && buildType->property("currentIndex").toInt()==1);
            CHECK(inputType && inputType->property("currentIndex").toInt()==1);
            CHECK(Await([&]{return rawAction && rawAction->property("currentIndex").toInt()==2;}));
            CHECK(QCoreApplication::translate("QualcommProgrammer","Programmer input")==QStringLiteral("引导程序来源"));
            CHECK(QCoreApplication::translate("gui::AppController","Simulation")==QStringLiteral("模拟执行"));
            CHECK(QCoreApplication::translate("gui::EdlRunner","Checking the selected device…")==QStringLiteral("正在检查所选设备…"));
            CHECK(QCoreApplication::translate("EdlSwitchDialog","Diagnostic port (DIAG)")==QStringLiteral("诊断口（DIAG）"));
            controller.language()->setMode("en"); CHECK(Await([&]{return ufsStart && ufsStart->property("text").toString()==QStringLiteral("Simulate UFS provisioning");}));
            CHECK(Await([&]{return vendorSelector && vendorSelector->property("currentIndex").toInt()==2;}));
            qc->setValue("packageBuild",false); qc->setValue("useImages",false);
            qc->setValue("dryRun",false); qc->setContextValue("provision","dryRun",false);
        }
        {
            // Complete each panel independently. Offline tools must be usable without a device.
            const QString loader=fixture.filePath("prog_firehose.mbn");
            Write(fixture.filePath("contents.xml"),"<contents/>");
            Write(fixture.filePath("ufs.xml"),"<data><ufs bNumberLU=\"1\"/></data>");
            for (const auto &context : {"kickstart", "archive", "digests", "provision"})
                qc->setContextValue(context,"programmer",loader);
            for (const auto &context : {"ramdump", "zip", "archive"})
                qc->setContextValue(context,"output",temporary.filePath(QString(context)+"-output"));
            qc->setContextValue("zip","package",fixture.filePath("contents.xml"));
            qc->setContextValue("digests","digestDir",temporary.path());
            qc->addXml(fixture.filePath("rawprogram_CN.xml"),"digests");
            qc->addXml(fixture.filePath("ufs.xml"),"provision");
            for (const auto &context : toolContexts) {
                selectTool(toolContexts.indexOf(context));
                auto *button=FindVisualItem(toolsPage, "qcToolStart-"+context); CHECK(button);
                const bool offline=context=="zip" || context=="archive" || context=="digests";
                CHECK(button && button->isEnabled()==offline);
                if (offline) CHECK(toolsPage->property("footerMessage").toString().isEmpty());
            }
#ifndef Q_OS_WIN
            qc->setContextValue("kickstart","node","/dev/test-node");
            auto *kickstartStart=FindVisualItem(toolsPage, "qcToolStart-kickstart");
            CHECK(kickstartStart && kickstartStart->isEnabled());
            qc->setContextValue("kickstart","node","");
            CHECK(kickstartStart && !kickstartStart->isEnabled());
#endif
            auto *ufsStart=FindVisualItem(toolsPage, "qcToolStart-provision");
            qc->setContextValue("provision","dryRun",true);
            CHECK(ufsStart && ufsStart->isEnabled());
            qc->setContextValue("provision","dryRun",false);
            CHECK(ufsStart && !ufsStart->isEnabled());
            auto *zipPanel=FindVisualItem(toolsPage, "qcToolPanel-zip");
            auto *archivePanel=FindVisualItem(toolsPage, "qcToolPanel-archive");
            auto *zipPicker=zipPanel->findChild<QObject *>("qcPathPicker-output");
            auto *archivePicker=archivePanel->findChild<QObject *>("qcPathPicker-output");
            CHECK(zipPicker && zipPicker->property("requestContext").toString()=="zip");
            CHECK(archivePicker && archivePicker->property("requestContext").toString()=="archive");
            CHECK(zipPicker && zipPicker->parent()->property("value").toString()==temporary.filePath("zip-output"));
            CHECK(archivePicker && archivePicker->parent()->property("value").toString()==temporary.filePath("archive-output"));
            qc->setContextValue("archive","archiveContents",true);
            qc->setContextValue("archive","package",fixture.filePath("contents.xml"));
            CHECK(archivePanel->property("packageBuild").toBool());
            qc->setContextValue("digests","packageBuild",true);
            qc->setContextValue("digests","package",fixture.filePath("contents.xml"));
            controller.language()->setMode("zh"); QCoreApplication::processEvents();
            controller.language()->setMode("en"); QCoreApplication::processEvents();
            CHECK(archivePanel->property("packageBuild").toBool());
            qc->setContextValue("archive","archiveContents",false);
            qc->setContextValue("digests","packageBuild",false);
            // Another vendor makes AppController ignore these requests, so verify the actual
            // button-to-request wiring without submitting any job or touching hardware.
            controller.setVendor(0);
            int requests=0; core::OperationRequest submitted;
            auto connection=QObject::connect(qc,&gui::QualcommController::executeRequested,&application,
                [&](const core::OperationRequest &request){++requests; submitted=request;});
            for (int i=0; i<toolContexts.size(); ++i) {
                auto *button=FindVisualItem(toolsPage, "qcToolStart-"+toolContexts[i]);
                CHECK(button && QMetaObject::invokeMethod(button,"clicked"));
                CHECK(requests==i+1 && submitted.operation==toolOperations[i]);
                CHECK(!controller.busy());
            }
            auto *resetStart = FindVisualItem(toolsPage, "qcToolStart-reset");
            qc->setContextValue("reset", "resetMethod", "firehose");
            qc->setContextValue("reset", "programmer", loader);
            CHECK(resetStart && QMetaObject::invokeMethod(resetStart,"clicked"));
            CHECK(requests == toolContexts.size()+1 && submitted.qualcomm.programmer == loader.toStdString());
            qc->setContextValue("reset", "resetMethod", "sahara");
            CHECK(resetStart && QMetaObject::invokeMethod(resetStart,"clicked"));
            CHECK(requests == toolContexts.size()+2 && submitted.qualcomm.programmer.empty() && submitted.qualcomm.images.empty());
            QObject::disconnect(connection); controller.setVendor(2);
            // Only the requested operation receives its result table.
            core::OperationResult result; result.operation=core::Operation::QualcommChipInfo;
            result.status=core::OperationStatus::Succeeded;
            result.items.push_back({"Chip", "test-chip"}); qc->applyResult(result);
            CHECK(qc->resultsFor("chipinfo")->rowCount()==1);
            CHECK(qc->resultsFor("reset")->rowCount()==0);
            selectTool(0); selectTool(1); selectTool(0);
            CHECK(qc->resultsFor("chipinfo")->rowCount()==1);
            result.items.clear(); qc->applyResult(result);
        }
        // Every operation must remain reachable in a narrow workspace, with no
        // horizontal overflow, including expanded advanced options and keyboard focus.
        window->setProperty("currentPage",2);
        window->setWidth(1300); window->setHeight(640);
        CHECK(Await([&]{return toolSelector->isVisible() && !toolNavigation->isVisible();}));
        auto *shortWindowAction=FindVisualItem(toolsPage, "qcToolStart-chipinfo");
        CHECK(shortWindowAction->mapToScene(QPointF(0,shortWindowAction->height())).y()
              <= toolsPage->mapToScene(QPointF(0,toolsPage->height())).y());
        if (argc>2) CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[2])).filePath("tools-wide-short.png")));
        window->setWidth(1024); window->setHeight(640);
        auto *toolsScroll=toolsPage->findChild<QObject *>("qcWorkspaceScroll"); CHECK(toolsScroll);
        auto *toolsContent=toolsScroll ? qvariant_cast<QQuickItem *>(toolsScroll->property("contentItem")) : nullptr; CHECK(toolsContent);
        if (toolsContent) for (const auto &language : {"en", "zh"}) for (const auto &theme : {"light", "dark"}) {
            controller.language()->setMode(language); controller.theme()->setMode(theme);
            CHECK(Await([&]{return toolSelector->isVisible() && !toolNavigation->isVisible();}));
            for (const auto &context : toolContexts) {
                toolSelector->setProperty("currentIndex", toolContexts.indexOf(context));
                CHECK(QMetaObject::invokeMethod(toolSelector, "activated", Q_ARG(int, toolContexts.indexOf(context))));
                QCoreApplication::processEvents();
                CHECK(toolsPage->property("context").toString() == context);
                CHECK(toolsContent->property("contentY").toReal() == 0);
                const QStringList methods = context == "reset" ? QStringList{"sahara", "firehose"} : QStringList{"sahara"};
                for (const auto &method : methods) {
                    if (context == "reset") qc->setContextValue("reset", "resetMethod", method);
                    auto *panel=FindVisualItem(toolsPage, "qcToolPanel-"+context);
                    auto *button=FindVisualItem(toolsPage, "qcToolStart-"+context);
                    auto *advanced=panel->findChild<QObject *>("qcAdvanced");
                    advanced->setProperty("expanded",true);
                    QElapsedTimer layout; layout.start(); Await([&]{return layout.elapsed()>30;});
                    const QPointF position=button->mapToScene(QPointF());
                    CHECK(button->isVisible() && panel->isVisible());
                    CHECK(position.x()>=toolsPage->mapToScene(QPointF()).x());
                    CHECK(position.x()+button->width()<=toolsPage->mapToScene(QPointF(toolsPage->width(),0)).x());
                    CHECK(position.y()>=toolsContent->mapToScene(QPointF(0,toolsContent->height())).y());
                    CHECK(position.y()+button->height()<=toolsPage->mapToScene(QPointF(0,toolsPage->height())).y());
                    const qreal maximum=qMax(qreal(0),toolsContent->property("contentHeight").toReal()-toolsContent->height());
                    toolsContent->setProperty("contentY",maximum);
                    QCoreApplication::processEvents();
                    CHECK(button->mapToScene(QPointF()) == position);
                    toolsContent->setProperty("contentY",0);
                    QCoreApplication::processEvents();
                    if (context == "reset") {
                        auto *choice=panel->findChild<QQuickItem *>("qcResetMethod"); CHECK(choice);
                        CHECK(choice && choice->property("currentIndex").toInt() == (method == "firehose" ? 1 : 0));
                        CHECK(choice && choice->mapToScene(QPointF()).x() >= toolsPage->mapToScene(QPointF()).x());
                        CHECK(choice && choice->mapToScene(QPointF(choice->width(),0)).x() <= toolsPage->mapToScene(QPointF(toolsPage->width(),0)).x());
                        CHECK(panel->property("showProgrammer").toBool() == (method == "firehose"));
                        CHECK(advanced->property("storage").toBool() == (method == "firehose"));
                    }
                    if(button->isEnabled()) { button->forceActiveFocus(Qt::TabFocusReason); CHECK(button->hasActiveFocus()); }
                    if(argc>2) {
                        QDir output(QString::fromLocal8Bit(argv[2]));
                        CHECK(window->grabWindow().save(output.filePath(QString("tools-%1-%2-%3.png").arg(language,theme,context == "reset" ? context+"-"+method : context))));
                    }
                    advanced->setProperty("expanded",false);
                }
            }
            qc->setContextValue("reset", "resetMethod", "sahara");
            toolsContent->setProperty("contentY",0);
        }
        selectTool(3);
        auto *kickstartPanel=FindVisualItem(toolsPage, "qcToolPanel-kickstart");
        auto *kickstartAdvanced=kickstartPanel->findChild<QObject *>("qcAdvanced");
        kickstartAdvanced->setProperty("expanded",true);
        CHECK(Await([&]{return toolsContent->property("contentHeight").toReal()>toolsContent->height();}));
        toolsContent->setProperty("contentY",toolsContent->property("contentHeight").toReal()-toolsContent->height());
        CHECK(toolsContent->property("contentY").toReal()>0);
        selectTool(4);
        CHECK(toolsContent->property("contentY").toReal()==0);
        window->setProperty("currentPage",1);
        window->setProperty("currentPage",2);
        controller.setVendor(0); controller.setVendor(2);
        window->setProperty("currentPage",2);
        CHECK(toolsPage->property("selectedTool").toInt() == 4);
        CHECK(qc->settingsFor("zip").value("output").toString()==temporary.filePath("zip-output"));
        selectTool(0);
        window->setProperty("currentPage",1);
        window->setWidth(1024); window->setHeight(640);
        auto *refresh=window->findChild<QQuickItem *>("statusRefresh"); CHECK(refresh);
        CHECK(Await([&]{return refresh && refresh->mapToScene(QPointF(refresh->width(),0)).x() <= window->width();}));
        {
            const auto originalDevices = controller.devices();
            QVariantList interfaces;
            for (int mode : {QTQ_DIAG, QTQ_ADB, QTQ_UNKNOWN, QTQ_EDL, QTQ_DUMP}) {
                interfaces.append(QVariantMap{{"vendor", 2}, {"index", mode + 1},
                    {"qualcommMode", mode}, {"mode", QStringLiteral("details / USB IF %1").arg(mode)},
                    {"key", QStringLiteral("interface-%1").arg(mode)}});
            }
            CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                Q_ARG(QVariantList, interfaces)));
            auto *summary = window->findChild<QQuickItem *>("statusSummary"); CHECK(summary);
            auto *badge = window->findChild<QQuickItem *>("deviceStatusBadge"); CHECK(badge);
            auto *tooltip = window->findChild<QObject *>("statusSummaryTooltip"); CHECK(tooltip);
            auto *progress = window->findChild<QQuickItem *>("statusProgress"); CHECK(progress);
            auto *cancel = window->findChild<QQuickItem *>("statusCancel"); CHECK(cancel);
            auto *combo = window->findChild<QQuickItem *>("deviceCombo"); CHECK(combo);
            auto *vendorSelector = window->findChild<QObject *>("vendorSelector"); CHECK(vendorSelector);
            CHECK(QMetaObject::invokeMethod(&controller, "onAdbAvailabilityChanged", Qt::DirectConnection,
                Q_ARG(bool, true), Q_ARG(QString, QString())));
            CHECK(QMetaObject::invokeMethod(&controller, "onAdbDevicesChanged", Qt::DirectConnection,
                Q_ARG(QStringList, QStringList{QStringLiteral("board")})));
            if (summary && tooltip && progress && cancel && combo && refresh) {
                for (const QString &language : {QStringLiteral("en"), QStringLiteral("zh")}) {
                    controller.language()->setMode(language);
                    const QString qualcommStatus = controller.deviceStatusText();
                    CHECK(qualcommStatus.contains(QStringLiteral("DIAG *1")));
                    CHECK(!qualcommStatus.contains(QStringLiteral("DIAG candidate")));
                    CHECK(!qualcommStatus.contains(QStringLiteral("DIAG 候选")));
                    if (vendorSelector) {
                        for (int vendor : {0, 1, 2, 0, 1, 2}) {
                            CHECK(QMetaObject::invokeMethod(vendorSelector, "activated", Q_ARG(int, vendor)));
                            const QString expected = vendor == 2 ? qualcommStatus
                                : language == QStringLiteral("en") ? QStringLiteral("ADB device found")
                                    : QStringLiteral("发现 ADB 设备");
                            CHECK(controller.deviceStatusText() == expected);
                            CHECK(Await([&] {
                                return summary->property("text").toString() == expected
                                    && tooltip->property("text").toString() == expected
                                    && vendorSelector->property("currentIndex").toInt() == vendor
                                    && combo->property("count").toInt() == (vendor == 2 ? interfaces.size() : 0);
                            }));
                        }
                    }
                    for (int width : {1024, 1300}) {
                        window->setWidth(width);
                        CHECK(Await([&] {
                            return summary->property("text").toString() == controller.deviceStatusText()
                                && summary->property("truncated").toBool()
                                && summary->width() > 0
                                && summary->width() <= window->width() * 0.25 + 1
                                && progress->width() >= (width >= 1200 ? 220 : 140)
                                && refresh->mapToScene(QPointF(refresh->width(), 0)).x() <= window->width();
                        }));
                        const QList<QQuickItem *> controls{summary, progress, cancel, combo, refresh};
                        for (int i = 1; i < controls.size(); ++i) {
                            CHECK(controls[i-1]->mapToScene(QPointF(controls[i-1]->width(), 0)).x()
                                <= controls[i]->mapToScene(QPointF(0, 0)).x());
                        }
                        MovePointer(window, summary->mapToScene(QPointF(summary->width()/2, summary->height()/2)));
                        CHECK(Await([&]{return tooltip->property("visible").toBool();}));
                        CHECK(tooltip->property("text").toString() == controller.deviceStatusText());
                        MovePointer(window, QPointF(2, 2));
                        CHECK(Await([&]{return !tooltip->property("visible").toBool();}));
                    }
                }
                controller.language()->setMode("en");
                CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                    Q_ARG(QVariantList, QVariantList{})));
                CHECK(Await([&]{return summary->property("text").toString() == QStringLiteral("No device found")
                    && !summary->property("truncated").toBool();}));
                MovePointer(window, summary->mapToScene(QPointF(summary->width()/2, summary->height()/2)));
                CHECK(Await([&]{return !tooltip->property("visible").toBool();}));
                MovePointer(window, QPointF(2, 2));
            }
            if (badge && summary && progress && refresh) {
                for (const QString &language : {QStringLiteral("en"), QStringLiteral("zh")}) {
                    controller.language()->setMode(language);
                    for (const QString &theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
                        controller.theme()->setMode(theme);
                        controller.setVendor(2);
                        CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                            Q_ARG(QVariantList, interfaces)));
                        CHECK(QMetaObject::invokeMethod(&controller, "onAdbDevicesChanged", Qt::DirectConnection,
                            Q_ARG(QStringList, QStringList{QStringLiteral("board")})));
                        const QColor deviceColor = summary->property("color").value<QColor>();
                        const QColor deviceBackground = badge->property("color").value<QColor>();
                        CHECK(deviceColor.green() > deviceColor.red() && deviceColor.green() > deviceColor.blue());

                        auto inspectStatus = [&](const QString &state) {
                            for (int width : {1024, 1300}) {
                                window->setWidth(width);
                                CHECK(Await([&] {
                                    return window->width() == width
                                        && badge->width() >= 180 && badge->width() <= width * 0.25 + 1
                                        && progress->width() >= (width >= 1200 ? 220 : 140)
                                        && badge->mapToScene(QPointF(badge->width(), 0)).x()
                                            <= progress->mapToScene(QPointF(0, 0)).x()
                                        && refresh->mapToScene(QPointF(refresh->width(), 0)).x() <= width;
                                }));
                                CHECK(summary->property("text").toString() == controller.deviceStatusText());
                                if (argc > 2) {
                                    QDir output(QString::fromLocal8Bit(argv[2])); CHECK(QDir().mkpath(output.path()));
                                    QElapsedTimer frame; frame.start(); Await([&]{return frame.elapsed() > 100;});
                                    CHECK(window->grabWindow().save(output.filePath(
                                        QString("status-%1-%2-%3-%4.png").arg(language, theme, state).arg(width))));
                                }
                            }
                        };
                        inspectStatus("device");
                        controller.setBatchTargets({QStringLiteral("interface-%1").arg(QTQ_DIAG),
                                                    QStringLiteral("interface-%1").arg(QTQ_EDL)}, true);
                        CHECK(controller.batchMode());
                        CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                            Q_ARG(QVariantList, QVariantList{})));
                        CHECK(controller.batchSelected() == 2 && !controller.hasDevice());
                        const QColor noDeviceBackground = badge->property("color").value<QColor>();
                        CHECK(noDeviceBackground != deviceBackground);
                        inspectStatus("batch-disconnected");
                        controller.clearBatch();
                        inspectStatus("none");

                        controller.setVendor(0);
                        CHECK(controller.adbReady() && !controller.hasDevice());
                        const QColor adbColor = summary->property("color").value<QColor>();
                        const QColor adbBackground = badge->property("color").value<QColor>();
                        CHECK(adbColor.blue() > adbColor.red() && adbColor.blue() > adbColor.green());
                        CHECK(adbBackground != deviceBackground && adbBackground != noDeviceBackground);
                        inspectStatus("adb");
                        CHECK(QMetaObject::invokeMethod(&controller, "onAdbDevicesChanged", Qt::DirectConnection,
                            Q_ARG(QStringList, QStringList{})));
                        CHECK(badge->property("color").value<QColor>() == noDeviceBackground);
                        inspectStatus("adb-disconnected");
                    }
                }
                controller.language()->setMode("en");
                controller.theme()->setMode("light");
                controller.setVendor(2);
            }
            CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                Q_ARG(QVariantList, originalDevices)));
            window->setWidth(1024);
        }
        {
            // Long device labels must widen the popup instead of being clipped:
            // a vertical ListView reports contentWidth == -1, so the popup has
            // to measure the labels itself.
            auto *deviceCombo=window->findChild<QQuickItem *>("deviceCombo"); CHECK(deviceCombo);
            if(deviceCombo) {
                const double comboWidth=deviceCombo->width();
                deviceCombo->setProperty("model",QVariantList{QStringLiteral(
                    "#0 05c6:9091 SN:0123456789ABCDEF [EDL] qualcomm:1:/dev/bus/usb/003/045:3-1.4")});
                auto *popup=deviceCombo->property("popup").value<QObject *>(); CHECK(popup);
                if(popup) {
                    CHECK(QMetaObject::invokeMethod(popup,"open"));
                    CHECK(Await([&]{return popup->property("width").toReal()>comboWidth;}));
                    CHECK(QMetaObject::invokeMethod(popup,"close"));
                }
            }
        }
        window->setWidth(1300); window->setHeight(780);
        if(argc>1 && argv[1][0]) {
            // Use real firmware for visual inspection without submitting a device task.
            qc->loadFirmware(QString::fromLocal8Bit(argv[1]));
            qc->addXml(QDir(QString::fromLocal8Bit(argv[1])).filePath("rawprogram_unsparse_without_nonhlos.xml"));
            qc->addXml(QDir(QString::fromLocal8Bit(argv[1])).filePath("patch0.xml"));
        }
        if(argc>2) {
            CHECK(Await([&]{return window->isVisible() && !window->grabWindow().isNull();}));
            QDir output(QString::fromLocal8Bit(argv[2])); CHECK(QDir().mkpath(output.path()));
            QElapsedTimer frames; frames.start(); Await([&]{return frames.elapsed()>350;});
            CHECK(window->grabWindow().save(output.filePath("qualcomm-flash.png")));
            auto *edlDialog = window->findChild<QObject *>("qcEdlDialog");
            CHECK(edlDialog);
            if(edlDialog) {
                QMetaObject::invokeMethod(edlDialog,"open");
                frames.restart(); Await([&]{return frames.elapsed()>150;});
                CHECK(window->grabWindow().save(output.filePath("qualcomm-edl-dialog.png")));
                QMetaObject::invokeMethod(edlDialog,"close");
            }
            QMetaObject::invokeMethod(loadXml,"clicked");
            frames.restart(); Await([&]{return frames.elapsed()>150;});
            CHECK(window->grabWindow().save(output.filePath("qualcomm-xml-dialog.png")));
            QMetaObject::invokeMethod(programPicker,"close");
            controller.theme()->setMode("dark"); frames.restart(); Await([&]{return frames.elapsed()>350;});
            CHECK(window->grabWindow().save(output.filePath("qualcomm-flash-dark.png")));
            window->setWidth(1024); window->setHeight(640); frames.restart(); Await([&]{return frames.elapsed()>350;});
            CHECK(window->grabWindow().save(output.filePath("qualcomm-small.png")));
            for (const auto &language : {"en", "zh"}) {
                controller.language()->setMode(language);
                for (const auto &theme : {"light", "dark"}) {
                    controller.theme()->setMode(theme);
                    for (int page=0;page<3;++page) {
                        window->setProperty("currentPage",page);
                        auto *workspace=window->findChild<QObject *>(page==0 ? "QualcommPartitionsPage" : page==1 ? "QualcommFlashPage" : "QualcommToolsPage");
                        auto *advanced=workspace ? workspace->findChild<QObject *>("qcAdvanced") : nullptr;
                        if(advanced && page!=2) advanced->setProperty("expanded",true);
                        if(page==2) {
                            auto *scroll=workspace->findChild<QObject *>("qcWorkspaceScroll"); CHECK(scroll);
                            auto *content=scroll ? qvariant_cast<QObject *>(scroll->property("contentItem")) : nullptr; CHECK(content);
                            if(content) content->setProperty("contentY",0);
                        }
                        frames.restart(); Await([&]{return frames.elapsed()>150;});
                        CHECK(window->grabWindow().save(output.filePath(QString("qualcomm-%1-%2-%3.png").arg(language,theme).arg(page))));
                    }
                }
            }
        }
        for (const auto &theme : {"light", "dark"}) {
            controller.theme()->setMode(theme);
            TestFocusBorders(engine);
        }
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
