#include "gui/InspectionQueue.h"
#include "tests/support/qt/Await.h"
#include "gui/backend/RockchipController.h"
#include "gui/backend/AllwinnerController.h"
#include "gui/EngineWorker.h"
#include "backends/rockchip/legacy/RKBoot.h"
#include "backends/rockchip/legacy/crc.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #value); ++failures; } } while (0)
int main(int argc, char **argv) {
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    QCoreApplication app(argc, argv);
    app.setOrganizationName("inspection-tests");
    app.setApplicationName("inspection-tests");
    core::OperationResult result;
    result.status = core::OperationStatus::Succeeded;
    result.firmware.valid = true;
    result.firmware.version = "current";
    core::FirmwareEntry entry;
    entry.partName = "boot"; entry.fileName = "boot.img"; entry.imageLike = true;
    result.firmware.entries.push_back(entry);
    {
        gui::RockchipController controller(nullptr);
        std::vector<core::InspectionRequest> requests;
        QObject::connect(&controller, &gui::RockchipController::inspectRequested,
            [&](const auto &request) { requests.push_back(request); });
        controller.setFirmware("first.img");
        controller.setFirmware("second.img");
        CHECK(requests.size() == 2);
        CHECK(requests[0].cancelled->stop_requested());
        controller.applyInspection(5, requests[0].generation, result);
        CHECK(!controller.firmwareValid());
        controller.applyInspection(5, requests[1].generation, result);
        CHECK(controller.firmwareValid());
        CHECK(controller.firmwareVersion() == "current");
        const auto flashHint = controller.firmwareHint();
        controller.inspectAdvancedFirmware("tools-first.img");
        const auto stale = requests.back();
        controller.inspectAdvancedFirmware("tools-second.img");
        CHECK(stale.cancelled->stop_requested());
        const auto advanced = requests.back();
        auto detail = result;
        detail.operation = core::Operation::FirmwareInfo;
        detail.firmware.version = "tools-version";
        controller.applyInspection(stale.context, stale.generation, detail);
        CHECK(controller.firmwareEntries()->count() == 0);
        controller.applyInspection(advanced.context, advanced.generation, detail);
        CHECK(controller.firmwareEntries()->count() == 6);
        CHECK(controller.firmwarePath() == "second.img");
        CHECK(controller.firmwareVersion() == "current" && controller.firmwareValid());
        CHECK(controller.firmwareHint() == flashHint);
        controller.inspectAdvancedFirmware("");
        CHECK(controller.firmwareEntries()->count() == 0);
        controller.applyInspection(advanced.context, advanced.generation, detail);
        CHECK(controller.firmwareEntries()->count() == 0);
        controller.inspectAdvancedFirmware("bad.img");
        const auto invalid = requests.back();
        detail.status = core::OperationStatus::Failed;
        detail.error = core::ErrorCode::InvalidFirmware;
        controller.applyInspection(invalid.context, invalid.generation, detail);
        CHECK(controller.advancedHintError());
        CHECK(controller.firmwareEntries()->count() == 0);
        CHECK(controller.firmwareVersion() == "current" && controller.firmwareValid());
        CHECK(controller.firmwareHint() == flashHint);
        controller.clearFirmware();
        controller.applyInspection(5, requests[1].generation, result);
        CHECK(!controller.firmwareValid());
        controller.setFirmware("third.img");
        core::OperationResult failed;
        failed.status = core::OperationStatus::Cancelled;
        controller.applyInspection(5, requests.back().generation, failed);
        CHECK(!controller.firmwareValid() && !controller.firmwareHintError());
        CHECK(controller.firmwareHint() == "Cancelled");
        failed.status = core::OperationStatus::Failed;
        failed.error = core::ErrorCode::FileAccess;
        controller.applyInspection(5, requests.back().generation, failed);
        CHECK(!controller.firmwareValid() && controller.firmwareHintError());
        CHECK(controller.firmwareHint() == "Cannot open file; check the path and permissions");
        failed.error = core::ErrorCode::InvalidFirmware;
        controller.applyInspection(5, requests.back().generation, failed);
        CHECK(controller.firmwareHint() == "Invalid firmware.");
        controller.setImageDir("first");
        const auto folder = requests.back();
        controller.clearImage();
        CHECK(folder.cancelled->stop_requested());
        controller.applyInspection(4, folder.generation, result);
        CHECK(controller.imageEntries()->rowCount() == 0);
        controller.setImageDir("second");
        controller.applyInspection(4, requests.back().generation, result);
        CHECK(controller.imageEntries()->rowCount() == 1);
        controller.cancelInspections();
        CHECK(requests.back().cancelled->stop_requested());
    }
    {
        // Exercise the real controller -> worker -> registry -> RK backend path.
        std::vector<unsigned char> boot(sizeof(STRUCT_RKBOOT_HEAD) + 4, 0);
        STRUCT_RKBOOT_HEAD head = {};
        head.uiTag = 0x544f4f42; head.usSize = sizeof(head); head.dwVersion = 0x102;
        std::memcpy(boot.data(), &head, sizeof(head));
        const auto crc = CRC_32(boot.data(), boot.size() - 4);
        for (int i = 0; i < 4; ++i) boot[boot.size() - 4 + i] = static_cast<unsigned char>(crc >> (8 * i));
        const auto directory = temporary.filePath(QStringLiteral("\u6d4b\u8bd5 package"));
        CHECK(QDir().mkpath(directory));
        const auto path = QDir(directory).filePath(QStringLiteral("\u52a0\u8f7d Loader.bin"));
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly));
        CHECK(file.write(reinterpret_cast<const char *>(boot.data()), boot.size()) == qint64(boot.size()));
        file.close();
        {
            gui::InspectionQueue queue;
            gui::RockchipController queued(nullptr);
            QObject::connect(&queued, &gui::RockchipController::inspectRequested, &queue,
                [&](const core::InspectionRequest &request) { queue.inspect(&queued, request); });
            QObject::connect(&queue, &gui::InspectionQueue::inspectionFinished, &queued, &gui::RockchipController::applyInspection);
            queued.setFirmware(path);
            CHECK(Await([&] { return queued.firmwareValid(); }));
            const auto flashVersion = queued.firmwareVersion();
            queued.inspectAdvancedFirmware(path);
            CHECK(Await([&] { return queued.firmwareEntries()->count() >= 5; }));
            CHECK(queued.firmwareVersion() == flashVersion && queued.firmwareValid());
            queued.clearFirmware();
            CHECK(queued.firmwareEntries()->count() >= 5);
            queued.cancelInspections();
        }
        gui::EngineWorker worker(nullptr);
        gui::RockchipController controller(nullptr);
        QObject::connect(&controller, &gui::RockchipController::inspectRequested, &worker, &gui::EngineWorker::inspect);
        QObject::connect(&worker, &gui::EngineWorker::inspectionFinished, &controller, &gui::RockchipController::applyInspection);
        controller.setFirmware(path);
        CHECK(controller.firmwareValid());
        QFile parameter(QDir(directory).filePath("parameter.txt"));
        CHECK(parameter.open(QIODevice::WriteOnly));
        CHECK(parameter.write("TYPE: GPT\nCMDLINE:mtdparts=rk29xxnand:0x1000@0x2000(boot)\n") > 0);
        parameter.close();
        QFile partition(QDir(directory).filePath("boot.img"));
        CHECK(partition.open(QIODevice::WriteOnly));
        CHECK(partition.write("boot") == 4); partition.close();
        controller.setImageDir(directory);
        CHECK(!controller.imageHintError() && controller.imageEntries()->rowCount() == 3);
        const auto &rows = controller.imageEntries()->rows();
        if (rows.size() == 3) {
            CHECK(rows[0].isLoader && rows[0].path == path);
            CHECK(rows[2].name == "boot" && rows[2].path == partition.fileName() && rows[2].selectable);
        }
        std::vector<core::TaskJob> jobs;
        QObject::connect(&controller, &gui::RockchipController::executeRequested,
            [&](const auto &job) { jobs.push_back(job); });
        controller.imageEntries()->setAllChecked(true);
        for (bool reboot : {false, true}) {
            jobs.clear();
            controller.executeImageDownload(reboot);
            CHECK(jobs.size() == 1);
            const auto &job = jobs.back();
            CHECK(job.rebootAfter == reboot && job.steps.size() == 3);
            if (job.steps.size() == 3) {
                CHECK(job.steps[0].operation() == core::Operation::LoaderUpgrade);
                CHECK(!job.steps[0].rockchip().reboot);
                CHECK(job.steps[1].operation() == core::Operation::DownloadImage);
                CHECK(job.steps[1].rockchip().partition == "parameter");
                CHECK(job.steps[2].rockchip().partition == "boot");
            }
            controller.executeFirmwareUpgrade(reboot);
            CHECK(jobs.size() == 2);
            CHECK(jobs.back().steps.size() == 1);
            CHECK(jobs.back().steps.front().operation() == core::Operation::FirmwareUpgrade);
            CHECK(jobs.back().steps.front().rockchip().reboot == reboot);
        }
        controller.runOperation(static_cast<int>(core::Operation::LoaderUpgrade), {{"path", path}});
        CHECK(jobs.back().steps.front().rockchip().reboot); // Standalone default is unchanged.
        core::OperationResult resetFailure;
        resetFailure.operation = core::Operation::FirmwareUpgrade;
        resetFailure.status = core::OperationStatus::Failed;
        resetFailure.error = core::ErrorCode::OperationFailed;
        controller.applyResult(resetFailure);
        CHECK(controller.firmwareHintError());
        CHECK(!controller.firmwareHint().contains("device rebooted"));
        // setFirmware persists the path, so restore submits the startup inspection.
        controller.restore();
        CHECK(controller.firmwareValid());
        controller.setFirmware(path);
        CHECK(controller.firmwareValid() && controller.firmwareLoaderVersion() == "1.02");
        controller.clearFirmware();
        CHECK(!controller.firmwareValid());
        controller.setFirmware(path);
        CHECK(controller.firmwareValid());
        controller.setFirmware(temporary.filePath("missing.img"));
        CHECK(!controller.firmwareValid() && controller.firmwareHintError());
        CHECK(controller.firmwareHint() == "Cannot open file; check the path and permissions");
        controller.setFirmware(path);
        CHECK(controller.firmwareValid());
        boot[10] ^= 1;
        CHECK(file.open(QIODevice::WriteOnly));
        CHECK(file.write(reinterpret_cast<const char *>(boot.data()), boot.size()) == qint64(boot.size()));
        file.close();
        controller.setFirmware(path);
        CHECK(!controller.firmwareValid() && controller.firmwareHintError());
        CHECK(controller.firmwareHint() == "Invalid firmware.");
    }
    {
        gui::AllwinnerController controller;
        controller.setActive(true);
        std::vector<core::InspectionRequest> requests;
        QObject::connect(&controller, &gui::AllwinnerController::inspectRequested,
            [&](const auto &request) { requests.push_back(request); });
        controller.setContextFirmware(1, "first.img");
        controller.setContextFirmware(1, "second.img");
        CHECK(requests.size() == 2);
        CHECK(requests[0].cancelled->stop_requested());
        controller.applyInspection(1, requests[0].generation, result);
        CHECK(!controller.valid());
        controller.applyInspection(1, requests[1].generation, result);
        CHECK(controller.valid());
        CHECK(controller.files().size() == 1);
        controller.setContextFirmware(1, "");
        controller.applyInspection(1, requests[1].generation, result);
        CHECK(!controller.valid());
        CHECK(controller.files().isEmpty());
        controller.setContextFirmware(0, "third.img");
        controller.cancelInspections();
        CHECK(requests.back().cancelled->stop_requested());
    }
    return failures ? 1 : 0;
}
