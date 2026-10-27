#include "backends/BackendFactory.h"
#include "application/BuildFeatures.h"
#if UPGRADE_TOOL_ROCKCHIP
#include "backends/rockchip/RockchipBackend.h"
#endif
#if UPGRADE_TOOL_ALLWINNER
#include "backends/allwinner/AllwinnerBackend.h"
#endif
#if UPGRADE_TOOL_QUALCOMM
#include "backends/qualcomm/QualcommBackend.h"
#endif
namespace core {
std::vector<std::unique_ptr<IDeviceBackend>> CreateBackends(platform::LogService *log) {
    std::vector<std::unique_ptr<IDeviceBackend>> result;
#if UPGRADE_TOOL_ROCKCHIP
    result.push_back(std::make_unique<RockchipBackend>(log));
#endif
#if UPGRADE_TOOL_ALLWINNER
    result.push_back(std::make_unique<allwinner::AllwinnerBackend>());
#endif
#if UPGRADE_TOOL_QUALCOMM
    result.push_back(std::make_unique<qualcomm::QualcommBackend>());
#endif
    (void)log; return result;
}
}
