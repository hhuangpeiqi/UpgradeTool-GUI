#include "gui/InspectionQueue.h"
#include "gui/UsbAccess.h"
#include "gui/UpgradePromptMessage.h"
#include "gui/DiagnosticLog.h"
#include <QSysInfo>
#include "gui/EdlDeviceSelection.h"
#include "gui/backend/AppController.h"
#include "gui/MessageDisplay.h"
#include "application/BuildFeatures.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cstring>
#include <algorithm>
#include <utility>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#include "gui/AdbRunner.h"
#include "gui/OperationSpec.h"
#include "gui/EngineWorker.h"
#include "gui/QtObserver.h"
#include "gui/backend/ConfigStore.h"
#include "gui/backend/ImageEntryModel.h"
#include "gui/backend/KeyValueModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/backend/LogModel.h"
#include "gui/backend/ThemeManager.h"
#include "domain/UpgradeObserver.h"
#include "application/Version.h"
#include "backends/qualcomm/OperationPolicy.h"
#include "platform/PlatformPath.h"
#include "platform/LogService.h"

namespace gui {

namespace {

const int kScanIntervalMs = 1000;

bool QualcommSimulation(const core::TaskJob &job)
{
    if (job.steps.empty()) return false;
    const auto &r = job.steps.front();
    return core::IsSimulation(r);
}

}

AppController::AppController(platform::LogService *pLog, QObject *parent)
    : AppController(pLog, {}, 45000, parent) {}

AppController::AppController(platform::LogService *pLog, EdlHooks edlHooks, int edlWaitMs, QObject *parent)
    : AppController(pLog, std::move(edlHooks), edlWaitMs, parent, DriverHooks{}, DriverController::hostAvailable()) {}

AppController::AppController(platform::LogService *pLog, EdlHooks edlHooks, int edlWaitMs, QObject *parent, DriverHooks driverHooks, bool driversAvailable)
	: QObject(parent),
      m_ownedLog(pLog ? nullptr : std::make_unique<platform::LogService>()),
      m_logService(pLog ? pLog : m_ownedLog.get()),
	  m_pWorker(new EngineWorker(m_logService)),
	  m_pObserver(new QtObserver(*m_logService, this)),
	  m_pThread(new QThread(this)),
	  m_pAdb(new AdbRunner(this)),
	  m_pConfig(new ConfigStore(this)),
	  m_pTheme(new ThemeManager(this)),
	  m_pLanguage(new LanguageManager(this)),
	  m_pLog(new LogModel(this)),
	  m_iDeviceIndex(-1),
	  m_bPermissionBlocked(false),
	  m_bPermissionIgnored(false),
	  m_bBatchMode(false),
	  m_bBatchContinue(true),
	  m_bSwitching(false),
	  m_bAdbAvailable(false)
{
    m_pConfig->attachLog(m_logService);
    m_logService->setConsumer([model = m_pLog](const core::LogEntry &entry) {
        if (QThread::currentThread() == model->thread()) model->append(entry);
        else QMetaObject::invokeMethod(model, [model, entry] { model->append(entry); }, Qt::QueuedConnection);
    });
    m_inspections = new InspectionQueue(this, m_logService);
    m_deviceModel=new DeviceModel(this);
    m_tasks=new TaskController(this);
    m_drivers=new DriverController(std::move(driverHooks), driversAvailable, this);
    connect(m_tasks,&TaskController::busyChanged,this,[this] {
        UpdateInteractionLock();
    });
    m_rockchip=new RockchipController(this);
    connect(m_rockchip,&RockchipController::executeRequested,this,[this](const core::TaskJob &job) { StartJob(job); });
    connect(m_rockchip, &RockchipController::errorRequested, this, [this](const QString &message) { LogError(message, "Rockchip"); });
    connect(m_rockchip,&RockchipController::inspectRequested,this,[this](const core::InspectionRequest &request) { m_inspections->inspect(m_rockchip, request); });
    connect(m_inspections,&InspectionQueue::inspectionFinished,m_rockchip,&RockchipController::applyInspection);
    m_pLanguage->registerRetranslatable(m_rockchip);
    m_allwinner=new AllwinnerController(this);
    connect(m_allwinner,&AllwinnerController::executeRequested,this,[this](const core::TaskJob &job) { StartJob(job); });
    connect(m_allwinner, &AllwinnerController::errorRequested, this, [this](const QString &message) { LogError(message, "Allwinner"); });
    connect(m_allwinner,&AllwinnerController::inspectRequested,this,[this](const core::InspectionRequest &request) { m_inspections->inspect(m_allwinner, request); });
    connect(m_inspections,&InspectionQueue::inspectionFinished,m_allwinner,&AllwinnerController::applyInspection);
    m_pLanguage->registerRetranslatable(m_allwinner);
	m_pWorker->moveToThread(m_pThread);
	m_pWorker->SetObserver(m_pObserver);

	connect(m_pThread, SIGNAL(started()), this, SLOT(onThreadStarted()));
	connect(this, SIGNAL(requestScan()), m_pWorker, SLOT(scanDevices()));
	qRegisterMetaType<core::TaskJob>();
    qRegisterMetaType<gui::ModeSwitchFailure>();
	qRegisterMetaType<core::OperationResult>();
	qRegisterMetaType<core::TaskSummary>();
	connect(this, &AppController::requestExecute, m_pWorker, &EngineWorker::execute);

	connect(m_pThread, &QThread::finished, m_pWorker, &QObject::deleteLater);
	connect(m_pWorker, &EngineWorker::devicesReady, this, &AppController::onDevicesReady);
    connect(m_pAdb,&AdbRunner::felMonitorRequested,m_pWorker,&EngineWorker::prepareFelSwitch);
    connect(m_pAdb, &AdbRunner::felMonitorRequested, this, [this](const FelSwitchRequest &request) {
        if (m_switchingFel && request.target.known()) m_felSwitchLocation = request.target;
    });
    connect(m_pAdb,&AdbRunner::felMonitorCancelled,m_pWorker,&EngineWorker::cancelFelSwitch);
    connect(m_pWorker,&EngineWorker::felMonitorReady,m_pAdb,&AdbRunner::FelMonitorReady);
    connect(m_pWorker,&EngineWorker::felMonitorFinished,m_pAdb,&AdbRunner::FelMonitorFinished);
    connect(m_pAdb,&AdbRunner::felTargetReady,this,[this](const core::DeviceRef &target) {
        m_pendingFelKey=QString::fromStdString(target.key());
        onDevicesReady(m_deviceModel->allRows());
    });

	connect(m_pWorker, &EngineWorker::taskFinished, this, &AppController::onTaskFinished);
	connect(m_pWorker, &EngineWorker::stepStarted, this, &AppController::onStepStarted);
    m_qualcomm = new QualcommController(this, m_inspections);
    connect(m_pAdb->service(), &AdbService::snapshotChanged, this, [this] {
        SetDeviceStatus();
        refreshEdl();
    });
    m_edl = new EdlRunner(edlHooks.discover ? std::move(edlHooks) : DefaultEdlHooks(m_pAdb->service()), edlWaitMs, this, m_logService);
    m_edlHandoffTimer = new QTimer(this);
    m_edlHandoffTimer->setSingleShot(true);
    m_edlHandoffTimer->setInterval(std::clamp(edlWaitMs, 1, 15000));
    connect(m_edlHandoffTimer, &QTimer::timeout, this, [this] {
        if (!m_edlHandoff) return;
        int matches = 0;
        bool accessBlocked = false;
        for (const auto &entry : m_deviceModel->allRows()) {
            auto row = entry.toMap();
            const bool blocked = row.value("accessError").toInt() != 0;
            // Compare identity without the eligibility check; keep the actual
            // snapshot and its access error unchanged.
            row["accessError"] = 0;
            if (MatchesEdlDevice(*m_edlHandoff, row)) {
                ++matches;
                accessBlocked |= blocked;
            }
        }
        const auto failure = matches == 1 && accessBlocked
            ? ScanSwitchAccess(2, m_edlHandoff->instance, m_edlHandoff->location, m_edlHandoff->interfaceNumber)
            : ModeSwitchFailure::Generic;
        FinishEdlSwitch(false, tr("Device entered 9008 mode, but its download interface is not ready. Check the USB driver and refresh."),
                        false, failure);
    });
    connect(m_edl, &EdlRunner::errorOccurred, this, [this](const QString &message) {
        m_logService->write(core::LogLevel::Error, "Qualcomm", {{}, "Device mode switch rejected", {}}, true, message.toStdString());
    });
    connect(m_edl, &EdlRunner::started, this, [this] {
        if (m_shuttingDown || m_bSwitching) return;
        m_edlHandoff.reset();
        m_pAdb->service()->setPaused(true);
        BeginModeSwitch(tr("Switching…"));
    });
    connect(m_edl, &EdlRunner::messageChanged, this, [this] {
        if (m_edl->running()) {
            UpdateModeSwitch(m_edl->message());
        }
    });
    connect(m_edl, &EdlRunner::finished, this, [this](bool ok, const QString &message, const EdlEndpoint &endpoint, ModeSwitchFailure failure) {
        if (m_vendor != 2 || !m_bSwitching || m_edlHandoff) return;
        if (!ok) {
            FinishEdlSwitch(false, message, m_edl->state() == EdlRunner::Cancelled, failure);
            return;
        }
        if (m_shuttingDown) return;
        m_edlHandoff = endpoint;
        m_edlHandoffTimer->start();
        UpdateModeSwitch(tr("Waiting for the download device to become ready…"));
        core::ScopedLogContext logContext(m_modeLogContext);
        m_logService->write(core::LogLevel::Info, "Qualcomm",
            {"gui::AppController", "Waiting for the download device to become ready…", {}});
        // Ignore pre-switch snapshots. Only a fresh backend scan can bind the
        // download target and complete the user-visible switch.
        onDevicesReady({});
        emit requestScan();
    });
    connect(m_qualcomm, &QualcommController::executeRequested, this, [this](const core::OperationRequest &request) {
        if(m_vendor != 2 || interactionLocked()) return;
        core::TaskJob job; job.steps.push_back(request); StartJob(job);
    });
    connect(m_qualcomm, &QualcommController::errorRequested, this, [this](const QString &message) { LogError(message, "Qualcomm"); });
    connect(m_pObserver,&QtObserver::stageProgressReceived,m_tasks,&TaskController::onStageProgress);
    connect(m_pObserver,&QtObserver::progressReceived,m_tasks,&TaskController::onProgress);
	connect(m_pObserver, &QtObserver::upgradePromptReceived, this, &AppController::onUpgradePrompt);
	connect(m_pObserver, &QtObserver::storageSelectionRequested, this, &AppController::onStorageSelection);
	connect(m_pObserver, &QtObserver::resultReceived, this, &AppController::onOperationResult);

	connect(m_pAdb, &AdbRunner::finished, this, &AppController::onAdbFinished);
	connect(m_pAdb, &AdbRunner::switchProgress, this, [this](const core::Message &entry) {
        const auto message = Display(entry);
        core::ScopedLogContext logContext(m_modeLogContext);
        m_logService->write(core::LogLevel::Info, "ADB", entry);
        if (!m_bSwitching) return;
        UpdateModeSwitch(message);
        if (m_switchingFel) {
            m_felSwitchHint.Set([message] { return message; });
            m_felSwitchError = false;
            emit felSwitchStateChanged();
        }
	});
	connect(m_pAdb, SIGNAL(deviceChoiceRequested(QStringList)),
	        this, SLOT(onAdbDeviceChoice(QStringList)));
	connect(m_pAdb, SIGNAL(devicesChanged(QStringList)),
	        this, SLOT(onAdbDevicesChanged(QStringList)));
	connect(m_pAdb, SIGNAL(availabilityChanged(bool, QString)),
	        this, SLOT(onAdbAvailabilityChanged(bool, QString)));
	connect(m_pAdb, &AdbRunner::recoveryStateChanged, this, [this](bool, const QString &) {
		SetDeviceStatus();
	});
	connect(m_pAdb, &AdbRunner::diagnosticMessage, this, [this](const QString &message) {
		m_logService->write(core::LogLevel::Info, "ADB", {{}, message.toStdString(), {}});
	});
    connect(m_pAdb, &AdbRunner::traceRecord, this, [this](const QJsonObject &record) {
        WriteDiagnostic(*m_logService, "ADB", record);
    });

	connect(m_pConfig, &gui::ConfigStore::udevInstallFinished,
	        this, [this](bool, const QString &) { retryPermission(); });

	m_bPermissionBlocked = PermissionMissing();
	emit permissionChanged();
	SetDeviceStatus();

	// Single source of truth for retranslation: every cached string owner
	// registers here so LanguageManager refreshes them all on a language switch.
	m_fileLocations.refresh();
	m_standardPlaces.Set([this] { return BuildStandardPlaces(); });
	m_pLanguage->registerRetranslatable(this);
	m_pLanguage->registerRetranslatable(m_pConfig);
    m_pLanguage->registerRetranslatable(m_qualcomm);
    m_pLanguage->registerRetranslatable(m_drivers);
    connect(m_drivers, &DriverController::busyChanged, this, &AppController::UpdateInteractionLock);
    connect(m_drivers, &DriverController::maintenanceRequested, this, &AppController::BeginDriverMaintenance);
    connect(m_drivers, &DriverController::diagnostic, this, [this](const QString &message) {
        m_logService->write(core::LogLevel::Debug, "Driver", {{}, message.toStdString(), {}}, false);
    });
    connect(m_drivers, &DriverController::finished, this, [this](gui::DriverController::Outcome outcome, const QString &message) {
        if (m_shuttingDown) return;
        m_logService->write(outcome == DriverController::Outcome::Failed ? core::LogLevel::Error : core::LogLevel::Info,
                            "Driver", {{}, outcome == DriverController::Outcome::Failed ? "Driver installation failed" :
                                outcome == DriverController::Outcome::PendingReconnect ? "Driver installation awaiting reconnect" :
                                "Driver installation succeeded", {}}, true, message.toStdString());
        m_pAdb->Resume(); refresh();
    });

    m_logService->write(core::LogLevel::Info, "Application",
        {"gui::AppController", "Application started", {}});
    m_logService->writef(core::LogLevel::Debug, "Application", "Environment: app_version=%s gui_pid=%lld os=%s architecture=%s",
        TOOL_VERSION, static_cast<long long>(QCoreApplication::applicationPid()),
        QSysInfo::prettyProductName().toUtf8().constData(), QSysInfo::currentCpuArchitecture().toUtf8().constData());

    if(!availableVendors().contains(m_vendor)) setVendor(availableVendors().first().toInt());
    m_rockchip->setActive(m_vendor==0);
    if(availableVendors().contains(0)) m_rockchip->restore();
	m_pThread->start();

	m_scanTimer = new QTimer(this);
	connect(m_scanTimer, &QTimer::timeout, this, &AppController::refresh);
	m_scanTimer->start(kScanIntervalMs);
	m_shutdownTimer = new QTimer(this);
	m_shutdownTimer->setInterval(20);
	connect(m_shutdownTimer, &QTimer::timeout, this, &AppController::CheckShutdown);

	m_pAdb->Start();
}

AppController::~AppController()
{
    m_shuttingDown = true;
    m_edlHandoffTimer->stop();
	m_drivers->shutdown();
	m_scanTimer->stop();
	m_pAdb->Stop();
    m_rockchip->cancelInspections();
    m_allwinner->cancelInspections();
    m_qualcomm->cancelInspections();
    m_inspections->shutdown();
    delete m_inspections; m_inspections = nullptr;
    delete m_edl; m_edl = nullptr;
	m_tasks->job().cancelled->request_stop();
	m_pObserver->CancelSelection();
	m_pThread->quit();
	m_pThread->wait();
    m_pConfig->waitForLogOperations();
    m_logService->setConsumer({});
}

void AppController::UpdateInteractionLock()
{
    const bool locked = interactionLocked();
    m_allwinner->setLocked(locked);
    m_rockchip->setLocked(locked);
    m_qualcomm->setLocked(locked);
    m_drivers->setSuspended(locked);
    emit interactionLockedChanged();
    if (!locked && (m_vendor == 2 || (m_vendor == 1 && m_pendingFelLocation))) {
        const auto snapshot = m_deviceModel->allRows();
        onDevicesReady(snapshot);
    }
}

void AppController::BeginDriverMaintenance()
{
    m_pAdb->Stop();
    // A queued barrier drains earlier scans and inspections on the USB worker.
    QMetaObject::invokeMethod(m_pWorker, [this] {
        m_pWorker->releaseAllwinnerSessions();
        QMetaObject::invokeMethod(this, &AppController::TryDriverInstall, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void AppController::TryDriverInstall()
{
    if (m_shuttingDown || !m_drivers->busy()) return;
    if (!m_edl->stopped()) {
        QTimer::singleShot(25, this, &AppController::TryDriverInstall);
        return;
    }
    m_drivers->beginInstall();
}

void AppController::requestQuit()
{
	if (m_shuttingDown || m_quitConfirmationPending) return;
	m_logService->write(core::LogLevel::Info, "Application", {{}, "Application exit requested", {}}, false);
	if (interactionLocked() || m_bSwitching) {
		m_quitConfirmationPending = true;
		emit quitConfirmationRequested();
		return;
	}
	BeginShutdown();
}

void AppController::confirmQuit(bool confirmed)
{
	if (!m_quitConfirmationPending || m_shuttingDown) return;
	m_quitConfirmationPending = false;
	if (confirmed) BeginShutdown();
}

void AppController::BeginShutdown()
{
	m_shuttingDown = true;
    m_edlHandoff.reset();
    m_edlHandoffTimer->stop();
	emit shuttingDownChanged();
    UpdateInteractionLock();
	m_logService->write(core::LogLevel::Info, "Application", {{}, "Stopping polling and cancelling outstanding work", {}}, false);
	m_scanTimer->stop();
	m_rockchip->setLocked(true);
	m_allwinner->setLocked(true);
	m_qualcomm->setLocked(true);
	m_rockchip->cancelInspections();
	m_allwinner->cancelInspections();
    m_qualcomm->cancelInspections();
    m_inspections->shutdown();
	m_tasks->cancel();
	m_rockchip->setCancelled(true);
	m_pObserver->CancelSelection();
	m_edl->shutdown();
    m_drivers->shutdown();
	m_pAdb->Stop();
	// Leave the GUI event loop running while cooperative cancellation unwinds.
	// This also lets queued worker completion and QThread deletion run normally.
	m_pThread->quit();
	m_shutdownTimer->start();
}

void AppController::CheckShutdown()
{
	if (m_shutdownReady || !m_inspections->stopped() || !m_pThread->isFinished() || !m_edl->stopped() || !m_drivers->stopped()) return;
	m_shutdownReady = true;
	m_shutdownTimer->stop();
	m_logService->write(core::LogLevel::Info, "Application", {{}, "Worker threads stopped; application ready to exit", {}}, false);
	emit shutdownReady();
}

QString AppController::toolVersion() const
{
	return QGuiApplication::applicationDisplayName() + QStringLiteral(" v")
	       + QString::fromLatin1(TOOL_VERSION);
}

QString AppController::logDir() const
{
	return QString::fromStdString(platform::GetUserLogDir());
}

void AppController::LogError(const QString &message, const char *source)
{
    m_logService->write(core::LogLevel::Error, source,
        {{}, "Operation rejected: %1", {message.toStdString()}}, true, message.toStdString());
}

void AppController::openLogDir()
{
	const QString path = QDir(logDir()).absolutePath();
	if (!QDir().mkpath(path) || !QFileInfo(path).isDir()) {
		LogError(tr("Cannot create or access log directory: %1").arg(QDir::toNativeSeparators(path)));
		return;
	}
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
		LogError(tr("Cannot open log directory: %1").arg(QDir::toNativeSeparators(path)));
}

QString AppController::dirOf(const QString &strPath) const
{
	QFileInfo info(strPath);
	if (info.isDir())
		return info.absoluteFilePath();
	return info.absolutePath();
}

QString AppController::toLocalPath(const QString &strUrl) const
{
	return QUrl(strUrl).toLocalFile();
}

QString AppController::pathUrl(const QString &strPath) const
{
	return QUrl::fromLocalFile(strPath).toString();
}

QString AppController::homeDir() const
{
	return QDir::homePath();
}

void AppController::refreshStandardPlaces()
{
	const bool volumesChanged = m_fileLocations.refresh();
	const QVariantList previous = standardPlaces();
	m_standardPlaces.retranslate();
	if (previous != standardPlaces()) emit standardPlacesChanged();
	if (volumesChanged) emit storageVolumesChanged();
}

QVariantList AppController::BuildStandardPlaces() const
{
	QVariantList places;
	QStringList seen;

	auto addPlace = [&places, &seen](const QString &strName, const QString &strPath) {
		if (strPath.isEmpty())
			return;
		QString strClean = FileLocations::normalizePath(strPath);
		const auto sensitivity = FileLocations::hostPlatform() == FileLocations::Platform::Windows
			? Qt::CaseInsensitive : Qt::CaseSensitive;
		if (strClean.isEmpty() || seen.contains(strClean, sensitivity))
			return;
		if (!QFileInfo(strClean).isDir())
			return;
		seen.append(strClean);
		QVariantMap map;
		map.insert("name", strName);
		map.insert("path", strClean);
		places.append(map);
	};

	addPlace(tr("Home"), QDir::homePath());
	addPlace(tr("Desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
	addPlace(tr("Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	addPlace(tr("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
	addPlace(tr("Pictures"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
	if (FileLocations::hostPlatform() != FileLocations::Platform::Windows)
		addPlace(tr("Root"), QStringLiteral("/"));
	for (const auto &place : m_fileLocations.places()) {
		const auto map = place.toMap();
		addPlace(map.value("name").toString(), map.value("path").toString());
	}

	return places;
}

QString AppController::lastDir(const QString &strKey) const
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope,
	                   "upgrade_tool", "upgrade_tool");
	return settings.value("paths/" + strKey).toString();
}

void AppController::rememberPath(const QString &strKey, const QString &strPath)
{
	QString strTrimmed = strPath.trimmed();
	if (strTrimmed.isEmpty())
		return;
	QFileInfo info(strTrimmed);
	QString strDir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
	if (strDir.isEmpty())
		return;
	QSettings settings(QSettings::IniFormat, QSettings::UserScope,
	                   "upgrade_tool", "upgrade_tool");
	settings.setValue("paths/" + strKey, strDir);
}

QStringList AppController::deviceLabels() const
{
	QStringList list;
	for (int i = 0; i < m_deviceModel->rows().size(); i++) {
		QVariantMap map = m_deviceModel->rows()[i].toMap();
		QString strLabel = QString("#%1 [%2] %3")
			.arg(map["index"].toInt())
			.arg(map["mode"].toString())
			.arg(map["key"].toString());
		// A Qualcomm device can re-enumerate into another mode (DIAG 0x9091 <->
		// EDL 0x9008) on the same USB port, keeping mode and key; show the
		// identity up front so the switch is visible even when the label is
		// elided.
		if (map.value("vendor", 0).toInt() == 2) {
			const QString strIdentity = QString("%1:%2")
				.arg(map["vid"].toUInt(), 4, 16, QChar('0'))
				.arg(map["pid"].toUInt(), 4, 16, QChar('0'));
			const QString strSerial = map["serial"].toString();
			strLabel = QString("#%1 %2%3 [%4] %5")
				.arg(map["index"].toInt())
				.arg(strIdentity)
				.arg(strSerial.isEmpty() ? QString() : QStringLiteral(" SN:") + strSerial)
				.arg(map["mode"].toString())
				.arg(map["key"].toString());
		}
        if(map.value("vendor").toInt()==1 && map.value("accessError").toInt())
            strLabel+=tr(" — unavailable (%1)").arg(map.value("accessError").toInt());
		list << strLabel;
	}
	return list;
}

QVariantMap AppController::rockchipMaskromEligibility() const
{
    QVariantList targets;
    QString reason;
    if (m_vendor == 0) {
        if (m_bBatchMode) targets = BatchTargets();
        else if (m_iDeviceIndex >= 0 && m_iDeviceIndex < m_deviceModel->rows().size())
            targets.append(m_deviceModel->rows()[m_iDeviceIndex]);
    }
    if (targets.isEmpty()) reason = tr("Select a Loader device first");
    else if (m_bBatchMode && targets.size() != m_batchKeys.size())
        reason = tr("A selected device is no longer available; refresh and select the targets again");
    for (const auto &target : targets) {
        if (!reason.isEmpty()) break;
        const auto row = target.toMap();
        if (row.value("vendor", 0).toInt() != 0 || row.value("usbType").toInt() != 2)
            reason = tr("All selected devices must be in Loader mode to enter Maskrom");
    }
    return {{"allowed", reason.isEmpty()}, {"reason", reason}};
}

QVariantMap AppController::qualcommEligibility() const
{
    QVariantList targets;
    if(m_vendor == 2) {
        if(m_bBatchMode) targets=BatchTargets();
        else if(m_iDeviceIndex >= 0 && m_iDeviceIndex < m_deviceModel->rows().size()) targets.append(m_deviceModel->rows()[m_iDeviceIndex]);
    }
    QVariantMap result;
    for(int op=int(core::Operation::QualcommFlash);op<=int(core::Operation::QualcommProvision);++op) {
        QString reason;
        if(targets.isEmpty()) reason=tr("Select a device first");
        else if(m_bBatchMode && targets.size()!=m_batchKeys.size())
            reason=tr("A selected device is no longer available; refresh and select the targets again");
        for(const auto &target:targets) {
            if(!reason.isEmpty()) break;
            const auto map=target.toMap();
            const auto text=core::qualcomm::EligibilityReason(static_cast<core::Operation>(op),
                static_cast<qtq_mode>(map.value("qualcommMode",QTQ_UNKNOWN).toInt()),map.value("accessError").toInt());
            reason=QCoreApplication::translate("QualcommBackend",text.c_str());
            if(!reason.isEmpty()) break;
        }
        result[QString::number(op)]=QVariantMap{{"allowed",reason.isEmpty()},{"reason",reason}};
    }
    return result;
}

bool AppController::hasFlashTarget() const {
    if(m_vendor==1 && m_bBatchMode) {
        const auto targets=BatchTargets();
        if(targets.isEmpty() || targets.size()!=m_batchKeys.size()) return false;
        return std::all_of(targets.begin(),targets.end(),[](const auto &target) {
            return !target.toMap().value("accessError").toInt();
        });
    }
    if (m_iDeviceIndex < 0 || m_iDeviceIndex >= m_deviceModel->rows().size()) return false;
    const auto row = m_deviceModel->rows()[m_iDeviceIndex].toMap();
    return !row.value("accessError").toInt() && (m_vendor != 2 || row.value("qualcommMode").toInt() == QTQ_EDL);
}
QString AppController::connectionState() const {
    if (hasFlashTarget()) return QStringLiteral("ready");
    if (hasDevice()) return QStringLiteral("interface");
    if (adbReady()) return QStringLiteral("adb");
    if (!m_pAdb->service()->snapshot().isEmpty()) return QStringLiteral("adb-unavailable");
    if (!m_pAdb->RecoveryMessage().isEmpty()) return QStringLiteral("recovering");
    return QStringLiteral("none");
}

void AppController::SetDeviceStatus()
{
    m_allwinner->setHasDevice(m_vendor==1 ? hasFlashTarget() : hasDevice());
	if (m_bBatchMode && !m_batchKeys.isEmpty()) {
		m_strDeviceStatus = tr("Batch: %1 selected / %2 current")
		                    .arg(m_batchKeys.size())
		                    .arg(m_deviceModel->keys().size());
		emit devicesChanged();
		return;
	}

	if (m_vendor == 2) {
		QList<int> modes;
		QMap<int, int> counts;
		for (const auto &value : m_deviceModel->rows()) {
			const int mode = value.toMap().value("qualcommMode", QTQ_UNKNOWN).toInt();
			if (!counts.contains(mode)) modes.append(mode);
			++counts[mode];
		}
		QStringList parts;
		for (int mode : modes) {
			const QString label = QCoreApplication::translate("QualcommBackend",
				core::qualcomm::ModeLabel(static_cast<qtq_mode>(mode)));
			parts << tr("%1 *%2").arg(label).arg(counts[mode]);
		}
        if (!parts.isEmpty()) {
            m_strDeviceStatus = tr("Interfaces found: %1").arg(parts.join(tr(", ")));
            emit devicesChanged();
            return;
        }
	}

	// Aggregate by mode, preserving device discovery order.
	QStringList strModes;
	QMap<QString, int> mapCount;
	for (int i = 0; i < m_deviceModel->rows().size(); i++) {
		QString strMode = m_deviceModel->rows()[i].toMap()["mode"].toString();
		if (strMode.isEmpty())
			continue;
		if (!mapCount.contains(strMode))
			strModes << strMode;
		mapCount[strMode]++;
	}

	QStringList strParts;
	for (int i = 0; i < strModes.size(); i++) {
		int iCount = mapCount[strModes[i]];
		strParts << (iCount == 1
		             ? tr("Found %1 device").arg(strModes[i])
		             : tr("Found %1 %2 device(s)").arg(iCount).arg(strModes[i]));
	}

    if(m_vendor==1) for(const auto &value:m_deviceModel->rows()) {
        const auto row=value.toMap();
        if(row.value("accessError").toInt()) {
            strParts << tr("USB driver or permissions are not ready (%1).").arg(row.value("accessError").toInt());
            break;
        }
    }
	// No device in the current vendor's flashing mode; report the shared ADB status
	if (strParts.isEmpty() && m_bAdbAvailable && !m_strAdbDevices.isEmpty()) {
		strParts << (m_strAdbDevices.size() == 1
		             ? tr("ADB device found")
		             : tr("Found %1 ADB device(s)").arg(m_strAdbDevices.size()));
	}
    if (strParts.isEmpty() && !m_pAdb->service()->snapshot().isEmpty()) {
        strParts << tr("ADB device offline or unauthorized; check USB debugging");
    }
	if (strParts.isEmpty() && !m_pAdb->RecoveryMessage().isEmpty())
		strParts << m_pAdb->RecoveryMessage();

	m_strDeviceStatus = strParts.isEmpty() ? tr("No device found")
	                                       : strParts.join(tr(", "));
	emit devicesChanged();
}

void AppController::retranslate()
{
    m_edl->retranslate();
	// The single rebuild path: every cached tr() string in this controller is
	// regenerated here, then the matching NOTIFY signals are emitted so QML
	// reads the new text immediately.
	m_felSwitchHint.retranslate();
	m_standardPlaces.retranslate();

	SetDeviceStatus();

	emit felSwitchStateChanged();
	emit standardPlacesChanged();
}

void AppController::onThreadStarted()
{
	if (m_shuttingDown) return;
	refresh();
}

void AppController::onDevicesReady(const QVariantList &incoming)
{
    m_deviceModel->setSnapshot(incoming);
    // Keep the active target stable while an operation or driver task runs.
    // UpdateInteractionLock replays the latest snapshot when it finishes.
    if (m_vendor == 2 && interactionLocked() && !m_edlHandoff) return;
    if (m_vendor == 1 && m_pendingFelLocation && interactionLocked()) return;
    QVariantList devices;
    for (const auto &entry : incoming) {
        const auto row = entry.toMap();
        if (row.value("vendor", 0).toInt() != m_vendor) continue;
#ifdef Q_OS_WIN
        if (row.value("accessError").toInt()) {
            m_drivers->poll(true);
            if(m_vendor!=1) continue;
        }
#endif
        devices.append(entry);
    }
    const bool handingOff = m_vendor == 2 && m_edlHandoff.has_value();
    const int edlIndex = handingOff ? SelectEdlDevice(*m_edlHandoff, devices) : -1;

	QString strPrevious = m_vendorSelection[m_vendor];
	if (m_iDeviceIndex >= 0 && m_iDeviceIndex < m_deviceModel->keys().size())
		strPrevious = m_deviceModel->keys().value(m_iDeviceIndex);
    if(m_vendor==1 && !m_pendingFelKey.isEmpty()) strPrevious=m_pendingFelKey;
    // Remember automatic selections too, so later scans cannot silently switch
    // to another Qualcomm device after the original target disappears.
    m_vendorSelection[m_vendor] = strPrevious;

    const bool rowsChanged = devices != m_deviceModel->rows();
    m_deviceModel->setRows(devices);

	int iSelected = handingOff ? edlIndex : m_deviceModel->keys().indexOf(strPrevious);
    const bool recoveringFel = m_vendor == 1 && m_pendingFelLocation.has_value();
    if (recoveringFel) {
        // Driver installation can change the interface key. Follow the original
        // physical USB port, and never fall back to an unrelated device.
        int matches = 0;
        iSelected = -1;
        for (int i = 0; i < devices.size(); ++i) {
            const auto row = devices[i].toMap();
            std::vector<int> ports;
            for (const auto &port : row.value("ports").toList()) ports.push_back(port.toInt());
            const int bus = row.value("bus").toInt();
            const auto physical = row.value("physicalLocation").toString().toStdString();
            const platform::UsbLocation location{physical.empty() ? platform::UsbTopologyLocation(bus, ports) : physical,
                bus, ports};
            if (!m_pendingFelLocation->matches(location)) continue;
            ++matches;
            if (!row.value("accessError").toInt()) iSelected = i;
        }
        if (matches != 1) iSelected = -1;
    }
	if (!devices.isEmpty() && !handingOff && !recoveringFel) {
#ifdef Q_OS_WIN
        if (m_vendor == 2) {
            const bool canChoose = !m_bBatchMode && !m_qualcommManualSelection &&
                (strPrevious.isEmpty() || (iSelected >= 0 &&
                    devices[iSelected].toMap().value("qualcommMode").toInt() != QTQ_EDL));
            if (canChoose) {
                int edlIndex = -1, edlCount = 0;
                for (int i = 0; i < devices.size(); ++i) {
                    if (devices[i].toMap().value("qualcommMode").toInt() == QTQ_EDL) {
                        edlIndex = i;
                        ++edlCount;
                    }
                }
                if (edlCount == 1) iSelected = edlIndex;
                else if (edlCount == 0 && strPrevious.isEmpty()) iSelected = 0;
            }
        } else if (iSelected < 0 && (m_vendor!=1 || strPrevious.isEmpty())) iSelected = 0;
#else
        if (iSelected < 0 && ((m_vendor != 2 && m_vendor != 1) || strPrevious.isEmpty())) {
            iSelected = 0;
            if (m_vendor == 2) {
                for (int i = 0; i < devices.size(); ++i) {
                    const auto row = devices[i].toMap();
                    if (row.value("qualcommMode").toInt() == QTQ_EDL && !row.value("accessError").toInt()) {
                        iSelected = i; break;
                    }
                }
            }
        }
#endif
    }

    if (iSelected >= 0) {
        m_vendorSelection[m_vendor] = m_deviceModel->keys().value(iSelected);
        if(m_vendor==1) m_pendingFelKey.clear();
        if (recoveringFel) {
            m_pendingFelLocation.reset();
            if (m_bBatchMode) {
                m_bBatchMode = false;
                m_batchKeys.clear();
                emit batchModeChanged();
            }
        }
    }
    if (!rowsChanged && iSelected == m_iDeviceIndex && !handingOff) return;

	if (iSelected != m_iDeviceIndex) {
		m_iDeviceIndex = iSelected;
		emit deviceIndexChanged();
	}

	if (m_bBatchMode && m_vendor != 2) {
		QStringList strAlive;
		for (int i = 0; i < m_batchKeys.size(); i++)
			if (m_deviceModel->keys().contains(m_batchKeys[i]))
				strAlive << m_batchKeys[i];
		m_batchKeys = strAlive;
		bool bWas = m_bBatchMode;
		m_bBatchMode = (m_batchKeys.size() > 1);
		if (bWas != m_bBatchMode)
			emit batchModeChanged();
	}

	SetDeviceStatus();
    if (handingOff && iSelected >= 0) {
        // A mode switch is a single-target action, even if batch selection was
        // previously active. Eligibility must describe the adopted device.
        if (m_bBatchMode) {
            m_bBatchMode = false;
            m_batchKeys.clear();
            emit batchModeChanged();
            SetDeviceStatus();
        }
        FinishEdlSwitch(true, tr("Download device ready."));
    }
}

bool AppController::PermissionMissing() const
{
#ifdef Q_OS_LINUX
	if (geteuid() == 0)
		return false;
	return !m_pConfig->udevRuleInstalled();
#else
	return false;
#endif
}

void AppController::setDeviceIndex(int iIndex)
{
	if (interactionLocked()) return;
	if (iIndex < -1 || iIndex >= m_deviceModel->rows().size())
		return;
    m_pendingFelLocation.reset();
    if (m_vendor == 2) {
        m_qualcommManualSelection = true;
    }
	if (iIndex == m_iDeviceIndex && !m_bBatchMode)
		return;

	if (m_bBatchMode) {
		m_bBatchMode = false;
		m_batchKeys.clear();
		emit batchModeChanged();
	}
    m_pendingFelKey.clear();
	m_iDeviceIndex = iIndex;
    m_vendorSelection[m_vendor] = iIndex >= 0 ? m_deviceModel->keys().value(iIndex) : QString();
	emit deviceIndexChanged();

	SetDeviceStatus();
}

void AppController::refresh()
{
    if (m_edlHandoff && !m_shuttingDown) {
        emit requestScan();
        return;
    }
	if (interactionLocked()) return;
    m_drivers->poll();
	emit requestScan();
    refreshEdl();
}

void AppController::retryPermission()
{
    // Rechecks may close the startup prompt, but never reopen it.
	if (!permissionBlocked()) return;
	m_bPermissionBlocked = PermissionMissing();
	emit permissionChanged();
	emit requestScan();
}

void AppController::ignorePermission()
{
	if (m_bPermissionIgnored)
		return;
	m_bPermissionIgnored = true;
	emit permissionChanged();
}

void AppController::cancel()
{
	if (!m_tasks->busy())
		return;
	if (m_bSwitching) {
        if (m_edlHandoff) {
            FinishEdlSwitch(false, tr("Switch cancelled. A reboot request already sent may not be reversible."), true);
            return;
        }
        if (m_edl->running()) { m_edl->cancel(); return; }
		m_pAdb->CancelSwitch();
		return;
	}
	m_tasks->cancel(); m_rockchip->setCancelled(true);
	m_tasks->job().cancelled->request_stop();
	m_pObserver->CancelSelection();
	core::ScopedLogContext logContext(m_bSwitching ? m_modeLogContext : m_taskLogContext);
    m_logService->write(core::LogLevel::Info, {}, {"gui::AppController", "Cancelling…", {}});
}

QVariantList AppController::BatchTargets() const
{
	QVariantList targets;
	for (int i = 0; i < m_batchKeys.size(); i++) {
		int iPos = m_deviceModel->keys().indexOf(m_batchKeys[i]);
		if (iPos < 0)
			continue;
		targets.append(m_deviceModel->rows()[iPos].toMap());
	}
	return targets;
}

void AppController::StartJob(core::TaskJob job)
{
	if (interactionLocked() || job.steps.empty()) return;
    for (const auto &step : job.steps) {
        if (step.operation() == core::Operation::EnterMaskrom) {
            const auto eligibility = rockchipMaskromEligibility();
            if (!eligibility.value("allowed").toBool()) {
                LogError(eligibility.value("reason").toString());
                return;
            }
        }
    }
	const bool local = !core::RequiresEnumeratedDevice(job.steps.front());
	if (!local) {
		QVariantList targets;
		if (m_bBatchMode) targets = BatchTargets();
		else if (m_iDeviceIndex >= 0 && m_iDeviceIndex < m_deviceModel->rows().size()) targets.append(m_deviceModel->rows()[m_iDeviceIndex]);
		if (targets.isEmpty()) { LogError(tr("Select a device first")); return; }
        if(job.steps.front().vendor()==core::Vendor::Qualcomm) {
            if(m_bBatchMode && targets.size()!=m_batchKeys.size()) {
                LogError(tr("A selected device is no longer available; refresh and select the targets again")); return;
            }
            for(const auto &value:targets) {
                const auto map=value.toMap();
                for(const auto &step:job.steps) {
                    const auto reason=core::qualcomm::EligibilityReason(step.operation(),
                        static_cast<qtq_mode>(map.value("qualcommMode",QTQ_UNKNOWN).toInt()),map.value("accessError").toInt());
                    if(!reason.empty()) { LogError(QCoreApplication::translate("QualcommBackend",reason.c_str())); return; }
                }
            }
        }
        if(job.steps.front().vendor()==core::Vendor::Allwinner) {
            for(const auto &value:targets) {
                const auto row=value.toMap();
                if(row.value("accessError").toInt()) {
                    LogError(tr("The selected Allwinner device is not accessible; check its driver or USB permissions (%1).")
                        .arg(row.value("accessError").toInt())); return;
                }
            }
        }
        for(const auto &value:targets) {
            const auto reference=value.toMap().value("reference").value<core::DeviceRef>();
            if(!reference) { LogError(tr("A selected device is no longer available; refresh and select the targets again")); return; }
            job.targets.push_back(reference);
        }
	}
	job.continueOnError = m_bBatchMode && m_bBatchContinue;
    job.logId = platform::LogService::nextTaskId();
    m_taskLogContext = {job.steps.empty() ? "Application" : core::VendorLogSource(job.steps.front().vendor()), job.logId};
    m_tasks->start(job); m_rockchip->setCancelled(false);
	m_pObserver->BeginSelectionSession();

    core::ScopedLogContext logContext(m_taskLogContext);
    m_logService->write(core::LogLevel::Info, {},
        {"gui::AppController", "Start: %1", {OperationMessage(m_tasks->runningOperation()).English()}}, true,
        tr("Start: %1").arg(OperationName(m_tasks->runningOperation())).toStdString());
    if (QualcommSimulation(m_tasks->job())) {
        m_logService->write(core::LogLevel::Info, {}, {"gui::AppController", "Simulation only; no device will be flashed", {}});
        m_tasks->setProgress(-1, tr("Simulation"));
    }
#ifdef Q_OS_WIN
    if (!local && !QualcommSimulation(m_tasks->job())) m_tasks->setProgress(-1, tr("Checking device access…"));
#endif
    emit requestExecute(m_tasks->job());
}

void AppController::setBatchTargets(const QStringList &keys, bool bContinueOnError)
{
	if (interactionLocked()) return;
    m_pendingFelLocation.reset();
	QStringList strAlive;
	for (int i = 0; i < keys.size(); i++)
		if (m_deviceModel->keys().contains(keys[i]))
			strAlive << keys[i];

	m_bBatchContinue = bContinueOnError;

	if (strAlive.size() > 1) {
		m_batchKeys = strAlive;
		m_bBatchMode = true;
		m_iDeviceIndex = -1;
		emit deviceIndexChanged();

		emit batchModeChanged();
		SetDeviceStatus();
		return;
	}

	clearBatch();
	if (strAlive.size() == 1) {
		int iPos = m_deviceModel->keys().indexOf(strAlive[0]);
		if (iPos >= 0) {
			m_iDeviceIndex = iPos;
			emit deviceIndexChanged();

		}
		SetDeviceStatus();
	}
}

void AppController::clearBatch()
{
	if (interactionLocked()) return;
	if (!m_bBatchMode && m_batchKeys.isEmpty())
		return;
	m_bBatchMode = false;
	m_batchKeys.clear();
	emit batchModeChanged();
	SetDeviceStatus();
}

void AppController::onTaskFinished(const core::TaskSummary &summary)
{
	if (m_shuttingDown) return;
	const bool cancelled = summary.status == core::OperationStatus::Cancelled;
	const bool ok = summary.status == core::OperationStatus::Succeeded;
	m_tasks->setBusy(false);
	TranslatableText::Factory statusText = [cancelled, ok] {
		return cancelled ? tr("Cancelled") : ok ? tr("Operation complete") : tr("Operation failed; see the log");
	};
	QString text = statusText();
    if (QualcommSimulation(m_tasks->job())) text = tr("Simulation: %1").arg(text);
    core::ScopedLogContext logContext(m_taskLogContext);
    m_logService->write(!ok && !cancelled ? core::LogLevel::Error : core::LogLevel::Info, {},
        {"gui::AppController", cancelled ? "Cancelled" : ok ? "Operation complete" : "Operation failed; see the log", {}}, true, text.toStdString());
    if(!m_tasks->job().steps.empty() && m_tasks->job().steps.front().vendor()==core::Vendor::Rockchip) m_rockchip->applyFinished(summary,m_tasks->job());
	if (m_tasks->job().targets.size() > 1) {
		const int succeededCount = summary.succeeded;
		const int failedCount = summary.failed;
		TranslatableText::Factory batchText = [succeededCount, failedCount, cancelled] {
			return tr("Batch finished: %1 succeeded, %2 failed").arg(succeededCount).arg(failedCount)
			       + (cancelled ? tr(", remaining operations cancelled") : QString());
		};
        m_logService->write(summary.failed > 0 ? core::LogLevel::Error : core::LogLevel::Info, {},
            {"gui::AppController", "Batch finished: %1 succeeded, %2 failed",
             {std::to_string(summary.succeeded), std::to_string(summary.failed)}}, true, batchText().toStdString());

	}
	if (m_tasks->runningOperation() >= core::Operation::AllwinnerFlash && m_tasks->runningOperation() <= core::Operation::AllwinnerReboot) {
		m_allwinner->applyFinished(summary, m_tasks->job().targets.size());
	}
	if (m_vendor == 2) m_tasks->setSpeed({});
	m_tasks->setProgress(ok ? 100 : qMax(0, m_tasks->progressValue()), text);
    m_tasks->finish(); m_rockchip->setCancelled(false);
    m_drivers->refresh(true);
    if (!m_tasks->job().steps.empty() && m_tasks->job().steps.front().operation() == core::Operation::EnterMaskrom)
        refresh();
}

void AppController::onStepStarted(int step, int target, int total, const QString &device)
{
	m_tasks->resetProgress();
	if (step < static_cast<int>(m_tasks->job().steps.size())) {
		const auto &request = m_tasks->job().steps[step];
		QString label=OperationName(request.operation());
        if(request.vendor()==core::Vendor::Rockchip && !request.rockchip().partition.empty()) label=QString::fromStdString(request.rockchip().partition);
		const int stepCount = m_tasks->job().steps.size();
		TranslatableText::Factory runText = [step, stepCount, total, target, label, device] {
			QString text = tr("Running %1/%2: %3").arg(step + 1).arg(stepCount).arg(label);
			if (total > 1) text += tr(", device %1/%2 %3").arg(target + 1).arg(total).arg(device);
			return text;
		};
		m_rockchip->applyStep(runText);
	}
}

void AppController::onUpgradePrompt(int prompt)
{
	bool failed = false;
	QString text = Display(UpgradePromptMessage(prompt, failed));
    if (prompt == static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_START) &&
        m_tasks->busy() && m_tasks->runningOperation() == core::Operation::EnterMaskrom)
        m_tasks->setProgress(-1, text);
}

void AppController::onStorageSelection(const QStringList &strOptions, int iCurrent)
{
	if (m_tasks->busy() && !m_tasks->cancelRequested()) emit storageSelectionRequested(strOptions, iCurrent);
	else m_pObserver->SetSelectResult(0);
}

void AppController::answerStorage(int iIndex)
{
	m_pObserver->SetSelectResult(iIndex);
}

void AppController::onOperationResult(const core::OperationResult &result)
{
    if(result.operation >= core::Operation::QualcommFlash && result.operation <= core::Operation::QualcommProvision) {
        m_qualcomm->applyResult(result); return;
    }
    if(result.operation >= core::Operation::AllwinnerFlash && result.operation <= core::Operation::AllwinnerReboot) {
        m_allwinner->applyResult(result); return;
    }
    m_rockchip->applyResult(result);
}

void AppController::refreshEdl()
{
    if (!m_shuttingDown && m_vendor == 2 && !interactionLocked()) m_edl->refresh(m_pAdb->AdbPath());
}
void AppController::switchToEdl(const QString &target, int method, const QString &endpoint)
{
    if (!m_shuttingDown && m_vendor == 2 && !interactionLocked()) m_edl->start(target, method, endpoint, m_pAdb->AdbPath());
}
void AppController::requestEdlSwitch()
{
    if (!m_shuttingDown && m_vendor == 2 && !interactionLocked()) m_edl->requestSwitch(m_pAdb->AdbPath());
}

void AppController::FinishEdlSwitch(bool ok, const QString &message, bool cancelled, ModeSwitchFailure failure)
{
    if (m_vendor != 2 || !m_bSwitching) return;
    const bool wasHandingOff = m_edlHandoff.has_value();
    m_edlHandoff.reset();
    m_edlHandoffTimer->stop();
    // Failed handoffs require an explicit selection; normal automatic selection
    // must not replace the missing target with an unrelated EDL device.
    if (!ok && wasHandingOff) m_qualcommManualSelection = true;
    m_pAdb->service()->setPaused(false);
    FinishModeSwitch(ok, message, cancelled, failure);
    emit requestScan();
    refreshEdl();
}

void AppController::BeginModeSwitch(const QString &message)
{
    m_modeLogContext = {m_vendor == 2 ? "Qualcomm" : "ADB", platform::LogService::nextTaskId()};
    m_pAdb->service()->setLogContext(m_modeLogContext);
    m_edl->setLogContext(m_modeLogContext);
	m_bSwitching = true;
	m_tasks->resetProgress();
	m_tasks->setProgress(-1, message);
	m_tasks->setBusy(true);
	emit switchingChanged();
    core::ScopedLogContext logContext(m_modeLogContext);
    m_logService->write(core::LogLevel::Info, {}, {{}, "Device mode switch in progress", {}}, true, message.toStdString());
}

void AppController::UpdateModeSwitch(const QString &message)
{
	if (!m_bSwitching) return;
	m_tasks->setProgress(-1, message);
}

void AppController::FinishModeSwitch(bool ok, const QString &message, bool cancelled, ModeSwitchFailure failure)
{
    if (!m_bSwitching) return;
	m_tasks->setProgress(ok ? 100 : 0, message);
	m_bSwitching = false;
    m_pAdb->service()->setLogContext({});
	emit switchingChanged();
	m_tasks->setBusy(false);
    m_drivers->refresh(true);
    core::ScopedLogContext logContext(m_modeLogContext);
    m_logService->write(!ok && !cancelled ? core::LogLevel::Error : core::LogLevel::Info, {},
        {{}, cancelled ? "Device mode switch cancelled" : ok ? "Device mode switch succeeded" : "Device mode switch failed", {}},
        true, message.toStdString());
    // Access diagnostics belong to the switch result, independent of popup visibility.
    if (!ok && !cancelled && failure == ModeSwitchFailure::DeviceAccess) {
        WriteDiagnostic(*m_logService, m_modeLogContext.source.c_str(), QJsonObject{
            {"event", "switch_access_failed"}, {"log_task", qint64(m_modeLogContext.task)},
            {"reason", int(failure.reason)}, {"source_interface", failure.sourceInterface},
            {"instance", failure.instance}, {"message", message}, {"detail", failure.detail}});
    }
    if (!ok && !cancelled && !m_shuttingDown) {
        if (failure == ModeSwitchFailure::DeviceAccess) {
            emit switchAccessRequested(MakeSwitchAccessPrompt(failure, m_drivers->available()));
        }
        else emit switchRecoveryRequested();
    }
}

void AppController::switchToLoader()
{
	if (interactionLocked() || m_bSwitching)
		return;
	if (!adbReady()) {
		LogError(tr("No ADB device detected; cannot switch."));
		return;
	}
	m_switchingFel = false;
	BeginModeSwitch(tr("Switching…"));
	m_pAdb->SwitchToLoader();
}

void AppController::switchToFel(int mode)
{
	if (interactionLocked() || m_vendor != 1 || m_bSwitching) return;
	if (mode < 0 || mode > 1 || !adbReady()) {
		LogError(tr("No ADB device detected or the switch method is invalid."));
		return;
	}
    m_pendingFelKey.clear();
    m_pendingFelLocation.reset();
    m_felSwitchLocation.reset();
	m_switchingFel = true;
	m_felSwitchHint.Set([] { return tr("Preparing to switch ADB device to FEL mode…"); });
	m_felSwitchError = false;
	emit felSwitchStateChanged();
	BeginModeSwitch(m_felSwitchHint.value());
	m_pAdb->SwitchToFel(static_cast<AdbRunner::FelMethod>(mode));
}

void AppController::onAdbDeviceChoice(const QStringList &strDevices)
{
	emit adbDeviceChoiceRequested(strDevices);
}

void AppController::onAdbDevicesChanged(const QStringList &strDevices)
{
	m_strAdbDevices = strDevices;
	SetDeviceStatus();
	emit adbStateChanged();
}

void AppController::onAdbAvailabilityChanged(bool bAvailable, const QString &strMessage)
{
	m_bAdbAvailable = bAvailable;
	m_logService->writef(core::LogLevel::Info, "ADB", "ADB availability: available=%d", bAvailable);
	if (bAvailable) m_adbOutageNotified = false;
	if (!bAvailable && !strMessage.isEmpty() && !m_adbOutageNotified) {
		m_adbOutageNotified = true;
        m_logService->write(core::LogLevel::Error, "ADB", {{}, "ADB is unavailable; check the executable, server and device connection", {}}, true, strMessage.toStdString());
	}
	SetDeviceStatus();
	emit adbStateChanged();
}

void AppController::chooseAdbDevice(const QString &strSerial)
{
	m_pAdb->ChooseDevice(strSerial);
}

void AppController::cancelAdbChoice()
{
	m_pAdb->CancelSwitch();
}

void AppController::onAdbFinished(bool bOk, const QString &strMessage, bool cancelled, ModeSwitchFailure failure)
{
	if (!m_bSwitching) return;
	if (m_switchingFel) {
        if (!bOk && !cancelled && failure == ModeSwitchFailure::DeviceAccess)
            m_pendingFelLocation = m_felSwitchLocation;
        else
            m_pendingFelLocation.reset();
        m_felSwitchLocation.reset();
		m_switchingFel = false;
		m_felSwitchHint.Set([strMessage] { return strMessage; });
		m_felSwitchError = !bOk;
		emit felSwitchStateChanged();
	}
	FinishModeSwitch(bOk, strMessage, cancelled, failure);
	emit requestScan();
}

QVariantList AppController::availableVendors() const {
    QVariantList vendors;
#if UPGRADE_TOOL_ROCKCHIP
    vendors.append(0);
#endif
#if UPGRADE_TOOL_ALLWINNER
    vendors.append(1);
#endif
#if UPGRADE_TOOL_QUALCOMM
    vendors.append(2);
#endif
    return vendors;
}

void AppController::setVendor(int vendor)
{
    if(interactionLocked() || !availableVendors().contains(vendor) || vendor==m_vendor) return;
    if (m_iDeviceIndex >= 0)
        m_vendorSelection[m_vendor] = m_deviceModel->keys().value(m_iDeviceIndex);
    else if (m_vendor != 2)
        m_vendorSelection[m_vendor].clear();
    if(m_vendor==1) QMetaObject::invokeMethod(m_pWorker,&EngineWorker::releaseAllwinnerSessions,Qt::QueuedConnection);
    m_pendingFelKey.clear();
    m_pendingFelLocation.reset();
    m_felSwitchLocation.reset();
    m_vendorBatch[m_vendor] = m_batchKeys; m_vendorContinue[m_vendor] = m_bBatchContinue;
    m_vendor=vendor; m_drivers->setVendor(vendor); m_allwinner->setActive(vendor==1); m_rockchip->setActive(vendor==0);
    m_deviceModel->setRows({}); m_iDeviceIndex=-1;
    m_batchKeys=m_vendorBatch[m_vendor]; m_bBatchContinue=m_vendorContinue[m_vendor]; m_bBatchMode=m_batchKeys.size()>1;
    emit vendorChanged(); emit deviceIndexChanged(); emit batchModeChanged();
    onDevicesReady(m_deviceModel->allRows());
    // Empty snapshots skip onDevicesReady's update path, but a vendor change
    // still needs to rebuild the status and notify the device bindings.
    if (m_deviceModel->rows().isEmpty()) SetDeviceStatus();
    refresh();
}

}
