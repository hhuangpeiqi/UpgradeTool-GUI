#ifndef CORE_QUALCOMM_OPERATION_POLICY_H
#define CORE_QUALCOMM_OPERATION_POLICY_H
#include "domain/Operation.h"
#include "backends/qualcomm/DevicePolicy.h"
#include <string>
namespace core { namespace qualcomm {
inline std::string EligibilityReason(Operation operation, qtq_mode mode, int accessError = 0)
{
    const char *reason = nullptr;
    if (!qtq_operation_allowed(mode, operation == Operation::QualcommRamdump))
        reason = mode == QTQ_DUMP
            ? "Crash-dump mode only supports RAM dump; switch to EDL (9008) for this operation"
            : "The selected interface is not an EDL (9008) target";
    else if (accessError)
        reason = "The selected interface cannot be opened; check permissions and the USB driver";
    return reason ? reason : "";
}
inline const char *ModeLabel(qtq_mode mode)
{
    switch (mode) {
    case QTQ_EDL: return "EDL";
    case QTQ_DUMP: return "Crash-dump candidate";
    case QTQ_DIAG: return "DIAG";
    case QTQ_ADB: return "ADB";
    default: return "Unknown mode";
    }
}
}}
#endif
