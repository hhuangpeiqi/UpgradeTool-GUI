#ifndef GUI_APP_CONTROLLER_H
#define GUI_APP_CONTROLLER_H

#include <QElapsedTimer>
#include "gui/EngineWorker.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "gui/backend/ConfigStore.h"
#include "gui/backend/ImageEntryModel.h"
#include "gui/backend/KeyValueModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/backend/LogModel.h"
#include "gui/backend/ThemeManager.h"

class QThread;
class CRKLog;

namespace gui {

class AdbRunner;
class EngineWorker;
class QtObserver;

class AppController : public QObject
{
	Q_OBJECT

    Q_PROPERTY(QVariantList allwinnerStates READ allwinnerStates NOTIFY allwinnerChanged)
    Q_PROPERTY(int vendor READ vendor WRITE setVendor NOTIFY vendorChanged)
    Q_PROPERTY(QString allwinnerFirmware READ allwinnerFirmware WRITE setAllwinnerFirmware NOTIFY allwinnerChanged)
    Q_PROPERTY(QVariantList allwinnerPartitions READ allwinnerPartitions NOTIFY allwinnerChanged)
    Q_PROPERTY(QVariantList allwinnerFiles READ allwinnerFiles NOTIFY allwinnerChanged)
    Q_PROPERTY(bool allwinnerValid READ allwinnerValid NOTIFY allwinnerChanged)
    Q_PROPERTY(gui::KeyValueModel *allwinnerResults READ allwinnerResults CONSTANT)
	Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
	Q_PROPERTY(QStringList deviceLabels READ deviceLabels NOTIFY devicesChanged)
	Q_PROPERTY(QString deviceStatusText READ deviceStatusText NOTIFY devicesChanged)
	Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY devicesChanged)
	Q_PROPERTY(int deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY deviceIndexChanged)

	Q_PROPERTY(bool permissionBlocked READ permissionBlocked NOTIFY permissionChanged)

	Q_PROPERTY(bool adbAvailable READ adbAvailable NOTIFY adbStateChanged)
	Q_PROPERTY(int adbDeviceCount READ adbDeviceCount NOTIFY adbStateChanged)
	Q_PROPERTY(bool adbReady READ adbReady NOTIFY adbStateChanged)
	Q_PROPERTY(QString felSwitchHint READ felSwitchHint NOTIFY felSwitchStateChanged)
	Q_PROPERTY(bool felSwitchError READ felSwitchError NOTIFY felSwitchStateChanged)
	Q_PROPERTY(bool switching READ switching NOTIFY switchingChanged)

	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(int progressValue READ progressValue NOTIFY progressChanged)
	Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
	Q_PROPERTY(QString progressSpeed READ progressSpeed NOTIFY progressChanged)
	Q_PROPERTY(bool batchMode READ batchMode NOTIFY batchModeChanged)
	Q_PROPERTY(int batchSelected READ batchSelected NOTIFY batchModeChanged)

	Q_PROPERTY(gui::LogModel *log READ log CONSTANT)
	Q_PROPERTY(gui::ImageEntryModel *imageEntries READ imageEntries CONSTANT)
	Q_PROPERTY(gui::KeyValueModel *firmwareEntries READ firmwareEntries CONSTANT)
	Q_PROPERTY(gui::KeyValueModel *results READ results CONSTANT)
	Q_PROPERTY(gui::ConfigStore *config READ config CONSTANT)
	Q_PROPERTY(gui::ThemeManager *theme READ theme CONSTANT)
	Q_PROPERTY(gui::LanguageManager *language READ language CONSTANT)

	Q_PROPERTY(QString imageDir READ imageDir NOTIFY imageStateChanged)
	Q_PROPERTY(QString imageHint READ imageHint NOTIFY imageStateChanged)
	Q_PROPERTY(bool imageHintError READ imageHintError NOTIFY imageStateChanged)
	Q_PROPERTY(bool imageQueueActive READ imageQueueActive NOTIFY imageStateChanged)

