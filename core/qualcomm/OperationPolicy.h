#ifndef CORE_QUALCOMM_OPERATION_POLICY_H
#define CORE_QUALCOMM_OPERATION_POLICY_H
#include "core/Operation.h"
#include "core/qualcomm/DevicePolicy.h"
#include <QCoreApplication>
namespace core { namespace qualcomm {
inline QString EligibilityReason(Operation operation, qtq_mode mode, int accessError = 0)
{
    const char *reason = nullptr;
    if (!qtq_operation_allowed(mode, operation == Operation::QualcommRamdump))
        reason = mode == QTQ_DUMP
            ? QT_TRANSLATE_NOOP("QualcommBackend", "Crash-dump mode only supports RAM dump; switch to EDL (9008) for this operation")
            : QT_TRANSLATE_NOOP("QualcommBackend", "The selected interface is not an EDL (9008) target");
    else if (accessError)
        reason = QT_TRANSLATE_NOOP("QualcommBackend", "The selected interface cannot be opened; check permissions and the USB driver");
    return reason ? QCoreApplication::translate("QualcommBackend", reason) : QString();
}
inline const char *ModeLabel(qtq_mode mode)
{
    switch (mode) {
    case QTQ_EDL: return QT_TRANSLATE_NOOP("QualcommBackend", "EDL");
    case QTQ_DUMP: return QT_TRANSLATE_NOOP("QualcommBackend", "Crash-dump candidate");
    case QTQ_DIAG: return QT_TRANSLATE_NOOP("QualcommBackend", "DIAG");
    case QTQ_ADB: return "ADB";
    default: return QT_TRANSLATE_NOOP("QualcommBackend", "Unknown mode");
    }
}
}}
#endif
