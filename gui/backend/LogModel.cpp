#include "gui/backend/LogModel.h"
#include "gui/MessageDisplay.h"
#include <QDateTime>

namespace gui {
LogModel::LogModel(QObject *parent) : QAbstractListModel(parent) {}
int LogModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_lines.size(); }
QVariant LogModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size()) return {};
    const auto &line = m_lines[index.row()];
    switch (role) {
    case TextRole: return line.text;
    case ErrorRole: return line.error;
    case TimeRole: return line.time;
    case LevelRole: return line.level;
    case SourceRole: return line.source;
    case ContextRole: return line.context;
    default: return {};
    }
}
QHash<int, QByteArray> LogModel::roleNames() const {
    return {{TextRole, "text"}, {ErrorRole, "error"}, {TimeRole, "time"}, {LevelRole, "level"},
            {SourceRole, "source"}, {ContextRole, "context"}};
}
void LogModel::append(const core::LogEntry &entry) {
    QString text = entry.displayText.empty() ? Display(entry.message) : QString::fromStdString(entry.displayText);
    text.replace("\r\n", "\n").replace('\r', '\n').replace('\t', "    ");
    while (text.endsWith('\n')) text.chop(1);
    if (text.trimmed().isEmpty()) return;
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch()).count();
    QString context;
    if (entry.context.task) context = QString("task=%1").arg(entry.context.task);
    if (entry.context.step) context += QString(" step=%1").arg(entry.context.step);
    if (!entry.context.device.empty()) context += " " + QString::fromStdString(entry.context.device);
    // Remove before insertion so the view can preserve its scroll position.
    if (m_lines.size() == 5000) {
        beginRemoveRows({}, 0, 0); m_lines.removeFirst(); endRemoveRows();
    }
    beginInsertRows({}, m_lines.size(), m_lines.size());
    m_lines.append({text, QDateTime::fromMSecsSinceEpoch(millis).toString("HH:mm:ss.zzz"),
                    core::LogLevelName(entry.level), QString::fromStdString(entry.context.source),
                    context.trimmed(), entry.level == core::LogLevel::Error});
    endInsertRows();
    emit countChanged();
}
void LogModel::clear() {
    if (m_lines.isEmpty()) return;
    beginResetModel(); m_lines.clear(); endResetModel(); emit countChanged();
}
}
