#include "gui/DiagnosticLog.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace gui {
namespace {
QString Value(const QJsonValue &value) {
    if (value.isString()) return value.toString();
    if (value.isBool()) return value.toBool() ? "true" : "false";
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 16);
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    return "null";
}
}
void WriteDiagnostic(platform::LogService &log, const char *source, const QJsonObject &record) {
    const auto event = record.value("event").toString();
    QStringList lines{event};
    // Each field has its own line, so commands, paths and output keep their boundaries.
    for (auto it = record.begin(); it != record.end(); ++it) {
        if (it.key() == "event" || it.key().startsWith("log_")) continue;
        lines << it.key() + "=" + Value(it.value());
    }
    core::LogEntry entry;
    entry.level = static_cast<core::LogLevel>(record.value("log_level").toInt(int(core::LogLevel::Debug)));
    entry.context = {source, static_cast<uint64_t>(record.value("log_task").toInteger()), 0,
                     record.value("log_device").toString().toStdString()};
    entry.visible = false;
    entry.message = {{}, lines.join('\n').toStdString(), {}};
    log.write(std::move(entry));
}
}
