#ifndef GUI_APP_CONTROLLER_H
#define GUI_APP_CONTROLLER_H

#include <QElapsedTimer>
#include <map>
#include "gui/backend/QualcommController.h"
#include "gui/backend/AllwinnerController.h"
#include "gui/backend/RockchipController.h"
#include "gui/backend/TaskController.h"
#include "gui/backend/DriverController.h"
#include "gui/backend/DeviceModel.h"
#include "gui/EdlRunner.h"
#include "gui/EngineWorker.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "gui/backend/ConfigStore.h"
#include "gui/backend/FileLocations.h"
#include "gui/backend/ImageEntryModel.h"
#include "gui/backend/KeyValueModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/backend/LogModel.h"
#include "gui/backend/Retranslatable.h"
#include "gui/backend/ThemeManager.h"

class QThread;
class QTimer;
namespace platform { class FileLog; }

namespace gui {

class AdbRunner;
class InspectionQueue;
class EngineWorker;
class QtObserver;

class AppController : public QObject, public Retranslatable
{
	Q_OBJECT

    Q_PROPERTY(gui::DeviceModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(gui::TaskController *tasks READ tasks CONSTANT)
    Q_PROPERTY(gui::DriverController *drivers READ drivers CONSTANT)
    Q_PROPERTY(bool interactionLocked READ interactionLocked NOTIFY interactionLockedChanged)
    Q_PROPERTY(gui::RockchipController *rockchip READ rockchip CONSTANT)
    Q_PROPERTY(gui::AllwinnerController *allwinner READ allwinner CONSTANT)
    Q_PROPERTY(gui::QualcommController *qualcomm READ qualcomm CONSTANT)
    Q_PROPERTY(gui::EdlRunner *edl READ edl CONSTANT)
    Q_PROPERTY(QVariantList availableVendors READ availableVendors CONSTANT)
    Q_PROPERTY(int vendor READ vendor WRITE setVendor NOTIFY vendorChanged)
	Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
	Q_PROPERTY(QStringList deviceLabels READ deviceLabels NOTIFY devicesChanged)
	Q_PROPERTY(QString deviceStatusText READ deviceStatusText NOTIFY devicesChanged)
	Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY devicesChanged)
    Q_PROPERTY(bool hasFlashTarget READ hasFlashTarget NOTIFY devicesChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap rockchipMaskromEligibility READ rockchipMaskromEligibility NOTIFY devicesChanged)
	Q_PROPERTY(QVariantMap qualcommEligibility READ qualcommEligibility NOTIFY devicesChanged)
	Q_PROPERTY(int deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY deviceIndexChanged)

	Q_PROPERTY(bool permissionBlocked READ permissionBlocked NOTIFY permissionChanged)

	Q_PROPERTY(int adbDeviceCount READ adbDeviceCount NOTIFY adbStateChanged)
	Q_PROPERTY(bool adbReady READ adbReady NOTIFY adbStateChanged)
	Q_PROPERTY(QString felSwitchHint READ felSwitchHint NOTIFY felSwitchStateChanged)
	Q_PROPERTY(bool felSwitchError READ felSwitchError NOTIFY felSwitchStateChanged)
	Q_PROPERTY(bool switching READ switching NOTIFY switchingChanged)

	Q_PROPERTY(bool batchMode READ batchMode NOTIFY batchModeChanged)
	Q_PROPERTY(int batchSelected READ batchSelected NOTIFY batchModeChanged)

	Q_PROPERTY(gui::LogModel *log READ log CONSTANT)
	Q_PROPERTY(gui::ConfigStore *config READ config CONSTANT)
	Q_PROPERTY(gui::ThemeManager *theme READ theme CONSTANT)
	Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY shuttingDownChanged)
	Q_PROPERTY(gui::LanguageManager *language READ language CONSTANT)



	Q_PROPERTY(QVariantList standardPlaces READ standardPlaces NOTIFY standardPlacesChanged)

public:
	explicit AppController(platform::FileLog *pLog, QObject *parent = NULL);
    AppController(platform::FileLog *pLog, EdlHooks edlHooks, int edlWaitMs, QObject *parent = nullptr);
    AppController(platform::FileLog *pLog, EdlHooks edlHooks, int edlWaitMs, QObject *parent, DriverHooks driverHooks, bool driversAvailable);
	~AppController();

