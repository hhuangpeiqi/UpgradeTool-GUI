#include "QualcommUiTests.h"

bool QualcommUiTests::TestFlash() {
    if (loadXml && programPicker && patchPicker) {
        controller.rememberPath("qualcomm/flash/rawprogram", temporary.path());
        const auto oldFiles = qc->xmlFiles();
        QMetaObject::invokeMethod(loadXml, "clicked");
        CHECK(AwaitUi([&]{return programPicker->property("visible").toBool();}));
        CHECK(programPicker->property("currentFolder").toString() == fixture.path());
        programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("rawprogram_US.xml")});
        QMetaObject::invokeMethod(programPicker, "confirmAccept");
        CHECK(AwaitUi([&]{return patchPicker->property("visible").toBool();}));
        CHECK(patchPicker->property("currentFolder").toString() == fixture.path());
        CHECK(qc->xmlFiles() == oldFiles);
        QMetaObject::invokeMethod(patchPicker, "close");
        CHECK(AwaitUi([&]{return !patchPicker->property("visible").toBool();}));
        CHECK(qc->xmlFiles() == oldFiles);
        QMetaObject::invokeMethod(loadXml, "clicked");
        CHECK(AwaitUi([&]{return programPicker->property("visible").toBool();}));
        programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("rawprogram_US.xml")});
        QMetaObject::invokeMethod(programPicker, "confirmAccept");
        CHECK(AwaitUi([&]{return patchPicker->property("visible").toBool();}));
        patchPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("patch0.xml")});
        QMetaObject::invokeMethod(patchPicker, "confirmAccept");
        CHECK(AwaitUi([&]{return !patchPicker->property("visible").toBool();}));
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
        CHECK(AwaitUi([&]{return programPicker->property("visible").toBool();}));
        QMetaObject::invokeMethod(programPicker, "close");
        CHECK(AwaitUi([&]{return !programPicker->property("visible").toBool();}));
        CHECK(!patchPicker->property("visible").toBool()); CHECK(qc->xmlFiles() == selected);
        QMetaObject::invokeMethod(loadXml, "clicked");
        CHECK(AwaitUi([&]{return programPicker->property("visible").toBool();}));
        programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("missing.xml")});
        QMetaObject::invokeMethod(programPicker, "confirmAccept");
        CHECK(programPicker->property("visible").toBool());
        CHECK(!programPicker->property("selectionError").toString().isEmpty()); CHECK(qc->xmlFiles() == selected);
        programPicker->setProperty("selectedPaths", QVariantList{fixture.filePath("rawprogram_CN.xml")});
        QMetaObject::invokeMethod(programPicker, "confirmAccept");
        CHECK(AwaitUi([&]{return patchPicker->property("visible").toBool();}));
        QMetaObject::invokeMethod(patchPicker, "skipped"); QMetaObject::invokeMethod(patchPicker, "close");
        CHECK(AwaitUi([&]{return !patchPicker->property("visible").toBool();}));
        CHECK(qc->xmlFiles() == QStringList({fixture.filePath("rawprogram_CN.xml")}));
        // Double-clicking a row confirms the selection, mirroring QFIL.
        QMetaObject::invokeMethod(loadXml, "clicked");
        CHECK(AwaitUi([&]{return programPicker->property("visible").toBool();}));
        QMetaObject::invokeMethod(programPicker, "activateEntry",
            Q_ARG(QVariant, QStringLiteral("rawprogram_CN.xml")),
            Q_ARG(QVariant, fixture.filePath("rawprogram_CN.xml")), Q_ARG(QVariant, false));
        CHECK(AwaitUi([&]{return patchPicker->property("visible").toBool();}));
        QMetaObject::invokeMethod(patchPicker, "close");
        CHECK(AwaitUi([&]{return !patchPicker->property("visible").toBool();}));
        CHECK(qc->xmlFiles() == QStringList({fixture.filePath("rawprogram_CN.xml")}));
        CHECK(qc->replaceFlashXml({fixture.filePath("rawprogram_CN.xml")},{fixture.filePath("patch0.xml")})); CHECK(AwaitUi([&] { return !qc->inspecting(); }));
    }
    core::TaskJob submitted;
    auto submissionConnection = QObject::connect(&controller,&gui::AppController::requestExecute,&application,[&](const core::TaskJob &job){submitted=job;});
    qc->setValue("dryRun",true);
    qc->run(static_cast<int>(core::Operation::QualcommFlash)); CHECK(controller.tasks()->busy());
    CHECK(!download->isEnabled()); controller.setVendor(0); CHECK(controller.vendor()==2);
    auto prior=qc->xmlFiles(); qc->removeXml(0); CHECK(AwaitUi([&] { return !qc->inspecting(); })); CHECK(qc->xmlFiles()==prior);
    CHECK(AwaitUi([&]{return !controller.tasks()->busy();})); CHECK(submitted.targets.empty()); CHECK(controller.tasks()->progressValue()==100);
    CHECK(controller.tasks()->progressText().startsWith("Simulation:"));
    CHECK(!submitted.steps.empty()); if(submitted.steps.empty()) { QObject::disconnect(submissionConnection); return false; } CHECK(submitted.steps.front().qualcomm().xml.size()==2);
    flashPage=window->findChild<QObject *>("QualcommFlashPage"); CHECK(flashPage);
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
        // refuses it and records the reason in the log.
        QString loggedText; bool loggedError=false;
        auto connection=QObject::connect(controller.log(),&QAbstractItemModel::rowsInserted,&application,
            [&](const QModelIndex &, int, int last) {
            const auto row = controller.log()->index(last, 0);
            const QString text = controller.log()->data(row, gui::LogModel::TextRole).toString();
            const bool error = controller.log()->data(row, gui::LogModel::ErrorRole).toBool();
            loggedText = text; loggedError = error;
        });
        qc->setValue("useImages",true); CHECK(!download->isEnabled());
        QMetaObject::invokeMethod(download,"clicked");
        CHECK(loggedError && !loggedText.isEmpty() && !controller.tasks()->busy());
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
    QObject::disconnect(submissionConnection);
    return true;
}