	Q_PROPERTY(QString firmwarePath READ firmwarePath NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareHint READ firmwareHint NOTIFY firmwareStateChanged)
	Q_PROPERTY(bool firmwareHintError READ firmwareHintError NOTIFY firmwareStateChanged)
	Q_PROPERTY(bool firmwareValid READ firmwareValid NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareLoaderVersion READ firmwareLoaderVersion NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareChip READ firmwareChip NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareBuildTime READ firmwareBuildTime NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareSign READ firmwareSign NOTIFY firmwareStateChanged)

	Q_PROPERTY(QString advancedHint READ advancedHint NOTIFY advancedStateChanged)
	Q_PROPERTY(bool advancedHintError READ advancedHintError NOTIFY advancedStateChanged)
	Q_PROPERTY(QStringList storages READ storages CONSTANT)
	Q_PROPERTY(QVariantList usageHelp READ usageHelp NOTIFY usageHelpChanged)

public:
	explicit AppController(CRKLog *pLog, QObject *parent = NULL);
	~AppController();

    int vendor() const { return m_vendor; }
    void setVendor(int);
    QString allwinnerFirmware() const { return m_allwinner[1].firmware; }
    void setAllwinnerFirmware(const QString &);
    QVariantList allwinnerPartitions() const { return m_allwinner[1].partitions; }
    QVariantList allwinnerFiles() const { return m_allwinner[1].files; }
    bool allwinnerValid() const { return m_allwinner[1].valid; }
    KeyValueModel *allwinnerResults() const { return m_allwinner[1].summary; }
    QVariantList allwinnerStates() const;
    Q_INVOKABLE void setAllwinnerContextFirmware(int context, const QString &path);
    Q_INVOKABLE void ensureAllwinnerContext(int context);
    Q_INVOKABLE void inspectAllwinnerFirmware(int context);
    Q_INVOKABLE void runAllwinner(int operation, const QVariantMap &parameters = QVariantMap());
    Q_INVOKABLE QVariantMap allwinnerSettings() const;
    Q_INVOKABLE void saveAllwinnerSettings(const QVariantMap &);

	QVariantList devices() const { return m_devices; }
	QStringList deviceLabels() const;
	QString deviceStatusText() const { return m_strDeviceStatus; }
	bool hasDevice() const { return !m_devices.isEmpty(); }

	int deviceIndex() const { return m_iDeviceIndex; }
	void setDeviceIndex(int iIndex);

	bool permissionBlocked() const { return m_bPermissionBlocked && !m_bPermissionIgnored; }

	bool adbAvailable() const { return m_bAdbAvailable; }
	int adbDeviceCount() const { return m_strAdbDevices.size(); }
	bool adbReady() const { return m_bAdbKnown && m_bAdbAvailable
	                                && !m_strAdbDevices.isEmpty(); }

	QString felSwitchHint() const { return m_felSwitchHint; }
	bool felSwitchError() const { return m_felSwitchError; }
	bool switching() const { return m_bSwitching; }
	bool busy() const { return m_bBusy; }
	int progressValue() const { return m_iProgressValue; }
	QString progressText() const { return m_strProgressText; }
	QString progressSpeed() const { return m_strProgressSpeed; }
	bool batchMode() const { return m_bBatchMode; }
	int batchSelected() const { return m_batchKeys.size(); }

	gui::LogModel *log() const { return m_pLog; }
	gui::ImageEntryModel *imageEntries() const { return m_pImageEntries; }
	gui::KeyValueModel *firmwareEntries() const { return m_pFirmwareEntries; }
	gui::KeyValueModel *results() const { return m_pResults; }
	gui::ConfigStore *config() const { return m_pConfig; }
	gui::ThemeManager *theme() const { return m_pTheme; }
	gui::LanguageManager *language() const { return m_pLanguage; }

	QString imageDir() const { return m_strImageDir; }
	QString imageHint() const { return m_strImageHint; }
	bool imageHintError() const { return m_bImageHintError; }
	bool imageQueueActive() const { return m_bQueueActive; }

