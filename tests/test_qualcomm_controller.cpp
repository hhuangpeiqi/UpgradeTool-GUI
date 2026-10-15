#include "gui/backend/QualcommController.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJSEngine>
#include <QFile>
#include <QDir>
#include <cstdio>
class CRKLog;
CRKLog *g_pLogObject = nullptr;
static int failures;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while (0)
static void Write(const QString &path, const QByteArray &data) { QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(data) == data.size()); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList booleanKeys = {"packageBuild", "archiveContents", "useImages", "debug", "dryRun", "allowMissing", "allowFusing", "finalize", "skipReset", "skipblock"};
    QTemporaryDir temp;
    const QString loader = temp.filePath("loader.mbn"), raw = temp.filePath("raw.xml"), ufs = temp.filePath("ufs.xml");
    Write(loader, QByteArray(512,'L'));
    Write(raw, "<data><program filename=\"boot.bin\" label=\"boot\"/></data>");
    Write(ufs, "<data><ufs bNumberLU=\"1\"/></data>");
    gui::QualcommController c;
    QJSEngine js;
    for (const auto &key : booleanKeys) {
        const auto value = c.settings().value(key);
        CHECK(value.metaType().id() == QMetaType::Bool);
        CHECK(js.toScriptValue(value).toBool() == value.toBool());
    }
    CHECK(!c.settingsFor("provision").value("dryRun").toBool());
    CHECK(!c.settingsFor("raw").value("dryRun").toBool());
    CHECK(!c.settings().value("useImages").toBool());
    {
        // Device-required operations mirror core::RequiresEnumeratedDevice:
        // offline packaging/digests, dry runs and a kickstart device node do
        // not need an enumerated EDL device.
        gui::QualcommController n;
        CHECK(n.needsDevice(int(core::Operation::QualcommFlash)));
        CHECK(n.needsDevice(int(core::Operation::QualcommRaw)));
        CHECK(n.needsDevice(int(core::Operation::QualcommChipInfo)));
        CHECK(n.needsDevice(int(core::Operation::QualcommReset)));
        CHECK(n.needsDevice(int(core::Operation::QualcommRamdump)));
        CHECK(n.needsDevice(int(core::Operation::QualcommKickstart)));
        CHECK(n.needsDevice(int(core::Operation::QualcommProvision)));
        CHECK(!n.needsDevice(int(core::Operation::QualcommCreateZip)));
        CHECK(!n.needsDevice(int(core::Operation::QualcommCreateArchive)));
        CHECK(!n.needsDevice(int(core::Operation::QualcommCreateDigests)));
        CHECK(!n.needsDevice(999));
        n.setValue("dryRun", true);
        CHECK(!n.needsDevice(int(core::Operation::QualcommFlash)));
        CHECK(n.needsDevice(int(core::Operation::QualcommChipInfo)));
        n.setContextValue("raw", "dryRun", true);
        CHECK(!n.needsDevice(int(core::Operation::QualcommRaw)));
        n.setContextValue("provision", "dryRun", true);
        CHECK(!n.needsDevice(int(core::Operation::QualcommProvision)));
        n.setContextValue("kickstart", "node", "/dev/ttyUSB0");
        CHECK(!n.needsDevice(int(core::Operation::QualcommKickstart)));
        n.setContextValue("kickstart", "node", "");
        CHECK(n.needsDevice(int(core::Operation::QualcommKickstart)));
    }
    {
        gui::QualcommController resetController;
        core::OperationRequest resetRequest; int resetRequests = 0;
        QObject::connect(&resetController, &gui::QualcommController::executeRequested,
            [&](const core::OperationRequest &request) { resetRequest = request; ++resetRequests; });
        const int operation = int(core::Operation::QualcommReset);
        CHECK(resetController.settingsFor("reset").value("resetMethod").toString() == "sahara");
        resetController.setContextValue("reset", "programmer", loader);
        resetController.run(operation);
        CHECK(resetRequests == 1 && resetRequest.qualcomm.programmer.empty() && resetRequest.qualcomm.images.empty());
        resetController.setContextValue("reset", "resetMethod", "firehose");
        resetController.setContextValue("reset", "storage", 1);
        resetController.run(operation);
        CHECK(resetRequests == 2 && resetRequest.operation == core::Operation::QualcommReset);
        CHECK(resetRequest.qualcomm.programmer == loader.toStdString()); CHECK(resetRequest.qualcomm.xml.empty());
        CHECK(resetRequest.qualcomm.storage == core::QualcommStorage::Emmc);
        resetController.setContextValue("reset", "programmer", temp.filePath("missing.mbn"));
        CHECK(!resetController.canRun(operation)); resetController.run(operation); CHECK(resetRequests == 2);
        resetController.setContextValue("reset", "resetMethod", "sahara");
        CHECK(resetController.canRun(operation)); resetController.run(operation);
        CHECK(resetRequests == 3 && resetRequest.qualcomm.programmer.empty());
        CHECK(resetController.settingsFor("reset").value("programmer").toString() == temp.filePath("missing.mbn"));
        resetController.setContextValue("reset", "resetMethod", "firehose");
        CHECK(!resetController.canRun(operation));
        resetController.setContextValue("reset", "programmer", "");
        CHECK(!resetController.canRun(operation));
        resetController.setContextValue("reset", "useImages", true);
        CHECK(!resetController.canRun(operation));
        resetController.setContextValue("reset", "images", "13:" + loader);
        resetController.run(operation);
        CHECK(resetRequests == 4 && resetRequest.qualcomm.images.size() == 1);
        CHECK(resetRequest.qualcomm.programmer.empty());
        resetController.setContextValue("reset", "resetMethod", "sahara");
        resetController.setContextValue("reset", "slot", "invalid");
        resetController.run(operation);
        CHECK(resetRequests == 5 && resetRequest.qualcomm.programmer.empty() && resetRequest.qualcomm.images.empty());
        CHECK(resetController.settingsFor("reset").value("images").toString() == "13:" + loader);
        resetController.setContextValue("reset", "resetMethod", "firehose");
        CHECK(!resetController.canRun(operation));
        resetController.setContextValue("reset", "slot", "");
        resetController.run(operation);
        CHECK(resetRequests == 6 && resetRequest.qualcomm.images.size() == 1);
    }
    core::OperationRequest submitted; int requests = 0;
    QObject::connect(&c, &gui::QualcommController::executeRequested, [&](const core::OperationRequest &r) { submitted = r; ++requests; });
    c.setValue("programmer",loader); c.addXml(raw);
    c.setValue("images","13:/missing/stale-loader.mbn"); c.setValue("dryRun",true);
    c.run(int(core::Operation::QualcommFlash)); CHECK(requests == 1); CHECK(submitted.qualcomm.images.empty());
    CHECK(submitted.qualcomm.programmer == loader.toStdString()); CHECK(submitted.qualcomm.dryRun);
    c.setValue("useImages",true); CHECK(!c.canRun(int(core::Operation::QualcommFlash)));
    c.setValue("images","13:" + loader + "\n13:" + loader); CHECK(!c.canRun(int(core::Operation::QualcommFlash)));
    c.setValue("images","0:" + loader); CHECK(!c.canRun(int(core::Operation::QualcommFlash)));
    c.setValue("images","13:" + loader); c.run(int(core::Operation::QualcommFlash));
    CHECK(requests == 2); CHECK(submitted.qualcomm.programmer.empty()); CHECK(submitted.qualcomm.images.size() == 1);
    CHECK(c.xmlFolder() == temp.path());
    c.setContextValue("provision","programmer",loader); c.addXml(ufs,"provision");
    c.run(int(core::Operation::QualcommProvision)); CHECK(requests == 3); CHECK(!submitted.qualcomm.dryRun);
    CHECK(submitted.qualcomm.xml.size() == 1 && submitted.qualcomm.xml[0] == ufs.toStdString());
    CHECK(submitted.qualcomm.storage == core::QualcommStorage::Ufs);
    c.setContextValue("provision","dryRun",true); c.run(int(core::Operation::QualcommProvision)); CHECK(submitted.qualcomm.dryRun);
    c.addXml(raw,"provision"); CHECK(!c.canRun(int(core::Operation::QualcommProvision))); c.removeXml(1,"provision");
    c.addXml(ufs); CHECK(!c.canRun(int(core::Operation::QualcommFlash))); c.removeXml(1);
    c.setContextValue("kickstart","programmer",loader); c.setContextValue("kickstart","node","/dev/test-node");
    c.setContextValue("kickstart","chunkSize","4096"); c.run(int(core::Operation::QualcommKickstart));
    CHECK(submitted.qualcomm.node == "/dev/test-node"); CHECK(submitted.qualcomm.chunkSize == 4096); CHECK(!submitted.qualcomm.dryRun);
    c.setContextValue("raw","programmer",loader);
    QVariantMap reset{{"type",4}}, read{{"type",0},{"address","0/34+1"},{"file",temp.filePath("backup.bin")}};
    CHECK(!c.canRun(int(core::Operation::QualcommRaw), {{"rawOperations",QVariantList{reset,read}}}));
    CHECK(c.canRun(int(core::Operation::QualcommRaw), {{"rawOperations",QVariantList{read,reset}}}));
    c.setContextValue("raw","chunkSize","invalid"); CHECK(c.canRun(int(core::Operation::QualcommChipInfo)));
    c.setContextValue("flash","transport",7); CHECK(c.settings().value("transport").toInt()==-1);
    c.setContextValue("flash","transport",0); CHECK(c.settings().value("transport").toInt()==0);
