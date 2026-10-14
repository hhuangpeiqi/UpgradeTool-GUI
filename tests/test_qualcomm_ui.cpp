#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QImage>
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
        gui::AppController controller(nullptr);
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
        auto *toolStart = window->findChild<QQuickItem *>("qcToolStart");
        CHECK(toolStart && !toolStart->isEnabled());
        auto *loadXml = window->findChild<QObject *>("qcLoadXml");
        auto *programPicker = window->findChild<QObject *>("qcProgramPicker");
        auto *patchPicker = window->findChild<QObject *>("qcPatchPicker");
        CHECK(loadXml && programPicker && patchPicker);
        auto *switchEdl = window->findChild<QQuickItem *>("qcSwitchEdl"); CHECK(switchEdl && switchEdl->isEnabled());
        auto *switchConfig = window->findChild<QObject *>("qcSwitchConfig"); CHECK(switchConfig);
        // "Switch method…" opens the dialog, while "Switch" switches straight away
        // with the last configuration confirmed in that dialog. With no device
        // discovered it must report the missing endpoint instead of starting a job.
        if (switchEdl) {
            QMetaObject::invokeMethod(switchEdl, "clicked");
            CHECK(!controller.busy());
            CHECK(!controller.edl()->message().isEmpty());
        }
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
        // The action bar carries a state hint, so the footer reads as the
        // primary action instead of blending into the form.
        auto *actionSummary=flashPage ? flashPage->findChild<QObject *>("qcActionSummary") : nullptr; CHECK(actionSummary);
        CHECK(actionSummary && !actionSummary->property("text").toString().isEmpty());
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
        auto *tool=window->findChild<QObject *>("qcTool"); CHECK(tool);
        auto selectTool=[&](int choice) { if(tool) { tool->setProperty("currentIndex",choice); QMetaObject::invokeMethod(tool,"activated",Q_ARG(int,choice)); } };
        if(tool) for(int choice=0;choice<8;++choice) { selectTool(choice); QCoreApplication::processEvents(); }
        if(tool) {
            selectTool(7); window->setProperty("currentPage",2);
            auto *toolsPage=window->findChild<QObject *>("QualcommToolsPage"); CHECK(toolsPage);
            auto *ufsAdvanced=toolsPage ? toolsPage->findChild<QObject *>("qcAdvanced") : nullptr; CHECK(ufsAdvanced);
            if(ufsAdvanced) ufsAdvanced->setProperty("expanded",true);
            auto *ufsDry=toolsPage ? toolsPage->findChild<QQuickItem *>("qcDryRun") : nullptr; CHECK(ufsDry && ufsDry->isVisible());
            qc->setValue("dryRun",true); CHECK(ufsDry && !ufsDry->property("checked").toBool());
            qc->setContextValue("provision","dryRun",true); CHECK(ufsDry && ufsDry->property("checked").toBool());
            selectTool(3); CHECK(ufsDry && !ufsDry->isVisible());
            selectTool(7); CHECK(ufsDry && ufsDry->property("checked").toBool());
            auto *buildType=flashPage->findChild<QObject *>("qcBuildType"); CHECK(buildType);
            auto *inputType=flashPage->findChild<QObject *>("qcProgrammerInput"); CHECK(inputType);
            auto *rawAction=window->findChild<QObject *>("qcRawAction"); CHECK(rawAction);
            if(rawAction) { rawAction->setProperty("currentIndex",2); QMetaObject::invokeMethod(rawAction,"activated",Q_ARG(int,2)); }
            qc->setValue("packageBuild",true); qc->setValue("useImages",true);
            auto *vendorSelector=window->findChild<QObject *>("vendorSelector"); CHECK(vendorSelector);
            controller.language()->setMode("zh"); CHECK(Await([&]{return tool->property("currentIndex").toInt()==7;}));
            CHECK(vendorSelector && vendorSelector->property("currentIndex").toInt()==2);
            CHECK(buildType && buildType->property("currentIndex").toInt()==1);
            CHECK(inputType && inputType->property("currentIndex").toInt()==1);
            CHECK(rawAction && rawAction->property("currentIndex").toInt()==2);
            CHECK(QCoreApplication::translate("QualcommProgrammer","Programmer input")==QStringLiteral("引导程序来源"));
            CHECK(QCoreApplication::translate("gui::AppController","Simulation")==QStringLiteral("模拟执行"));
            CHECK(QCoreApplication::translate("gui::EdlRunner","Checking the selected device…")==QStringLiteral("正在检查所选设备…"));
            CHECK(QCoreApplication::translate("EdlSwitchDialog","Diagnostic port (DIAG)")==QStringLiteral("诊断口（DIAG）"));
            controller.language()->setMode("en"); CHECK(Await([&]{return tool->property("currentIndex").toInt()==7;}));
            CHECK(vendorSelector && vendorSelector->property("currentIndex").toInt()==2);
            qc->setValue("packageBuild",false); qc->setValue("useImages",false);
            qc->setValue("dryRun",false); qc->setContextValue("provision","dryRun",false);
        }
        if(tool) selectTool(0);
        window->setProperty("currentPage",1);
        window->setWidth(1024); window->setHeight(640);
        auto *refresh=window->findChild<QQuickItem *>("statusRefresh"); CHECK(refresh);
        CHECK(Await([&]{return refresh && refresh->mapToScene(QPointF(refresh->width(),0)).x() <= window->width();}));
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
                        if(advanced) advanced->setProperty("expanded",true);
                        if(page==2 && tool) selectTool(7);
                        frames.restart(); Await([&]{return frames.elapsed()>150;});
                        CHECK(window->grabWindow().save(output.filePath(QString("qualcomm-%1-%2-%3.png").arg(language,theme).arg(page))));
                    }
                }
            }
        }
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
