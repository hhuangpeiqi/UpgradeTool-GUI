#ifndef GUI_CONFIG_STORE_H
#define GUI_CONFIG_STORE_H

#include <QObject>
#include <future>
#include "platform/LogService.h"
#include <QString>
#include <QVariantMap>

#include "gui/backend/Retranslatable.h"

namespace gui {

class ConfigStore : public QObject, public Retranslatable
{
	Q_OBJECT
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool uiLogDetails READ uiLogDetails NOTIFY uiLogDetailsChanged)

    Q_PROPERTY(int logTotalMiB READ logTotalMiB NOTIFY logStorageChanged)
    Q_PROPERTY(qulonglong logBytes READ logBytes NOTIFY logStorageChanged)
    Q_PROPERTY(bool logBusy READ logBusy NOTIFY logStorageChanged)
    Q_PROPERTY(QString logError READ logError NOTIFY logStorageChanged)
public:
	explicit ConfigStore(QObject *parent = NULL);

	QString status() const { return m_status.value(); }
	bool uiLogDetails() const { return m_uiLogDetails; }

    void waitForLogOperations() { if (m_logFuture.valid()) m_logFuture.wait(); }
    void attachLog(platform::LogService *log);
    int logTotalMiB() const { return m_logTotalMiB; }
    qulonglong logBytes() const { return m_logBytes; }
    bool logBusy() const { return m_logBusy; }
    QString logError() const { return m_logError; }
    Q_INVOKABLE void refreshLogStorage();
    Q_INVOKABLE void clearLogFiles();
	void retranslate() override;

	Q_INVOKABLE QVariantMap load();
	Q_INVOKABLE bool save(const QVariantMap &values);
	Q_INVOKABLE void installUdev();
	Q_INVOKABLE bool udevRuleInstalled() const;
	Q_INVOKABLE QString udevCommand() const;

signals:
    void logStorageChanged();
	void statusChanged();
	void uiLogDetailsChanged();
	void udevInstallFinished(bool bOk, const QString &strMessage);

private:
    void runLogOperation(bool clear, bool apply = false);
    platform::LogLimits limits() const;
    platform::LogService *m_logService = nullptr;
    int m_logFileMiB = 5, m_logTotalMiB = 50;
    qulonglong m_logBytes = 0;
    bool m_logBusy = false;
    QString m_logError;
    std::future<void> m_logFuture;

	void SetStatus(TranslatableText::Factory factory);

    bool m_uiLogDetails = false;
	TranslatableText m_status;
};

}

#endif
