#pragma once
#include "platform/LogService.h"
#include <QJsonObject>
namespace gui {
void WriteDiagnostic(platform::LogService &log, const char *source, const QJsonObject &record);
}
