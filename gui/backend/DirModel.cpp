#include "gui/backend/DirModel.h"

#include <QDir>
#include <QFileInfo>
#include <QVector>

#include <algorithm>
#include <utility>

namespace gui {

namespace {

QString ExpandUser(const QString &strPath)
{
	if (strPath == "~")
		return QDir::homePath();
	if (strPath.startsWith("~/"))
		return QDir::homePath() + strPath.mid(1);
	return strPath;
}

bool NameMatches(const QStringList &globs, const QString &strName)
{
	if (globs.isEmpty())
		return true;
	for (int i = 0; i < globs.size(); i++) {
		const QString &strGlob = globs[i];
		if (strGlob.isEmpty() || strGlob == "*")
			return true;
		if (QDir::match(strGlob, strName))
			return true;
	}
	return false;
}

}

DirModel::DirModel(QObject *parent)
	: QAbstractListModel(parent)
	, m_bShowFiles(true)
	, m_bShowHidden(false)
{
	m_strRoot = QDir::homePath();
	Reload();
}

int DirModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return m_entries.size();
}

QVariant DirModel::data(const QModelIndex &index, int iRole) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
		return QVariant();
	const DirEntry &entry = m_entries[index.row()];
	switch (iRole) {
	case NameRole:     return entry.name;
	case PathRole:     return entry.path;
	case IsDirRole:    return entry.isDir;
	case IsHiddenRole: return entry.isHidden;
	case SizeRole:     return entry.size;
	case ModifiedRole: return entry.modified;
	default:           return QVariant();
	}
}

QHash<int, QByteArray> DirModel::roleNames() const
{
	QHash<int, QByteArray> roles;
	roles[NameRole] = "name";
	roles[PathRole] = "path";
	roles[IsDirRole] = "isDir";
	roles[IsHiddenRole] = "isHidden";
	roles[SizeRole] = "size";
	roles[ModifiedRole] = "modified";
	return roles;
}

void DirModel::setRootPath(const QString &strPath)
{
	QString strNormalized = ExpandUser(strPath.trimmed());
	if (strNormalized.isEmpty())
		strNormalized = QDir::homePath();

	QFileInfo info(strNormalized);
	if (info.isFile())
		strNormalized = QDir::cleanPath(info.absolutePath());
	else
		strNormalized = QDir::cleanPath(info.absoluteFilePath());

	if (strNormalized == m_strRoot)
		return;
	m_strRoot = strNormalized;
	emit rootPathChanged();
	Reload();
}

void DirModel::setShowFiles(bool bShow)
{
	if (m_bShowFiles == bShow)
		return;
	m_bShowFiles = bShow;
	emit showFilesChanged();
	Reload();
}

void DirModel::setShowHidden(bool bShow)
{
	if (m_bShowHidden == bShow)
		return;
	m_bShowHidden = bShow;
	emit showHiddenChanged();
	Reload();
}

void DirModel::setNameFilters(const QStringList &filters)
{
	if (m_filters == filters)
		return;
	m_filters = filters;
	emit nameFiltersChanged();
	Reload();
}

void DirModel::setSearchText(const QString &strText)
{
	const QString strTrimmed = strText.trimmed();
	if (m_strSearch == strTrimmed)
		return;
	m_strSearch = strTrimmed;
	emit searchTextChanged();
	ApplySearch();
}

void DirModel::refresh()
{
	Reload();
}

QString DirModel::nameAt(int iRow) const
{
	if (iRow < 0 || iRow >= m_entries.size())
		return QString();
	return m_entries[iRow].name;
}

QString DirModel::pathAt(int iRow) const
{
	if (iRow < 0 || iRow >= m_entries.size())
		return QString();
	return m_entries[iRow].path;
}

bool DirModel::isDirAt(int iRow) const
{
	if (iRow < 0 || iRow >= m_entries.size())
		return false;
	return m_entries[iRow].isDir;
}

bool DirModel::makeDirectory(const QString &strName)
{
	QString strTrimmed = strName.trimmed();
	if (strTrimmed.isEmpty()) {
		SetError([] { return tr("Directory name cannot be empty"); });
		return false;
	}
	if (strTrimmed.contains('/') || strTrimmed == "." || strTrimmed == "..") {
		SetError([] { return tr("Invalid directory name"); });
		return false;
	}

	QDir dir(m_strRoot);
	if (!dir.exists()) {
		SetError([] { return tr("Current directory does not exist"); });
		return false;
	}
	if (dir.exists(strTrimmed)) {
		SetError([] { return tr("A file or directory with the same name already exists"); });
		return false;
	}
	if (!dir.mkdir(strTrimmed)) {
		SetError([] { return tr("Failed to create directory"); });
		return false;
	}

	ClearError();
	Reload();
	return true;
}

void DirModel::Reload()
{
	m_scanned.clear();

	QDir dir(m_strRoot);
	if (!dir.exists()) {
		beginResetModel();
		m_entries.clear();
		endResetModel();
		SetError([] { return tr("Directory does not exist"); });
		emit countChanged();
		return;
	}
	if (!dir.isReadable()) {
		beginResetModel();
		m_entries.clear();
		endResetModel();
		SetError([] { return tr("Cannot read the directory (permission denied)"); });
		emit countChanged();
		return;
	}

	QDir::Filters filters = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable;
	if (m_bShowHidden)
		filters |= QDir::Hidden | QDir::System;

	QFileInfoList infoList = dir.entryInfoList(filters, QDir::NoSort);
	std::sort(infoList.begin(), infoList.end(),
	          [](const QFileInfo &a, const QFileInfo &b) {
		if (a.isDir() != b.isDir())
			return a.isDir();
		return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
	});

	for (int i = 0; i < infoList.size(); i++) {
		const QFileInfo &info = infoList[i];
		if (!info.isDir()) {
			if (!m_bShowFiles)
				continue;
			if (!NameMatches(m_filters, info.fileName()))
				continue;
		}
		DirEntry entry;
		entry.name = info.fileName();
		entry.path = info.absoluteFilePath();
		entry.isDir = info.isDir();
		entry.isHidden = info.isHidden();
		entry.size = entry.isDir ? 0 : info.size();
		entry.modified = info.lastModified();
		m_scanned.append(entry);
	}

	ApplySearch();
	ClearError();
}

void DirModel::ApplySearch()
{
	beginResetModel();
	m_entries.clear();
	if (!m_strSearch.isEmpty()) {
		for (int i = 0; i < m_scanned.size(); i++) {
			const DirEntry &entry = m_scanned[i];
			if (entry.name.contains(m_strSearch, Qt::CaseInsensitive))
				m_entries.append(entry);
		}
	} else {
		m_entries = m_scanned;
	}
	endResetModel();
	emit countChanged();
}

void DirModel::SetError(TranslatableText::Factory factory)
{
	const QString strText = factory ? factory() : QString();
	if (m_error.value() == strText && m_error.bound() == static_cast<bool>(factory))
		return;
	m_error.Set(std::move(factory));
	emit errorTextChanged();
}

void DirModel::ClearError()
{
	if (!m_error.bound() && m_error.value().isEmpty())
		return;
	m_error.Clear();
	emit errorTextChanged();
}

void DirModel::retranslate()
{
	if (!m_error.bound())
		return;
	m_error.retranslate();
	emit errorTextChanged();
}

}