#ifndef Q_OS_WIN
    c.setContextValue("flash","transport",2); CHECK(c.settings().value("transport").toInt()==-1);
#endif
    CHECK(!c.canRun(int(core::Operation::QualcommRaw), {{"rawOperations",QVariantList{read}}}));
    c.setContextValue("zip","output","zip-output.zip"); c.setContextValue("archive","output","archive-output.bin");
    CHECK(c.settingsFor("zip").value("output").toString() == "zip-output.zip");
    CHECK(c.settingsFor("ramdump").value("output").toString().isEmpty());
    c.setValue("selector","old-selector"); c.loadFirmware(temp.path());
    CHECK(c.settings().value("images").toString().isEmpty()); CHECK(!c.settings().value("useImages").toBool());
    CHECK(c.settings().value("selector").toString().isEmpty());
    // No unique firehose programmer in the folder, so there is no anchor and
    // the XML picker falls back to its own remembered folder.
    CHECK(c.xmlFolder().isEmpty());
    c.loadFirmware(temp.filePath("new-package.zip")); CHECK(c.xmlFiles().isEmpty());
    CHECK(c.xmlFolder() == temp.path());
    QDir single(temp.filePath("single")); CHECK(QDir().mkpath(single.path()));
    Write(single.filePath("rawprogram0.xml"), "<data><program filename=\"boot.bin\"/></data>");
    Write(single.filePath("patch0.xml"), "<patches/>");
    c.loadFirmware(single.path()); CHECK(c.xmlFiles().isEmpty()); CHECK(c.xmlFolder().isEmpty());
    const QString raw2 = temp.filePath("rawprogram2.xml"), raw1 = temp.filePath("rawprogram1.xml");
    const QString patch = temp.filePath("patch0.xml"), broken = temp.filePath("broken.xml");
    Write(raw1, "<data><program filename=\"boot.bin\"/></data>");
    Write(raw2, "<data><erase start_sector=\"0\"/></data>");
    Write(patch, "<patches/>"); Write(broken, "<data>");
    CHECK(c.xmlFiles().isEmpty());
    c.setValue("programmer", loader); CHECK(c.xmlFolder() == temp.path());
    CHECK(c.replaceFlashXml({raw2,raw1,raw1},{patch}));
    CHECK(c.xmlFiles() == QStringList({raw1,raw2,patch}));
    {
        const auto entries = c.xmlEntriesFor("flash");
        CHECK(entries.size() == 3);
        CHECK(entries.at(0).toMap().value("name").toString() == "rawprogram1.xml");
        CHECK(entries.at(0).toMap().value("kind").toString() == "program");
        CHECK(entries.at(1).toMap().value("kind").toString() == "program");
        CHECK(entries.at(2).toMap().value("name").toString() == "patch0.xml");
        CHECK(entries.at(2).toMap().value("kind").toString() == "patch");
        CHECK(entries.at(2).toMap().value("path").toString() == patch);
        const auto provision = c.xmlEntriesFor("provision");
        CHECK(provision.size() == 1);
        CHECK(provision.at(0).toMap().value("name").toString() == "ufs.xml");
        CHECK(c.xmlEntriesFor("raw").isEmpty());
    }
    const auto original = c.xmlFiles();
    CHECK(!c.replaceFlashXml({raw1},{ufs})); CHECK(c.xmlFiles() == original);
    CHECK(!c.replaceFlashXml({broken},{patch})); CHECK(c.xmlFiles() == original);
    CHECK(!c.replaceFlashXml({}, {patch})); CHECK(c.xmlFiles() == original);
    CHECK(!c.replaceFlashXml({patch},{raw1})); CHECK(c.xmlFiles() == original);
    CHECK(!c.replaceFlashXml({temp.filePath("missing.xml")},{})); CHECK(c.xmlFiles() == original);
    c.setLocked(true); CHECK(!c.replaceFlashXml({raw1},{})); CHECK(c.xmlFiles() == original);
    c.setLocked(false); CHECK(c.replaceFlashXml({raw1},{})); CHECK(c.xmlFiles() == QStringList({raw1}));
    core::OperationResult error; error.operation=core::Operation::QualcommRaw; error.diagnostic="device disappeared";
    c.applyResult(error); CHECK(c.operationErrorFor("raw") == "device disappeared"); CHECK(c.operationErrorFor("flash").isEmpty());
    CHECK(c.resultsFor("raw")->rowCount() == 0); CHECK(c.resultsFor("flash")->rowCount() == 0);
    CHECK(c.results()->rowCount() == 0);
    for (auto op : {core::Operation::QualcommChipInfo, core::Operation::QualcommReset, core::Operation::QualcommFlash}) {
        error.operation = op; c.applyResult(error);
        const auto context = op == core::Operation::QualcommChipInfo ? "chipinfo" : op == core::Operation::QualcommReset ? "reset" : "flash";
        CHECK(c.operationErrorFor(context) == "device disappeared");
        CHECK(c.resultsFor(context)->rowCount() == 0); CHECK(c.results()->rowCount() == 0);
    }
    core::OperationResult chip; chip.operation = core::Operation::QualcommChipInfo;
    chip.status = core::OperationStatus::Succeeded; chip.items.push_back({"Chip", "test-chip"}); c.applyResult(chip);
    CHECK(c.operationErrorFor("chipinfo").isEmpty());
    CHECK(c.resultsFor("chipinfo")->rowCount() == 1); CHECK(c.results()->rowCount() == 1);
    return failures ? 1 : 0;
}
