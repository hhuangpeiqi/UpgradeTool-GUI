#ifndef GUI_CONFIG_STORE_H
#define GUI_CONFIG_STORE_H

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace gui {

class ConfigStore : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
	explicit ConfigStore(QObject *parent = NULL);

	QString filePath() const { return m_strFile; }
	QString status() const { return m_strStatus; }

	Q_INVOKABLE QVariantMap load();
	Q_INVOKABLE bool save(const QVariantMap &values);
	Q_INVOKABLE void installUdev();
	Q_INVOKABLE bool udevRuleInstalled() const;
	Q_INVOKABLE QString udevCommand() const;

signals:
	void filePathChanged();
	void statusChanged();
	void udevInstallFinished(bool bOk, const QString &strMessage);

private:
	void SetStatus(const QString &strText);

	QString m_strFile;
	QString m_strStatus;
};

}

#endif