	QString firmwarePath() const { return m_strFirmware; }
	QString firmwareHint() const { return m_strFirmwareHint; }
	bool firmwareHintError() const { return m_bFirmwareHintError; }
	bool firmwareValid() const { return m_bFirmwareValid; }
	QString firmwareVersion() const { return m_strFwVersion; }
	QString firmwareLoaderVersion() const { return m_strFwLoader; }
	QString firmwareChip() const { return m_strFwChip; }
	QString firmwareBuildTime() const { return m_strFwBuild; }
	QString firmwareSign() const { return m_strFwSign; }

	QString advancedHint() const { return m_strAdvancedHint; }
	bool advancedHintError() const { return m_bAdvancedHintError; }
	QStringList storages() const;

	Q_INVOKABLE QString toolVersion() const;
	Q_INVOKABLE QString logDir() const;
	Q_INVOKABLE QString homeDir() const;
	Q_INVOKABLE QVariantList standardPlaces() const;
	QVariantList usageHelp() const;
	Q_INVOKABLE void logLine(const QString &strText);
	Q_INVOKABLE QString lastDir(const QString &strKey) const;
	Q_INVOKABLE void rememberPath(const QString &strKey, const QString &strPath);
	Q_INVOKABLE QString dirOf(const QString &strPath) const;
	Q_INVOKABLE QString toLocalPath(const QString &strUrl) const;
	Q_INVOKABLE QString pathUrl(const QString &strPath) const;

	Q_INVOKABLE void refresh();
	Q_INVOKABLE void cancel();

	Q_INVOKABLE void retryPermission();
	Q_INVOKABLE void ignorePermission();
	Q_INVOKABLE void runOperation(int operation, const QVariantMap &parameters = QVariantMap());
	Q_INVOKABLE void setBatchTargets(const QStringList &keys, bool bContinueOnError);
	Q_INVOKABLE void clearBatch();
	Q_INVOKABLE void answerStorage(int iIndex);

	Q_INVOKABLE void setImageDir(const QString &strDir);
	Q_INVOKABLE void clearImage();
	Q_INVOKABLE void executeImageDownload(bool bReboot);

	Q_INVOKABLE void setFirmware(const QString &strPath);
	Q_INVOKABLE void clearFirmware();
	Q_INVOKABLE void executeFirmwareUpgrade(bool bReboot);
	Q_INVOKABLE void extractFirmware(const QString &strDir);
	Q_INVOKABLE void showFirmwareInfo();

	Q_INVOKABLE void switchToLoader();
	Q_INVOKABLE void switchToFel(int mode = 0);
	Q_INVOKABLE void chooseAdbDevice(const QString &strSerial);
	Q_INVOKABLE void cancelAdbChoice();

	Q_INVOKABLE void exportComLog(const QString &strFile);
	Q_INVOKABLE void exportImage(const QString &strBegin, const QString &strCount,
	                             const QString &strFile);
	Q_INVOKABLE void createGpt(const QString &strInput, const QString &strOutput);

signals:
    void vendorChanged();
    void allwinnerChanged();
	void requestScan();
	void requestExecute(const core::TaskJob &job);
	void requestAbort();

	void devicesChanged();
	void deviceIndexChanged();
	void permissionChanged();
	void adbStateChanged();
	void felSwitchStateChanged();
	void switchingChanged();
	void busyChanged();
	void progressChanged();
	void batchModeChanged();
	void imageStateChanged();
	void firmwareStateChanged();
	void advancedStateChanged();
	void usageHelpChanged();