	// Rebuilds every cached translated string when the UI language changes.
	// Driven by LanguageManager's registry, not by per-feature refresh calls.
	void retranslate() override;

    DeviceModel *deviceModel() { return m_deviceModel; }
    TaskController *tasks() { return m_tasks; }
    DriverController *drivers() { return m_drivers; }
    bool interactionLocked() const { return m_shuttingDown || m_tasks->busy() || m_drivers->busy(); }
    RockchipController *rockchip() { return m_rockchip; }
    AllwinnerController *allwinner() { return m_allwinner; }
    QualcommController *qualcomm() { return m_qualcomm; }
    EdlRunner *edl() { return m_edl; }
    Q_INVOKABLE void refreshEdl();
    Q_INVOKABLE void requestEdlSwitch();
    Q_INVOKABLE void switchToEdl(const QString &target, int method, const QString &endpoint);

    QVariantList availableVendors() const;
    int vendor() const { return m_vendor; }
    void setVendor(int);

	QVariantList devices() const { return m_deviceModel->rows(); }
	QStringList deviceLabels() const;
	QString deviceStatusText() const { return m_strDeviceStatus; }
	bool hasDevice() const { return !m_deviceModel->rows().isEmpty(); }
	bool hasFlashTarget() const;
    QString connectionState() const;
    QVariantMap qualcommEligibility() const;
    QVariantMap rockchipMaskromEligibility() const;

	int deviceIndex() const { return m_iDeviceIndex; }
	void setDeviceIndex(int iIndex);

	bool permissionBlocked() const { return m_bPermissionBlocked && !m_bPermissionIgnored; }

	int adbDeviceCount() const { return m_strAdbDevices.size(); }
	bool adbReady() const { return m_bAdbAvailable
	                                && !m_strAdbDevices.isEmpty(); }

	QString felSwitchHint() const { return m_felSwitchHint.value(); }
	bool felSwitchError() const { return m_felSwitchError; }
	bool switching() const { return m_bSwitching; }
	bool batchMode() const { return m_bBatchMode; }
	int batchSelected() const { return m_batchKeys.size(); }

	gui::LogModel *log() const { return m_pLog; }
	gui::ConfigStore *config() const { return m_pConfig; }
	gui::ThemeManager *theme() const { return m_pTheme; }
	gui::LanguageManager *language() const { return m_pLanguage; }




	Q_INVOKABLE QString toolVersion() const;
	Q_INVOKABLE QString logDir() const;
	Q_INVOKABLE void openLogDir();
	Q_INVOKABLE QString homeDir() const;
	QVariantList standardPlaces() const { return m_standardPlaces.value(); }
	Q_INVOKABLE void refreshStandardPlaces();
	Q_INVOKABLE bool directoryAvailable(const QString &path) const { return m_fileLocations.directoryAvailable(path); }
	Q_INVOKABLE void logLine(const QString &strText);
	Q_INVOKABLE QString lastDir(const QString &strKey) const;
	Q_INVOKABLE void rememberPath(const QString &strKey, const QString &strPath);
	Q_INVOKABLE QString dirOf(const QString &strPath) const;
	Q_INVOKABLE QString toLocalPath(const QString &strUrl) const;
	Q_INVOKABLE QString pathUrl(const QString &strPath) const;

	Q_INVOKABLE void refresh();
	Q_INVOKABLE void cancel();
	Q_INVOKABLE void requestQuit();
	Q_INVOKABLE void confirmQuit(bool confirmed);
	bool shuttingDown() const { return m_shuttingDown; }

	Q_INVOKABLE void retryPermission();
	Q_INVOKABLE void ignorePermission();
	Q_INVOKABLE void setBatchTargets(const QStringList &keys, bool bContinueOnError);
	Q_INVOKABLE void clearBatch();
	Q_INVOKABLE void answerStorage(int iIndex);



