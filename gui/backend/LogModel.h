#pragma once
#include "domain/LogEntry.h"
#include <QAbstractListModel>
#include <QList>

namespace gui {
class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { TextRole = Qt::UserRole + 1, ErrorRole, TimeRole, LevelRole, SourceRole, ContextRole };
    explicit LogModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_lines.size(); }
    void append(const core::LogEntry &entry);
    Q_INVOKABLE void clear();
signals:
    void countChanged();
private:
    struct Line { QString text, time, level, source, context; bool error; };
    QList<Line> m_lines;
};
}
