#ifndef GUI_DIR_MODEL_H
#define GUI_DIR_MODEL_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "gui/backend/Retranslatable.h"

namespace gui {

struct DirEntry {
	QString name;
	QString path;
	bool isDir;
	bool isHidden;
	qint64 size;
	QDateTime modified;
};

class DirModel : public QAbstractListModel, public Retranslatable
{
	Q_OBJECT
	Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)
	Q_PROPERTY(bool showFiles READ showFiles WRITE setShowFiles NOTIFY showFilesChanged)
	Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
	Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters NOTIFY nameFiltersChanged)
	Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
	enum Roles {
		NameRole = Qt::UserRole + 1,
		PathRole,
		IsDirRole,
		IsHiddenRole,
		SizeRole,
		ModifiedRole
	};

	explicit DirModel(QObject *parent = NULL);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int iRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	QString rootPath() const { return m_strRoot; }
	bool showFiles() const { return m_bShowFiles; }
	bool showHidden() const { return m_bShowHidden; }
	QStringList nameFilters() const { return m_filters; }
	QString searchText() const { return m_strSearch; }
	int count() const { return m_entries.size(); }
	QString errorText() const { return m_error.value(); }

	void retranslate() override;

	void setRootPath(const QString &strPath);
	void setShowFiles(bool bShow);
	void setShowHidden(bool bShow);
	void setNameFilters(const QStringList &filters);
	void setSearchText(const QString &strText);

	Q_INVOKABLE void refresh();
	Q_INVOKABLE QString nameAt(int iRow) const;
	Q_INVOKABLE QString pathAt(int iRow) const;
	Q_INVOKABLE bool isDirAt(int iRow) const;
	Q_INVOKABLE bool makeDirectory(const QString &strName);

signals:
	void rootPathChanged();
	void showFilesChanged();
	void showHiddenChanged();
	void nameFiltersChanged();
	void searchTextChanged();
	void countChanged();
	void errorTextChanged();

private:
	void Reload();
	void ApplySearch();
	void SetError(TranslatableText::Factory factory);
	void ClearError();

	QString m_strRoot;
	bool m_bShowFiles;
	bool m_bShowHidden;
	QStringList m_filters;
	QString m_strSearch;
	QList<DirEntry> m_scanned;
	QList<DirEntry> m_entries;
	TranslatableText m_error;
};

}

#endif
