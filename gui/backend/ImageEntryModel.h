#ifndef GUI_IMAGE_ENTRY_MODEL_H
#define GUI_IMAGE_ENTRY_MODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace gui {

struct ImageRow {
	QString storage;
	QString addr;
	QString name;
	QString path;
	bool selectable;
	bool checked;
	bool isLoader;
};

class ImageEntryModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(int checkableCount READ checkableCount NOTIFY countsChanged)
	Q_PROPERTY(int checkedCount READ checkedCount NOTIFY countsChanged)

public:
	enum Roles {
		NoRole = Qt::UserRole + 1,
		SelectableRole,
		CheckedRole,
		StorageRole,
		AddrRole,
		NameRole,
		PathRole,
		IsLoaderRole
	};

	explicit ImageEntryModel(QObject *parent = NULL);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int iRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	int count() const { return m_rows.size(); }
	int checkableCount() const;
	int checkedCount() const;

	const QList<ImageRow> &rows() const { return m_rows; }

	void SetRows(const QList<ImageRow> &rows);
	void Clear();

	Q_INVOKABLE void setChecked(int iRow, bool bChecked);
	Q_INVOKABLE void setPath(int iRow, const QString &strPath);
	Q_INVOKABLE void setAllChecked(bool bChecked);

signals:
	void countChanged();
	void countsChanged();

private:
	QList<ImageRow> m_rows;
};

}

#endif
