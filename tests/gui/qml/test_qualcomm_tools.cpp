#include "QualcommUiTests.h"
#include <QtQuickTest/quicktest.h>

namespace {
#if QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
bool HasPendingPolish(QQuickItem *item) {
    if (QQuickTest::qIsPolishScheduled(item)) return true;
    for (auto *child : item->childItems())
        if (HasPendingPolish(child)) return true;
    return false;
}
#endif

bool WaitForWindowPolish(QQuickWindow *window) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    return QQuickTest::qWaitForPolish(window, 5000);
#else
    // Qt 6.2 only exposes the per-item query. Recheck the entire visual tree
    // because polishing one layout can schedule another item.
    QCoreApplication::processEvents();
    return QTest::qWaitFor([&] { return !HasPendingPolish(window->contentItem()); }, 5000);
#endif
}
}

bool QualcommUiTests::TestTools() {
    for(int page=0;page<3;++page) { window->setProperty("currentPage",page); QCoreApplication::processEvents(); }
    CHECK(WaitForWindowPolish(window));
    toolsPage=window->findChild<QQuickItem *>("QualcommToolsPage"); CHECK(toolsPage);
    CHECK(!window->findChild<QObject *>("qcTool"));
    toolNavigation = toolsPage->findChild<QQuickItem *>("qcToolNavigation");
    toolSelector = toolsPage->findChild<QQuickItem *>("qcToolSelector");
    CHECK(toolNavigation && toolNavigation->isVisible());
    CHECK(toolSelector && !toolSelector->isVisible());
    CHECK(toolsPage->property("selectedTool").toInt() == 0);
    if (argc>2) {
        // Let earlier validation toasts close before capturing the tool forms.
        QElapsedTimer toast; toast.start(); AwaitUi([&]{return toast.elapsed()>2800;});
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
        // Tool changes can move navigation delegates; wait before reading click coordinates.
        CHECK(WaitForWindowPolish(window));
        auto *navigation = FindVisualItem(toolsPage, "qcToolSelect-" + toolContexts[i]);
        CHECK(navigation && navigation->isVisible());
        if (navigation) QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            navigation->mapToScene(QPointF(navigation->width()/2, navigation->height()/2)).toPoint());
        CHECK(AwaitUi([&] { return toolsPage->property("selectedTool").toInt() == i; }));
        CHECK(WaitForWindowPolish(window));
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
            QElapsedTimer layout; layout.start(); AwaitUi([&]{return layout.elapsed()>40;});
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
        controller.language()->setMode("zh"); CHECK(AwaitUi([&]{return ufsStart && ufsStart->property("text").toString()==QStringLiteral("模拟 UFS 配置");}));
        CHECK(AwaitUi([&]{return vendorSelector && vendorSelector->property("currentIndex").toInt()==2;}));
        CHECK(buildType && buildType->property("currentIndex").toInt()==1);
        CHECK(inputType && inputType->property("currentIndex").toInt()==1);
        CHECK(AwaitUi([&]{return rawAction && rawAction->property("currentIndex").toInt()==2;}));
        CHECK(QCoreApplication::translate("QualcommProgrammer","Programmer input")==QStringLiteral("引导程序来源"));
        CHECK(QCoreApplication::translate("gui::AppController","Simulation")==QStringLiteral("模拟执行"));
        CHECK(QCoreApplication::translate("gui::EdlRunner","Checking the selected device…")==QStringLiteral("正在检查所选设备…"));
        CHECK(QCoreApplication::translate("EdlSwitchDialog","Diagnostic interface")==QStringLiteral("诊断接口"));
        controller.language()->setMode("en"); CHECK(AwaitUi([&]{return ufsStart && ufsStart->property("text").toString()==QStringLiteral("Simulate UFS provisioning");}));
        CHECK(AwaitUi([&]{return vendorSelector && vendorSelector->property("currentIndex").toInt()==2;}));
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
        qc->addXml(fixture.filePath("rawprogram_CN.xml"),"digests"); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
        qc->addXml(fixture.filePath("ufs.xml"),"provision"); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
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
            CHECK(requests==i+1 && submitted.operation()==toolOperations[i]);
            CHECK(!controller.tasks()->busy());
        }
        auto *resetStart = FindVisualItem(toolsPage, "qcToolStart-reset");
        qc->setContextValue("reset", "resetMethod", "firehose");
        qc->setContextValue("reset", "programmer", loader);
        CHECK(resetStart && QMetaObject::invokeMethod(resetStart,"clicked"));
        CHECK(requests == toolContexts.size()+1 && submitted.qualcomm().programmer == loader.toStdString());
        qc->setContextValue("reset", "resetMethod", "sahara");
        CHECK(resetStart && QMetaObject::invokeMethod(resetStart,"clicked"));
        CHECK(requests == toolContexts.size()+2 && submitted.qualcomm().programmer.empty() && submitted.qualcomm().images.empty());
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
    return true;
}
