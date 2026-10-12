#ifndef GUI_CONFIG_STORE_H
#define GUI_CONFIG_STORE_H

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "gui/backend/Retranslatable.h"

namespace gui {

class ConfigStore : public QObject, public Retranslatable
{
	Q_OBJECT
	Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
	explicit ConfigStore(QObject *parent = NULL);

	QString filePath() const { return m_strFile; }
	QString status() const { return m_status.value(); }

	void retranslate() override;

    void setUsbVendor(const QString &vendor) { m_usbVendor = vendor; }
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
	void SetStatus(TranslatableText::Factory factory);

    QString m_usbVendor = "2207";
	QString m_strFile;
	TranslatableText m_status;
};

}

#endif
