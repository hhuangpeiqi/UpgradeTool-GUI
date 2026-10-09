#include "gui/backend/KeyValueModel.h"

#include <QVariantMap>

namespace gui {

KeyValueModel::KeyValueModel(QObject *parent)
	: QAbstractListModel(parent)
{
}

int KeyValueModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return m_rows.size();
}

QVariant KeyValueModel::data(const QModelIndex &index, int iRole) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
		return QVariant();
	const Row &row = m_rows[index.row()];
	switch (iRole) {
	case LabelRole: return row.label;
	case ValueRole: return row.value;
	default:        return QVariant();
	}
}

QHash<int, QByteArray> KeyValueModel::roleNames() const
{
	QHash<int, QByteArray> roles;
	roles[LabelRole] = "label";
	roles[ValueRole] = "value";
	return roles;
}

void KeyValueModel::SetItems(const QVariantList &items)
{
	beginResetModel();
	m_rows.clear();
	for (int i = 0; i < items.size(); i++) {
		QVariantMap map = items[i].toMap();
		m_rows.append(Row{ map["label"].toString(), map["value"].toString() });
	}
	endResetModel();
	emit countChanged();
}

void KeyValueModel::Clear()
{
	if (m_rows.isEmpty())
		return;
	beginResetModel();
	m_rows.clear();
	endResetModel();
	emit countChanged();
}

}
