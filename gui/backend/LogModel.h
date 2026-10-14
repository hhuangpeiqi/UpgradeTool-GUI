#ifndef GUI_LOG_MODEL_H
#define GUI_LOG_MODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace gui {

class LogModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
	enum Roles {
		TextRole = Qt::UserRole + 1,
		ErrorRole,
		ProgressRole
	};

	explicit LogModel(QObject *parent = NULL);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int iRole) const override;
	QHash<int, QByteArray> roleNames() const override;

	int count() const { return m_lines.size(); }

	void Append(const QString &strText, bool bError);
	Q_INVOKABLE void clear();

signals:
	void countChanged();

private:
	struct Line {
		QString text;
		bool error;
		bool progress;
	};

	QList<Line> m_lines;
};

}

#endif
