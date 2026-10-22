#include "QualcommUiTests.h"

bool QualcommUiTests::TestDevices() {
    download=window->findChild<QQuickItem *>("qcDownload");
    CHECK(download); if(!download) return false;
    // Settings are complete, but no EDL device is enumerated, so the
    // device operations stay disabled (the backend would refuse them).
    CHECK(!download->isEnabled());
    auto *rawStart = window->findChild<QQuickItem *>("qcRawStart");
    CHECK(rawStart && !rawStart->isEnabled());
    auto *toolStart = FindVisualItem(window->contentItem(), "qcToolStart-chipinfo");
    CHECK(toolStart && !toolStart->isEnabled());
    {
        auto update=[&](const QVariantList &devices) {
            CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(devices))));
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
    loadXml = window->findChild<QObject *>("qcLoadXml");
    programPicker = window->findChild<QObject *>("qcProgramPicker");
    patchPicker = window->findChild<QObject *>("qcPatchPicker");
    CHECK(loadXml && programPicker && patchPicker);
    auto *qcPage = window->findChild<QObject *>("QualcommFlashPage"); CHECK(qcPage);
    auto *switchEdl = qcPage->findChild<QQuickItem *>("qcSwitchEdl"); CHECK(switchEdl && !switchEdl->isEnabled());
    CHECK(!window->findChild<QObject *>("qcSwitchConfig"));
    auto *switchMethod = qcPage->findChild<QQuickItem *>("qcSwitchMethod"); CHECK(switchMethod);
    edlDialog = window->findChild<QObject *>("qcEdlDialog"); CHECK(edlDialog);
    auto *edlDevice = edlDialog->findChild<QObject *>("qcEdlDevice"); CHECK(edlDevice);
    auto *edlPort = edlDialog->findChild<QObject *>("qcEdlPort"); CHECK(edlPort);
    auto *edlStart = edlDialog->findChild<QQuickItem *>("qcEdlStart"); CHECK(edlStart);
    auto refreshSwitch = [&] {
        CHECK(AwaitUi([&]{ return !controller.edl()->discovering(); }));
        controller.refreshEdl();
        CHECK(AwaitUi([&]{ return !controller.edl()->discovering(); }));
        QCoreApplication::processEvents();
    };
    refreshSwitch();
    CHECK(!controller.edl()->ready());
    QString switchError;
    auto switchErrorConnection = QObject::connect(&controller, &gui::AppController::toastRequested, &application,
        [&](const QString &text, bool error) { if (error) switchError = text; });
    controller.requestEdlSwitch();
    CHECK(!controller.tasks()->busy() && !switchError.isEmpty());
    QObject::disconnect(switchErrorConnection);
    auto *switchTooltip = qcPage->findChild<QObject *>("qcSwitchTooltip"); CHECK(switchTooltip);
    auto *startTooltip = edlDialog->findChild<QObject *>("qcEdlStartTooltip"); CHECK(startTooltip);
    auto hoverButton = [&](QQuickItem *button, QObject *tooltip, const QString &expected) {
        QElapsedTimer layout; layout.start(); AwaitUi([&]{return layout.elapsed() > 60;});
        CHECK(!button->isEnabled());
        MovePointer(window, button->mapToScene(QPointF(button->width()/2, button->height()/2)));
        CHECK(AwaitUi([&]{ return tooltip->property("visible").toBool(); }));
        CHECK(tooltip->property("text").toString().contains(expected));
        MovePointer(window, QPointF(2, 2));
        CHECK(AwaitUi([&]{ return !tooltip->property("visible").toBool(); }));
    };
    hoverButton(switchEdl, switchTooltip, QStringLiteral("No diagnostic interface"));
    controller.edl()->setMethod(gui::EdlRunner::Adb);
    hoverButton(switchEdl, switchTooltip, QStringLiteral("No authorized ADB device"));
    controller.edl()->setMethod(gui::EdlRunner::Diag);
    controller.language()->setMode("zh");
    hoverButton(switchEdl, switchTooltip, QStringLiteral("未检测到诊断接口"));
    controller.language()->setMode("en");
    // A working ADB sibling must not enable DIAG when its driver is missing.
    endpointMask = 34; refreshSwitch();
    CHECK(!switchEdl->isEnabled());
    hoverButton(switchEdl, switchTooltip, QStringLiteral("driver is missing"));
    controller.edl()->setMethod(gui::EdlRunner::Adb);
    CHECK(switchEdl->isEnabled());
    controller.edl()->setMethod(gui::EdlRunner::Diag);
    CHECK(!switchEdl->isEnabled());
    CHECK(diagSwitches == 0 && adbSwitches == 0);
    endpointMask = 0; refreshSwitch();
    // First entry discovers candidates without ever opening a settings dialog.
    endpointMask = 1;
    CHECK(AwaitUi([&]{ return switchEdl->isEnabled(); })); // automatic polling
    MovePointer(window, switchEdl->mapToScene(QPointF(switchEdl->width()/2, switchEdl->height()/2)));
    QElapsedTimer hover; hover.start(); AwaitUi([&]{return hover.elapsed() > 60;});
    CHECK(!switchTooltip->property("visible").toBool());
    MovePointer(window, QPointF(2, 2));
    QMetaObject::invokeMethod(switchEdl, "clicked");
    CHECK(controller.tasks()->busy() && !switchEdl->isEnabled());
    CHECK(controller.switching() && controller.tasks()->progressValue() == -1);
    CHECK(!edlDialog->property("visible").toBool());
    CHECK(AwaitUi([&]{ return !controller.tasks()->busy(); }));
    CHECK(diagSwitches == 1 && adbSwitches == 0);
    CHECK(!controller.switching() && controller.tasks()->progressValue() == 100);
    refreshSwitch();
    // ADB availability is independent of the diagnostic candidates.
    endpointMask = 2; refreshSwitch();
    CHECK(!switchEdl->isEnabled());
    controller.edl()->setMethod(gui::EdlRunner::Adb);
    CHECK(switchEdl->isEnabled());
    CHECK(switchMethod->property("selectedIndex").toInt() == 1);
    QMetaObject::invokeMethod(switchEdl, "clicked");
    CHECK(AwaitUi([&]{ return !controller.tasks()->busy(); }));
    CHECK(diagSwitches == 1 && adbSwitches == 1);
    controller.edl()->setMethod(gui::EdlRunner::Diag);
    // Ambiguous ports require explicit selection, which unplugging invalidates.
    endpointMask = 5; refreshSwitch();
    QMetaObject::invokeMethod(switchEdl, "clicked");
    CHECK(AwaitUi([&]{ return edlDialog->property("visible").toBool(); }));
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
    CHECK(AwaitUi([&]{ return edlDialog->property("visible").toBool(); }));
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
    CHECK(AwaitUi([&]{ return edlDialog->property("visible").toBool(); }));
    CHECK(!edlStart->isEnabled());
    edlPort->setProperty("currentIndex", 1);
    QMetaObject::invokeMethod(edlPort, "activated", Q_ARG(int, 1));
    CHECK(edlStart->isEnabled());
    QMetaObject::invokeMethod(edlStart, "clicked");
    CHECK(AwaitUi([&]{ return !controller.tasks()->busy(); }));
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
            QElapsedTimer layout; layout.start(); AwaitUi([&]{return layout.elapsed() > 60;});
            const QPointF before = button->mapToScene(QPointF());
            const QPointF primaryBefore = primary->mapToScene(QPointF());
            controller.tasks()->setSpeed("Old speed");
            requestSent = false;
            QMetaObject::invokeMethod(button, "clicked");
            CHECK(controller.tasks()->busy());
            CHECK(AwaitUi([&]{ return requestSent.load(); }));
            layout.restart(); AwaitUi([&]{return layout.elapsed() > 60;});
            CHECK(qAbs(before.x() - button->mapToScene(QPointF()).x()) < 1);
            CHECK(qAbs(before.y() - button->mapToScene(QPointF()).y()) < 1);
            CHECK(qAbs(primaryBefore.x() - primary->mapToScene(QPointF()).x()) < 1);
            if (page < 2) CHECK(button->mapToScene(QPointF(button->width(), 0)).x() <= primary->mapToScene(QPointF()).x());
            CHECK(primary->mapToScene(QPointF(primary->width(), 0)).x() <= workspace->mapToScene(QPointF(workspace->width(), 0)).x());
            CHECK(!primary->isEnabled() && !button->isEnabled());
            CHECK(button->property("busy").toBool());
            CHECK(controller.tasks()->progressValue() == -1 && controller.tasks()->progressSpeed().isEmpty());
            CHECK(window->findChild<QObject *>("statusProgress")->property("indeterminate").toBool());
            CHECK(window->findChild<QObject *>("statusPercentage")->property("text").toString().isEmpty());
            CHECK(window->findChild<QObject *>("statusProgressMessage")->property("text").toString() == controller.tasks()->progressText());
            CHECK(window->findChild<QObject *>("statusProgressTooltip")->property("text").toString() == controller.tasks()->progressText());
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
            controller.cancel(); CHECK(AwaitUi([&]{ return !controller.tasks()->busy(); }));
            CHECK(!button->property("busy").toBool());
            CHECK(!window->findChild<QObject *>("statusProgress")->property("indeterminate").toBool());
            CHECK(controller.tasks()->progressValue() == 0);
            refreshSwitch();
        }
    }
    controller.language()->setMode("en");
    controller.theme()->setMode("light");
    window->setProperty("currentPage", 1);
    window->setWidth(1300); window->setHeight(780);
    showEdl = true; endpointMask = 0; refreshSwitch();
    return true;
}
