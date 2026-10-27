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
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool uiLogDetails READ uiLogDetails NOTIFY uiLogDetailsChanged)

public:
	explicit ConfigStore(QObject *parent = NULL);

	QString status() const { return m_status.value(); }
	bool uiLogDetails() const { return m_uiLogDetails; }

	void retranslate() override;

    void setUsbVendor(const QString &vendor) { m_usbVendor = vendor; }
	Q_INVOKABLE QVariantMap load();
	Q_INVOKABLE bool save(const QVariantMap &values);
	Q_INVOKABLE void installUdev();
	Q_INVOKABLE bool udevRuleInstalled() const;
	Q_INVOKABLE QString udevCommand() const;

signals:
	void statusChanged();
	void uiLogDetailsChanged();
	void udevInstallFinished(bool bOk, const QString &strMessage);

private:
	void SetStatus(TranslatableText::Factory factory);

    bool m_uiLogDetails = false;
    QString m_usbVendor = "2207";
	TranslatableText m_status;
};

}

#endif
