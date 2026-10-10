#ifndef GUI_LANGUAGE_MANAGER_H
#define GUI_LANGUAGE_MANAGER_H

#include <QObject>
#include <QString>
#include <QTranslator>

namespace gui {

class LanguageManager : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
	Q_PROPERTY(QString current READ current NOTIFY languageChanged)

public:
	explicit LanguageManager(QObject *parent = NULL);

	QString mode() const { return m_strMode; }
	void setMode(const QString &strMode);

	// Effective language code currently applied ("zh_CN" or "en").
	QString current() const { return m_strEffective; }

signals:
	void modeChanged();
	void languageChanged();

private:
	static QString NormalizeMode(const QString &strMode);
	QString Effective() const;
	void Apply();

	QString m_strMode;
	QString m_strEffective;
	QTranslator m_appTranslator;
	QTranslator m_qtTranslator;
	bool m_bAppLoaded;
	bool m_bQtLoaded;
};

}

#endif
