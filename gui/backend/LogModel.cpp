#include "gui/backend/LogModel.h"

namespace gui {

namespace {
const int kMaxLines = 5000;

QString Chomp(const QString &strText)
{
	QString strLine = strText;
	strLine.replace('\t', "    ");
	while (strLine.endsWith('\n') || strLine.endsWith('\r'))
		strLine.chop(1);
	return strLine;
}
}

LogModel::LogModel(QObject *parent)
	: QAbstractListModel(parent)
{
}

int LogModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return m_lines.size();
}

QVariant LogModel::data(const QModelIndex &index, int iRole) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size())
		return QVariant();
	const Line &line = m_lines[index.row()];
	switch (iRole) {
	case TextRole:     return line.text;
	case ErrorRole:    return line.error;
	case ProgressRole: return line.progress;
	default:           return QVariant();
	}
}

QHash<int, QByteArray> LogModel::roleNames() const
{
	QHash<int, QByteArray> roles;
	roles[TextRole] = "text";
	roles[ErrorRole] = "error";
	roles[ProgressRole] = "isProgress";
	return roles;
}

void LogModel::Append(const QString &strText, bool bError)
{
	QString strLine = Chomp(strText);

	beginInsertRows(QModelIndex(), m_lines.size(), m_lines.size());
	m_lines.append(Line{ strLine, bError, false });
	endInsertRows();

	if (m_lines.size() > kMaxLines) {
		beginRemoveRows(QModelIndex(), 0, m_lines.size() - kMaxLines - 1);
		m_lines.erase(m_lines.begin(), m_lines.begin() + (m_lines.size() - kMaxLines));
		endRemoveRows();
	}

	emit countChanged();
}

void LogModel::clear()
{
	if (m_lines.isEmpty())
		return;
	beginResetModel();
	m_lines.clear();
	endResetModel();
	emit countChanged();
}

}
