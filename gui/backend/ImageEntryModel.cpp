#include "gui/backend/ImageEntryModel.h"

#include <QVector>

namespace gui {

ImageEntryModel::ImageEntryModel(QObject *parent)
	: QAbstractListModel(parent)
{
}

int ImageEntryModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return m_rows.size();
}

QVariant ImageEntryModel::data(const QModelIndex &index, int iRole) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
		return QVariant();
	const ImageRow &row = m_rows[index.row()];
	switch (iRole) {
	case NoRole:         return index.row() + 1;
	case SelectableRole: return row.selectable;
	case CheckedRole:    return row.checked;
	case StorageRole:    return row.storage;
	case AddrRole:       return row.addr;
	case NameRole:       return row.name;
	case PathRole:       return row.path;
	case IsLoaderRole:   return row.isLoader;
	default:             return QVariant();
	}
}

QHash<int, QByteArray> ImageEntryModel::roleNames() const
{
	QHash<int, QByteArray> roles;
	roles[NoRole] = "no";
	roles[SelectableRole] = "selectable";
	roles[CheckedRole] = "checked";
	roles[StorageRole] = "storage";
	roles[AddrRole] = "addr";
	roles[NameRole] = "name";
	roles[PathRole] = "path";
	roles[IsLoaderRole] = "isLoader";
	return roles;
}

int ImageEntryModel::checkableCount() const
{
	int iCount = 0;
	for (int i = 0; i < m_rows.size(); i++)
		if (m_rows[i].selectable)
			iCount++;
	return iCount;
}

int ImageEntryModel::checkedCount() const
{
	int iCount = 0;
	for (int i = 0; i < m_rows.size(); i++)
		if (m_rows[i].selectable && m_rows[i].checked)
			iCount++;
	return iCount;
}

void ImageEntryModel::SetRows(const QList<ImageRow> &rows)
{
	beginResetModel();
	m_rows = rows;
	endResetModel();
	emit countChanged();
	emit countsChanged();
}

void ImageEntryModel::Clear()
{
	if (m_rows.isEmpty())
		return;
	beginResetModel();
	m_rows.clear();
	endResetModel();
	emit countChanged();
	emit countsChanged();
}

void ImageEntryModel::setChecked(int iRow, bool bChecked)
{
	if (iRow < 0 || iRow >= m_rows.size())
		return;
	if (!m_rows[iRow].selectable)
		return;
	if (m_rows[iRow].checked == bChecked)
		return;
	m_rows[iRow].checked = bChecked;
	QModelIndex idx = index(iRow, 0);
	emit dataChanged(idx, idx, QVector<int>() << CheckedRole);
	emit countsChanged();
}

void ImageEntryModel::setAllChecked(bool bChecked)
{
	bool bChanged = false;
	for (int i = 0; i < m_rows.size(); i++) {
		if (!m_rows[i].selectable || m_rows[i].checked == bChecked)
			continue;
		m_rows[i].checked = bChecked;
		bChanged = true;
	}
	if (!bChanged)
		return;
	if (!m_rows.isEmpty())
		emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0), QVector<int>() << CheckedRole);
	emit countsChanged();
}

void ImageEntryModel::setPath(int iRow, const QString &strPath)
{
	if (iRow < 0 || iRow >= m_rows.size())
		return;
	QString strValue = strPath.trimmed();
	if (m_rows[iRow].path == strValue)
		return;
	m_rows[iRow].path = strValue;
	if (!strValue.isEmpty())
		m_rows[iRow].selectable = true;
	QModelIndex idx = index(iRow, 0);
	emit dataChanged(idx, idx, QVector<int>() << PathRole << SelectableRole);
	emit countsChanged();
}

}