	void toastRequested(const QString &strText, bool bError);
	void storageSelectionRequested(const QStringList &strOptions, int iCurrent);
	void adbDeviceChoiceRequested(const QStringList &strDevices);

private:
    int m_vendor = 0;
    CRKLog *m_diskLog = nullptr;
    QVariantList m_allDevices;
    struct AllwinnerState {
        QString firmware, hint;
        QVariantList partitions, files;
        bool valid = false, inspected = false, error = false;
        KeyValueModel *summary = nullptr;
        KeyValueModel *results = nullptr;
    };
    // Contexts: 0 raw boot package, 1 firmware flashing, 2 offline tools, 3 device info.
    AllwinnerState m_allwinner[4];
    int m_allwinnerContext = 1;
    QString m_vendorSelection[2];
    QStringList m_vendorBatch[2];
    bool m_vendorContinue[2] = { false, false };
private slots:
	void onThreadStarted();
	void onDevicesReady(const QVariantList &devices);
	void onOutput(int iLevel, const QString &strText);
	void onProgress(const QString &strPrompt, long long llTotal,
	                long long llCurrent, int iCall);
	void onUpgradePrompt(int prompt);
	void onStorageSelection(const QStringList &strOptions, int iCurrent);
	void onOperationResult(const core::OperationResult &result);
	void onTaskFinished(const core::TaskSummary &summary);
	void onStepStarted(int step, int target, int total, const QString &device);
	void onAdbFinished(bool bOk, const QString &strMessage);
	void onAdbDeviceChoice(const QStringList &strDevices);
	void onAdbDevicesChanged(const QStringList &strDevices);
	void onAdbAvailabilityChanged(bool bAvailable, const QString &strMessage);
	void onLanguageChanged();

private:
	void SetBusy(bool bBusy);
	void SetDeviceStatus();
	void SetProgress(int iPct, const QString &strText);
	void ResetProgress();
	void AppendLog(const QString &strText, bool bError);
	void StartJob(core::TaskJob job);
	void SetImageHint(const QString &strText, bool bError);
	void SetFirmwareHint(const QString &strText, bool bError);
	void SetAdvancedHint(const QString &strText, bool bError);
	void RebuildUsageHelp();
	void ApplyAllwinnerInspectResult(int context, const core::OperationResult &result);
	QVariantList BatchTargets() const;
	bool PermissionMissing() const;

	EngineWorker *m_pWorker;
	QtObserver *m_pObserver;
	QThread *m_pThread;
	AdbRunner *m_pAdb;
	ConfigStore *m_pConfig;
	ThemeManager *m_pTheme;
	LanguageManager *m_pLanguage;

	LogModel *m_pLog;
	ImageEntryModel *m_pImageEntries;
	KeyValueModel *m_pFirmwareEntries;
	KeyValueModel *m_pResults;

	QVariantList m_devices;
	QVariantList m_usageHelp;
	QString m_strDeviceStatus;
	QStringList m_deviceKeys;
	QString m_strDeviceSignature;
	int m_iDeviceIndex;

	bool m_bPermissionBlocked;
	bool m_bPermissionIgnored;

	bool m_bBusy;
	bool m_bCancelRequested;
	int m_iProgressValue;
	QString m_strProgressText;
	QString m_strProgressSpeed;
	core::Operation m_runningOperation = core::Operation::Count;
	core::TaskJob m_job;
	bool m_bBatchMode;
	QStringList m_batchKeys;
	bool m_bBatchContinue;

	QElapsedTimer m_progressTimer;
	QString m_strProgressStep;
	long long m_llProgressBase;

	QString m_strImageDir;
	QString m_strImageHint;
	bool m_bImageHintError;
	bool m_bQueueActive;

	QString m_strFirmware;
	QString m_strFirmwareHint;
	bool m_bFirmwareHintError;
	bool m_bFirmwareValid;
	QString m_strFwVersion;
	QString m_strFwLoader;
	QString m_strFwChip;
	QString m_strFwBuild;
	QString m_strFwSign;
	bool m_bFirmwareReboot;

	QString m_strAdvancedHint;
	bool m_bAdvancedHintError;

	bool m_bSwitching;
	bool m_switchingFel = false;
	QString m_felSwitchHint;
	bool m_felSwitchError = false;

	QStringList m_strAdbDevices;
	bool m_bAdbKnown;
	bool m_bAdbAvailable;
	bool m_bAdbWarned;
};

}

#endif