	Q_INVOKABLE void switchToLoader();
	Q_INVOKABLE void switchToFel(int mode = 0);
	Q_INVOKABLE void chooseAdbDevice(const QString &strSerial);
	Q_INVOKABLE void cancelAdbChoice();


signals:
    void interactionLockedChanged();
	void quitConfirmationRequested();
	void shuttingDownChanged();
	void shutdownReady();
    void vendorChanged();
	void requestScan();
	void requestExecute(const core::TaskJob &job);

	void devicesChanged();
	void deviceIndexChanged();
	void permissionChanged();
	void adbStateChanged();
	void felSwitchStateChanged();
	void switchingChanged();
	void batchModeChanged();
	void standardPlacesChanged();
	void storageVolumesChanged();

	void toastRequested(const QString &strText, bool bError);
	void storageSelectionRequested(const QStringList &strOptions, int iCurrent);
	void adbDeviceChoiceRequested(const QStringList &strDevices);

private:
    void UpdateInteractionLock();
    void BeginDriverMaintenance();
    void TryDriverInstall();
    DriverController *m_drivers = nullptr;
    EdlRunner *m_edl = nullptr;
    EdlEndpoint m_pendingEdl;
    int m_vendor = 0;
    QualcommController *m_qualcomm;
    platform::FileLog *m_diskLog = nullptr;
    AllwinnerController *m_allwinner;
    RockchipController *m_rockchip;
    TaskController *m_tasks;
    DeviceModel *m_deviceModel;
    std::map<int,QString> m_vendorSelection;
    bool m_qualcommManualSelection = false;
    std::map<int,QStringList> m_vendorBatch;
    std::map<int,bool> m_vendorContinue;
private slots:
	void onThreadStarted();
	void onDevicesReady(const QVariantList &devices);
	void onOutput(int iLevel, const QString &strText);
	void onUpgradePrompt(int prompt);
	void onStorageSelection(const QStringList &strOptions, int iCurrent);
	void onOperationResult(const core::OperationResult &result);
	void onTaskFinished(const core::TaskSummary &summary);
	void onStepStarted(int step, int target, int total, const QString &device);
	void onAdbFinished(bool bOk, const QString &strMessage);
	void onAdbDeviceChoice(const QStringList &strDevices);
	void onAdbDevicesChanged(const QStringList &strDevices);
	void onAdbAvailabilityChanged(bool bAvailable, const QString &strMessage);

private:
	void BeginShutdown();
	void CheckShutdown();
	QTimer *m_scanTimer = nullptr;
	QTimer *m_shutdownTimer = nullptr;
	bool m_quitConfirmationPending = false;
	bool m_shuttingDown = false;
	bool m_shutdownReady = false;
	void SetDeviceStatus();
	void AppendLog(const QString &strText, bool bError);
	void StartJob(core::TaskJob job);
	void BeginModeSwitch(const QString &message);
	void UpdateModeSwitch(const QString &message);
	void FinishModeSwitch(bool ok, const QString &message);
	QVariantList BuildStandardPlaces() const;
	QVariantList BatchTargets() const;
	bool PermissionMissing() const;

	EngineWorker *m_pWorker;
	QtObserver *m_pObserver;
	QThread *m_pThread;
	AdbRunner *m_pAdb;
    InspectionQueue *m_inspections;
	ConfigStore *m_pConfig;
	ThemeManager *m_pTheme;
	LanguageManager *m_pLanguage;

	LogModel *m_pLog;

	Translatable<QVariantList> m_standardPlaces;
	FileLocations m_fileLocations;
	QString m_strDeviceStatus;
	int m_iDeviceIndex;

	bool m_bPermissionBlocked;
	bool m_bPermissionIgnored;

	bool m_bBatchMode;
	QStringList m_batchKeys;
	bool m_bBatchContinue;


	bool m_bSwitching;
	bool m_switchingFel = false;
	TranslatableText m_felSwitchHint;
	bool m_felSwitchError = false;

	QStringList m_strAdbDevices;
	bool m_bAdbAvailable;
	bool m_adbOutageNotified = false;
};

}

#endif
