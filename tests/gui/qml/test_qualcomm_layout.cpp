#include "QualcommUiTests.h"

bool QualcommUiTests::TestLayout() {
    // Every operation must remain reachable in a narrow workspace, with no
    // horizontal overflow, including expanded advanced options and keyboard focus.
    window->setProperty("currentPage",2);
    window->setWidth(1300); window->setHeight(640);
    CHECK(AwaitUi([&]{return toolSelector->isVisible() && !toolNavigation->isVisible();}));
    auto *shortWindowAction=FindVisualItem(toolsPage, "qcToolStart-chipinfo");
    CHECK(shortWindowAction->mapToScene(QPointF(0,shortWindowAction->height())).y()
          <= toolsPage->mapToScene(QPointF(0,toolsPage->height())).y());
    if (argc>2) CHECK(window->grabWindow().save(QDir(QString::fromLocal8Bit(argv[2])).filePath("tools-wide-short.png")));
    window->setWidth(1024); window->setHeight(640);
    auto *toolsScroll=toolsPage->findChild<QObject *>("qcWorkspaceScroll"); CHECK(toolsScroll);
    auto *toolsContent=toolsScroll ? qvariant_cast<QQuickItem *>(toolsScroll->property("contentItem")) : nullptr; CHECK(toolsContent);
    if (toolsContent) for (const auto &language : {"en", "zh"}) for (const auto &theme : {"light", "dark"}) {
        controller.language()->setMode(language); controller.theme()->setMode(theme);
        CHECK(AwaitUi([&]{return toolSelector->isVisible() && !toolNavigation->isVisible();}));
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
                QElapsedTimer layout; layout.start(); AwaitUi([&]{return layout.elapsed()>30;});
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
    CHECK(AwaitUi([&]{return toolsContent->property("contentHeight").toReal()>toolsContent->height();}));
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
    CHECK(AwaitUi([&]{return refresh && refresh->mapToScene(QPointF(refresh->width(),0)).x() <= window->width();}));
    {
        const auto originalDevices = controller.devices();
        QVariantList interfaces;
        for (int mode : {QTQ_DIAG, QTQ_ADB, QTQ_UNKNOWN, QTQ_EDL, QTQ_DUMP}) {
            interfaces.append(QVariantMap{{"vendor", 2}, {"index", mode + 1},
                {"qualcommMode", mode}, {"mode", QStringLiteral("details / USB IF %1").arg(mode)},
                {"key", QStringLiteral("interface-%1").arg(mode)}});
        }
        CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
            Q_ARG(QVariantList, SnapshotDevices(interfaces))));
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
                        CHECK(AwaitUi([&] {
                            return summary->property("text").toString() == expected
                                && tooltip->property("text").toString() == expected
                                && vendorSelector->property("currentIndex").toInt() == vendor
                                && combo->property("count").toInt() == (vendor == 2 ? interfaces.size() : 0);
                        }));
                    }
                }
                for (int width : {1024, 1300}) {
                    window->setWidth(width);
                    CHECK(AwaitUi([&] {
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
                    CHECK(AwaitUi([&]{return tooltip->property("visible").toBool();}));
                    CHECK(tooltip->property("text").toString() == controller.deviceStatusText());
                    MovePointer(window, QPointF(2, 2));
                    CHECK(AwaitUi([&]{return !tooltip->property("visible").toBool();}));
                }
            }
            controller.language()->setMode("en");
            CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                Q_ARG(QVariantList, SnapshotDevices(QVariantList{}))));
            CHECK(AwaitUi([&]{return summary->property("text").toString() == QStringLiteral("No device found")
                && !summary->property("truncated").toBool();}));
            MovePointer(window, summary->mapToScene(QPointF(summary->width()/2, summary->height()/2)));
            CHECK(AwaitUi([&]{return !tooltip->property("visible").toBool();}));
            MovePointer(window, QPointF(2, 2));
        }
        if (badge && summary && progress && refresh) {
            for (const QString &language : {QStringLiteral("en"), QStringLiteral("zh")}) {
                controller.language()->setMode(language);
                for (const QString &theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
                    controller.theme()->setMode(theme);
                    controller.setVendor(2);
                    CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                        Q_ARG(QVariantList, SnapshotDevices(interfaces))));
                    CHECK(QMetaObject::invokeMethod(&controller, "onAdbDevicesChanged", Qt::DirectConnection,
                        Q_ARG(QStringList, QStringList{QStringLiteral("board")})));
                    const QColor deviceColor = summary->property("color").value<QColor>();
                    const QColor deviceBackground = badge->property("color").value<QColor>();
                    CHECK(deviceColor.green() > deviceColor.red() && deviceColor.green() > deviceColor.blue());

                    auto inspectStatus = [&](const QString &state) {
                        for (int width : {1024, 1300}) {
                            window->setWidth(width);
                            CHECK(AwaitUi([&] {
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
                                QElapsedTimer frame; frame.start(); AwaitUi([&]{return frame.elapsed() > 100;});
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
                        Q_ARG(QVariantList, SnapshotDevices(QVariantList{}))));
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
            Q_ARG(QVariantList, SnapshotDevices(originalDevices))));
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
                CHECK(AwaitUi([&]{return popup->property("width").toReal()>comboWidth;}));
                CHECK(QMetaObject::invokeMethod(popup,"close"));
            }
        }
    }
    window->setWidth(1300); window->setHeight(780);
    if(argc>1 && argv[1][0]) {
        // Use real firmware for visual inspection without submitting a device task.
        qc->loadFirmware(QString::fromLocal8Bit(argv[1]));
        qc->addXml(QDir(QString::fromLocal8Bit(argv[1])).filePath("rawprogram_unsparse_without_nonhlos.xml")); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
        qc->addXml(QDir(QString::fromLocal8Bit(argv[1])).filePath("patch0.xml")); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
    }
    if(argc>2) {
        CHECK(AwaitUi([&]{return window->isVisible() && !window->grabWindow().isNull();}));
        QDir output(QString::fromLocal8Bit(argv[2])); CHECK(QDir().mkpath(output.path()));
        QElapsedTimer frames; frames.start(); AwaitUi([&]{return frames.elapsed()>350;});
        CHECK(window->grabWindow().save(output.filePath("qualcomm-flash.png")));
        auto *edlDialog = window->findChild<QObject *>("qcEdlDialog");
        CHECK(edlDialog);
        if(edlDialog) {
            QMetaObject::invokeMethod(edlDialog,"open");
            frames.restart(); AwaitUi([&]{return frames.elapsed()>150;});
            CHECK(window->grabWindow().save(output.filePath("qualcomm-edl-dialog.png")));
            QMetaObject::invokeMethod(edlDialog,"close");
        }
        QMetaObject::invokeMethod(loadXml,"clicked");
        frames.restart(); AwaitUi([&]{return frames.elapsed()>150;});
        CHECK(window->grabWindow().save(output.filePath("qualcomm-xml-dialog.png")));
        QMetaObject::invokeMethod(programPicker,"close");
        controller.theme()->setMode("dark"); frames.restart(); AwaitUi([&]{return frames.elapsed()>350;});
        CHECK(window->grabWindow().save(output.filePath("qualcomm-flash-dark.png")));
        window->setWidth(1024); window->setHeight(640); frames.restart(); AwaitUi([&]{return frames.elapsed()>350;});
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
                    frames.restart(); AwaitUi([&]{return frames.elapsed()>150;});
                    CHECK(window->grabWindow().save(output.filePath(QString("qualcomm-%1-%2-%3.png").arg(language,theme).arg(page))));
                }
            }
        }
    }
    return true;
}
