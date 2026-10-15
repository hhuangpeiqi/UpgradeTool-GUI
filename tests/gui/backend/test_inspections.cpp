#include "gui/backend/RockchipController.h"
#include "gui/backend/AllwinnerController.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
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
