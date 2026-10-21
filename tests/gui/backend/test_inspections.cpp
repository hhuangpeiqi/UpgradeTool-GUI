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
