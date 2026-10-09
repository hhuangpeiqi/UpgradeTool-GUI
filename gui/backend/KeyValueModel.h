#ifndef GUI_KEY_VALUE_MODEL_H
#define GUI_KEY_VALUE_MODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>

namespace gui {

class KeyValueModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
	enum Roles {
		LabelRole = Qt::UserRole + 1,
		ValueRole
	};

	explicit KeyValueModel(QObject *parent = NULL);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int iRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	int count() const { return m_rows.size(); }

	void SetItems(const QVariantList &items);
	void Clear();

signals:
	void countChanged();

private:
	struct Row {
		QString label;
		QString value;
	};

	QList<Row> m_rows;
};

}

#endif
