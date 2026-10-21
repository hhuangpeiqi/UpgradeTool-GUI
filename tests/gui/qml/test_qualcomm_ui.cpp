#include "QualcommUiTests.h"

int main(int argc,char **argv) {
    int failures = 0;
    auto Write = [&](const QString &path, const QByteArray &data) {
        QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); CHECK(file.write(data) == data.size());
    };
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
        gui::AppController controller(nullptr, hooks, 5000, nullptr, gui::DriverHooks{}, false);
        // All devices are simulated. Vendor changes must not open host udev
        // permission dialogs over the controls exercised by these tests.
        QObject::connect(&controller, &gui::AppController::permissionChanged, &controller, [&] {
            if (controller.permissionBlocked()) controller.ignorePermission();
        });
        // Keep periodic and vendor-triggered hardware scans from replacing the
        // device snapshots injected by these UI tests.
        CHECK(QObject::disconnect(&controller, SIGNAL(requestScan()), nullptr, nullptr));
        CHECK(AwaitUi([&]{return !controller.tasks()->busy();})); controller.setVendor(2); controller.ignorePermission();
        auto *qc=controller.qualcomm();
        controller.language()->setMode("zh");
        CHECK(QCoreApplication::translate("QualcommBackend","Selected device does not use the requested transport")==QStringLiteral("所选设备与指定传输方式不匹配"));
        qc->addXml(temporary.filePath("missing.xml")); CHECK(AwaitUi([&] { return !qc->inspecting(); })); CHECK(qc->error().startsWith(QStringLiteral("无法打开")));
        controller.language()->setMode("en"); CHECK(qc->error().startsWith("Cannot open"));
        qc->removeXml(0); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
        qc->loadFirmware(fixture.path()); CHECK(qc->xmlFiles().isEmpty());
        CHECK(qc->settings().value("programmer").toString()==fixture.filePath("prog_firehose.mbn"));
        qc->addXml(fixture.filePath("rawprogram_CN.xml")); CHECK(AwaitUi([&] { return !qc->inspecting(); })); qc->addXml(fixture.filePath("patch0.xml")); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
        CHECK(AwaitUi([&] { return !qc->inspecting(); })); CHECK(qc->xmlFiles().size()==2 && qc->preview().size()==1); CHECK(qc->error().isEmpty());
        qc->moveXml(1,-1); CHECK(AwaitUi([&] { return !qc->inspecting(); })); CHECK(qc->xmlFiles().first().endsWith("patch0.xml")); qc->moveXml(0,1); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
        qc->addXml(fixture.filePath("rawprogram_CN.xml")); CHECK(AwaitUi([&] { return !qc->inspecting(); })); CHECK(qc->xmlFiles().size()==2);
        QQmlApplicationEngine engine;
        QObject::connect(controller.language(), &gui::LanguageManager::languageChanged, &engine, &QQmlApplicationEngine::retranslate);
        QObject::connect(&engine,&QQmlEngine::warnings,&application,[&](const QList<QQmlError> &errors) {
            for(const auto &error:errors) { std::fprintf(stderr,"QML: %s\n",error.toString().toUtf8().constData()); ++failures; }
        });
        engine.rootContext()->setContextProperty("app",&controller); engine.rootContext()->setContextProperty("startTab",1);
        engine.load(QUrl("qrc:/qt/qml/UpgradeTool/Main.qml")); CHECK(!engine.rootObjects().isEmpty());
        if(engine.rootObjects().isEmpty()) return 1;
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); CHECK(window);
        QualcommUiTests tests{failures, argc, argv, temporary, application, fixture, controller,
            endpointMask, diagSwitches, adbSwitches, requestSent, showEdl, qc, window};
        if (!tests.TestDevices() || !tests.TestFlash() || !tests.TestTools() || !tests.TestLayout()) return 1;
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
