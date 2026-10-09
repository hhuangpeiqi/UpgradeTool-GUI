#ifndef GUI_THEME_MANAGER_H
#define GUI_THEME_MANAGER_H

#include <QObject>
#include <QString>

namespace gui {

class ThemeManager : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
	Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)

public:
	explicit ThemeManager(QObject *parent = NULL);

	QString mode() const { return m_strMode; }
	void setMode(const QString &strMode);

	bool dark() const { return m_bDark; }

signals:
	void modeChanged();
	void darkChanged();

private slots:
	void onSystemSchemeChanged();

private:
	static QString NormalizeMode(const QString &strMode);
	void Recompute();

	QString m_strMode;
	bool m_bDark;
};

}

#endif
