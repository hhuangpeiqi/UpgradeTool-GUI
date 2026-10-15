#include "gui/backend/AppController.h"
#include "application/BuildFeatures.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cstring>
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
#include "platform/FileLog.h"

namespace gui {

namespace {

const int kScanIntervalMs = 1000;

bool QualcommSimulation(const core::TaskJob &job)
{
    if (job.steps.empty()) return false;
    const auto &r = job.steps.front();
    return r.vendor() == core::Vendor::Qualcomm && (r.qualcomm().dryRun ||
        r.operation()== core::Operation::QualcommCreateDigests);
}


// The QUD backend reports the kernel handle ("\\.\COM5") while the pre-switch
// native enumeration reports the bare name ("COM5"). Compare the device itself,
// not its escaping, when matching the device that just entered 9008 mode.
QString DevicePathKey(const QString &path)
{
    QString key = path.trimmed();
    if (key.startsWith(QStringLiteral("\\\\.\\"))) key.remove(0, 4);
    return key.toUpper();
}

QString PromptText(int prompt, bool &failed)
{
	failed = false;
	switch (prompt) {
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_BOOT_START): return QObject::tr("Downloading Boot");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_BOOT_FAIL): failed = true; return QObject::tr("Download Boot failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_BOOT_SUCCESS): return QObject::tr("Download Boot succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_IDB_START): return QObject::tr("Downloading IDB");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_IDB_FAIL): failed = true; return QObject::tr("Download IDB failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_IDB_SUCCESS): return QObject::tr("Download IDB succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_FIRMWARE_START): return QObject::tr("Downloading firmware");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_FIRMWARE_FAIL): failed = true; return QObject::tr("Download firmware failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_DOWNLOAD_FIRMWARE_SUCCESS): return QObject::tr("Download firmware succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_TEST_DEVICE_START): return QObject::tr("Testing device");
	case static_cast<int>(core::UpgradePrompt::PROMPT_TEST_DEVICE_FAIL): failed = true; return QObject::tr("Test device failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_TEST_DEVICE_SUCCESS): return QObject::tr("Test device succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_RESET_DEVICE_START): return QObject::tr("Resetting device");
	case static_cast<int>(core::UpgradePrompt::PROMPT_RESET_DEVICE_FAIL): failed = true; return QObject::tr("Reset device failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_RESET_DEVICE_SUCCESS): return QObject::tr("Reset device succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MSC_START): return QObject::tr("Waiting for MSC mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MSC_FAIL): failed = true; return QObject::tr("Waiting for MSC mode failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MSC_SUCCESS): return QObject::tr("Entered MSC mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_LOADER_START): return QObject::tr("Waiting for Loader mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_LOADER_FAIL): failed = true; return QObject::tr("Waiting for Loader mode failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_LOADER_SUCCESS): return QObject::tr("Entered Loader mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_START): return QObject::tr("Waiting for Maskrom mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_FAIL): failed = true; return QObject::tr("Waiting for Maskrom mode failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_WAIT_MASKROM_SUCCESS): return QObject::tr("Entered Maskrom mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_IDB_START): return QObject::tr("Erasing IDB");
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_IDB_FAIL): failed = true; return QObject::tr("Erase IDB failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_IDB_SUCCESS): return QObject::tr("Erase IDB succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_SWITCH_MSC_START): return QObject::tr("Switching to MSC mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_SWITCH_MSC_FAIL): failed = true; return QObject::tr("Switch to MSC mode failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_SWITCH_MSC_SUCCESS): return QObject::tr("Switched to MSC mode");
	case static_cast<int>(core::UpgradePrompt::PROMPT_CHECK_CHIP_START): return QObject::tr("Checking chip");
	case static_cast<int>(core::UpgradePrompt::PROMPT_CHECK_CHIP_FAIL): failed = true; return QObject::tr("Check chip failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_CHECK_CHIP_SUCCESS): return QObject::tr("Check chip succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_PREPARE_IDB_START): return QObject::tr("Preparing IDB");
	case static_cast<int>(core::UpgradePrompt::PROMPT_PREPARE_IDB_FAIL): failed = true; return QObject::tr("Prepare IDB failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_PREPARE_IDB_SUCCESS): return QObject::tr("Prepare IDB done");
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_FLASH_START): return QObject::tr("Erasing Flash");
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_FLASH_FAIL): failed = true; return QObject::tr("Erase Flash failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_ERASE_FLASH_SUCCESS): return QObject::tr("Erase Flash succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_BLOCK_STATE_START): return QObject::tr("Reading block state");
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_BLOCK_STATE_FAIL): failed = true; return QObject::tr("Read block state failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_BLOCK_STATE_SUCCESS): return QObject::tr("Read block state succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_FLASH_INFO_START): return QObject::tr("Reading Flash info");
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_FLASH_INFO_FAIL): failed = true; return QObject::tr("Read Flash info failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_GET_FLASH_INFO_SUCCESS): return QObject::tr("Read Flash info succeeded");
	case static_cast<int>(core::UpgradePrompt::PROMPT_LOWER_FORMAT_START): return QObject::tr("Low-level formatting");
	case static_cast<int>(core::UpgradePrompt::PROMPT_LOWER_FORMAT_FAIL): failed = true; return QObject::tr("Low-level format failed");
	case static_cast<int>(core::UpgradePrompt::PROMPT_LOWER_FORMAT_SUCCESS): return QObject::tr("Low-level format succeeded");
	default: return QString();
	}
}

}

AppController::AppController(platform::FileLog *pLog, QObject *parent)
    : AppController(pLog, DefaultEdlHooks(), 45000, parent) {}

AppController::AppController(platform::FileLog *pLog, EdlHooks edlHooks, int edlWaitMs, QObject *parent)
	: QObject(parent),
	  m_pWorker(new EngineWorker(pLog)),
	  m_pObserver(new QtObserver(this)),
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
	  m_bAdbKnown(false),
	  m_bAdbAvailable(false),
	  m_bAdbWarned(false)
{
	m_diskLog = pLog;
    m_deviceModel=new DeviceModel(this);
    m_tasks=new TaskController(this);
    connect(m_tasks,&TaskController::busyChanged,this,[this] {
        const bool locked=m_tasks->busy(); m_allwinner->setLocked(locked); m_rockchip->setLocked(locked);
    });
    m_rockchip=new RockchipController(m_pConfig,this);
    connect(m_rockchip,&RockchipController::executeRequested,this,[this](const core::TaskJob &job) { StartJob(job); });
    connect(m_rockchip,&RockchipController::toastRequested,this,&AppController::toastRequested);
    connect(m_rockchip,&RockchipController::inspectRequested,m_pWorker,&EngineWorker::inspect);
    connect(m_pWorker,&EngineWorker::inspectionFinished,m_rockchip,&RockchipController::applyInspection);
    m_pLanguage->registerRetranslatable(m_rockchip);
    m_allwinner=new AllwinnerController(this);
    connect(m_allwinner,&AllwinnerController::executeRequested,this,[this](const core::TaskJob &job) { StartJob(job); });
    connect(m_allwinner,&AllwinnerController::toastRequested,this,&AppController::toastRequested);
    connect(m_allwinner,&AllwinnerController::inspectRequested,m_pWorker,&EngineWorker::inspect);
    connect(m_pWorker,&EngineWorker::inspectionFinished,m_allwinner,&AllwinnerController::applyInspection);
    m_pLanguage->registerRetranslatable(m_allwinner);
	m_pWorker->moveToThread(m_pThread);
	m_pWorker->SetObserver(m_pObserver);

	connect(m_pThread, SIGNAL(started()), this, SLOT(onThreadStarted()));
	connect(this, SIGNAL(requestScan()), m_pWorker, SLOT(scanDevices()));
	qRegisterMetaType<core::TaskJob>();
	qRegisterMetaType<core::OperationResult>();
	qRegisterMetaType<core::TaskSummary>();
	connect(this, &AppController::requestExecute, m_pWorker, &EngineWorker::execute);
	connect(m_pThread, &QThread::finished, m_pWorker, &QObject::deleteLater);
	connect(m_pWorker, &EngineWorker::devicesReady, this, &AppController::onDevicesReady);
	connect(m_pWorker, &EngineWorker::taskFinished, this, &AppController::onTaskFinished);
	connect(m_pWorker, &EngineWorker::stepStarted, this, &AppController::onStepStarted);
    m_qualcomm = new QualcommController(this);
    m_edl = new EdlRunner(std::move(edlHooks), edlWaitMs, this);
    connect(m_edl, &EdlRunner::errorOccurred, this, [this](const QString &message) {
        AppendLog(message, true); emit toastRequested(message, true);
    });
    connect(m_edl, &EdlRunner::started, this, [this] {
        BeginModeSwitch(tr("Switching…"));
    });
    connect(m_edl, &EdlRunner::messageChanged, this, [this] {
        if (m_edl->running()) {
            UpdateModeSwitch(m_edl->message());
        }
    });
    connect(m_edl, &EdlRunner::finished, this, [this](bool ok, const QString &message, const EdlEndpoint &endpoint) {
        m_pendingEdl = ok ? endpoint : EdlEndpoint();
        FinishModeSwitch(ok, message);
        emit requestScan(); refreshEdl();
    });
    connect(m_qualcomm, &QualcommController::executeRequested, this, [this](const core::OperationRequest &request) {
        if(m_vendor != 2 || m_tasks->busy()) return;
        core::TaskJob job; job.steps.push_back(request); StartJob(job);
    });
    connect(m_qualcomm, &QualcommController::errorRequested, this, [this](const QString &message) { emit toastRequested(message,true); });
    connect(m_tasks, &TaskController::busyChanged, this, [this] { m_qualcomm->setLocked(m_tasks->busy()); });
	connect(m_pObserver, &QtObserver::outputReceived, this, &AppController::onOutput);
    connect(m_pObserver,&QtObserver::stageProgressReceived,m_tasks,&TaskController::onStageProgress);
    connect(m_pObserver,&QtObserver::progressReceived,m_tasks,&TaskController::onProgress);
	connect(m_pObserver, &QtObserver::upgradePromptReceived, this, &AppController::onUpgradePrompt);
	connect(m_pObserver, &QtObserver::storageSelectionRequested, this, &AppController::onStorageSelection);
	connect(m_pObserver, &QtObserver::resultReceived, this, &AppController::onOperationResult);

	connect(m_pAdb, SIGNAL(finished(bool, QString)),
	        this, SLOT(onAdbFinished(bool, QString)));
	connect(m_pAdb, &AdbRunner::switchProgress, this, [this](const QString &message) {
		if (!m_switchingFel) return;
		UpdateModeSwitch(message);
		m_felSwitchHint.Set([message] { return message; });
		m_felSwitchError = false;
		emit felSwitchStateChanged();
	});
	connect(m_pAdb, SIGNAL(deviceChoiceRequested(QStringList)),
	        this, SLOT(onAdbDeviceChoice(QStringList)));
	connect(m_pAdb, SIGNAL(devicesChanged(QStringList)),
	        this, SLOT(onAdbDevicesChanged(QStringList)));
	connect(m_pAdb, SIGNAL(availabilityChanged(bool, QString)),
	        this, SLOT(onAdbAvailabilityChanged(bool, QString)));

	connect(m_pConfig, &gui::ConfigStore::udevInstallFinished,
	        this, [this](bool, const QString &) { retryPermission(); });

	m_bPermissionBlocked = PermissionMissing();
	emit permissionChanged();
	SetDeviceStatus();

	// Single source of truth for retranslation: every cached string owner
	// registers here so LanguageManager refreshes them all on a language switch.
	m_standardPlaces.Set([this] { return BuildStandardPlaces(); });
	m_pLanguage->registerRetranslatable(this);
	m_pLanguage->registerRetranslatable(m_pConfig);
    m_pLanguage->registerRetranslatable(m_qualcomm);



	{
		QString strStamp = QTime::currentTime().toString("HH:mm:ss");
		AppendLog(QString("[%1] %2").arg(strStamp).arg(tr("Application started")), false);
	}

    if(!availableVendors().contains(m_vendor)) setVendor(availableVendors().first().toInt());
    m_rockchip->setActive(m_vendor==0);
    if(availableVendors().contains(0)) m_rockchip->restore();
	m_pThread->start();

	QTimer *pTimer = new QTimer(this);
	connect(pTimer, SIGNAL(timeout()), this, SLOT(refresh()));
	pTimer->start(kScanIntervalMs);

	m_pAdb->Start();
}

AppController::~AppController()
{
    m_rockchip->cancelInspections();
    m_allwinner->cancelInspections();
    delete m_edl; m_edl = nullptr;
	m_tasks->job().cancelled->request_stop();
	m_pObserver->CancelSelection();
	m_pThread->quit();
	m_pThread->wait();
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

void AppController::logLine(const QString &strText)
{
	AppendLog(strText, false);
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

QVariantList AppController::BuildStandardPlaces() const
{
	QVariantList places;
	QStringList seen;

	auto addPlace = [&places, &seen](const QString &strName, const QString &strPath) {
		if (strPath.isEmpty())
			return;
		QString strClean = QDir::cleanPath(strPath);
		if (strClean.isEmpty() || seen.contains(strClean))
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
	addPlace(tr("Root"), QStringLiteral("/"));

	const QString strUser = QDir::home().dirName();
	QStringList mediaRoots;
	mediaRoots << QStringLiteral("/media/") + strUser
	           << QStringLiteral("/run/media/") + strUser
	           << QStringLiteral("/mnt");
	for (int i = 0; i < mediaRoots.size(); i++) {
		QDir dir(mediaRoots[i]);
		if (!dir.exists())
			continue;
		const QFileInfoList entries = dir.entryInfoList(
			QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
		for (int j = 0; j < entries.size(); j++)
			addPlace(entries[j].fileName(), entries[j].absoluteFilePath());
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
		list << strLabel;
	}
	return list;
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


void AppController::SetDeviceStatus()
{
    m_allwinner->setHasDevice(hasDevice());
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
		m_strDeviceStatus = parts.isEmpty() ? tr("No device found")
			: tr("Interfaces found: %1").arg(parts.join(tr(", ")));
		emit devicesChanged();
		return;
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

	// No device in the current vendor's flashing mode; report the shared ADB status
	if (strParts.isEmpty() && m_vendor != 2) {
		if (!m_bAdbKnown)
			strParts << tr("Detecting ADB…");
		else if (!m_bAdbAvailable)
			strParts << tr("No ADB environment detected");
		else if (!m_strAdbDevices.isEmpty())
			strParts << (m_strAdbDevices.size() == 1
			             ? tr("ADB device found")
			             : tr("Found %1 ADB device(s)").arg(m_strAdbDevices.size()));
	}

	m_strDeviceStatus = strParts.isEmpty() ? tr("No device found")
	                                       : strParts.join(tr(", "));
	emit devicesChanged();
}





void AppController::AppendLog(const QString &strText, bool bError)
{
	if (m_pLog)
		m_pLog->Append(strText, bError);
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
	refresh();
}

void AppController::onDevicesReady(const QVariantList &incoming)
{
    m_deviceModel->setSnapshot(incoming);
    QString edlKey;
    if (m_vendor == 2 && m_pendingEdl.mode() == QTQ_EDL) {
        QStringList matches;
        const auto &expected=m_pendingEdl;
        for (const auto &entry : incoming) {
            const auto d=entry.toMap();
            if(d.value("vendor").toInt()!=2 || d.value("qualcommMode").toInt()!=QTQ_EDL ||
                d.value("vid").toUInt()!=expected.vid || d.value("pid").toUInt()!=expected.pid) continue;
            QVector<int> ports; for(const auto &p:d.value("ports").toList()) ports.append(p.toInt());
            if(d.value("transport",-1).toInt()==int(core::QualcommTransport::Qud) && !expected.path.isEmpty()) {
                if(DevicePathKey(d.value("devicePath").toString())!=DevicePathKey(expected.path)) continue;
                if(!expected.instance.isEmpty()) {
                    if(d.value("deviceInstance").toString()!=expected.instance) continue;
                } else if(expected.serial.isEmpty() || d.value("serial").toString()!=expected.serial) continue;
            } else {
                if(expected.ports.isEmpty() || d.value("bus").toInt()!=expected.bus || ports!=expected.ports) continue;
                if(d.value("usbAddress").toUInt()!=expected.address ||
                    d.value("interfaceNumber",-1).toInt()!=expected.interfaceNumber ||
                    d.value("alternate").toInt()!=expected.alternate) continue;
                if(!expected.serial.isEmpty() && d.value("serial").toString()!=expected.serial) continue;
            }
            matches.append(d.value("key").toString());
        }
        if(matches.size()==1) edlKey=matches.first();
        if(!matches.isEmpty()) m_pendingEdl={};
    }
    QVariantList devices;
    for(const auto &d:incoming) if(d.toMap().value("vendor",0).toInt()==m_vendor) devices.append(d);
    // Compare complete snapshots, including mode, access and interface incarnation.
    if (devices == m_deviceModel->rows()) {
        if (!edlKey.isEmpty()) setDeviceIndex(m_deviceModel->keys().indexOf(edlKey));
        return;
    }

	QString strPrevious = m_vendorSelection[m_vendor];
	if (m_iDeviceIndex >= 0 && m_iDeviceIndex < m_deviceModel->keys().size())
		strPrevious = m_deviceModel->keys().value(m_iDeviceIndex);

    m_deviceModel->setRows(devices);

	int iSelected = m_deviceModel->keys().indexOf(edlKey.isEmpty() ? strPrevious : edlKey);
	if (iSelected < 0 && !devices.isEmpty() && (m_vendor != 2 || strPrevious.isEmpty()))
		iSelected = 0;

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
	if (m_tasks->busy()) return;
	if (iIndex < -1 || iIndex >= m_deviceModel->rows().size())
		return;
	if (iIndex == m_iDeviceIndex && !m_bBatchMode)
		return;

	if (m_bBatchMode) {
		m_bBatchMode = false;
		m_batchKeys.clear();
		emit batchModeChanged();
	}
	m_pendingEdl = {};
	m_iDeviceIndex = iIndex;
    m_vendorSelection[m_vendor] = iIndex >= 0 ? m_deviceModel->keys().value(iIndex) : QString();
	emit deviceIndexChanged();

	SetDeviceStatus();
}

void AppController::refresh()
{
	if (m_tasks->busy()) return;
	emit requestScan();
    refreshEdl();
}

void AppController::retryPermission()
{
	m_bPermissionIgnored = false;
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
        if (m_edl->running()) { m_edl->cancel(); return; }
		m_pAdb->CancelSwitch();
		return;
	}
	m_tasks->cancel(); m_rockchip->setCancelled(true);
	m_tasks->job().cancelled->request_stop();
	m_pObserver->CancelSelection();
	AppendLog(tr("Cancelling…"), false);
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
	if (m_tasks->busy() || job.steps.empty()) return;
	const bool local = !core::RequiresEnumeratedDevice(job.steps.front());
	if (!local) {
		QVariantList targets;
		if (m_bBatchMode) targets = BatchTargets();
		else if (m_iDeviceIndex >= 0 && m_iDeviceIndex < m_deviceModel->rows().size()) targets.append(m_deviceModel->rows()[m_iDeviceIndex]);
		if (targets.isEmpty()) { emit toastRequested(tr("Select a device first"), true); return; }
        if(job.steps.front().vendor()==core::Vendor::Qualcomm) {
            if(m_bBatchMode && targets.size()!=m_batchKeys.size()) {
                emit toastRequested(tr("A selected device is no longer available; refresh and select the targets again"),true); return;
            }
            for(const auto &value:targets) {
                const auto map=value.toMap();
                for(const auto &step:job.steps) {
                    const auto reason=core::qualcomm::EligibilityReason(step.operation(),
                        static_cast<qtq_mode>(map.value("qualcommMode",QTQ_UNKNOWN).toInt()),map.value("accessError").toInt());
                    if(!reason.empty()) { emit toastRequested(QCoreApplication::translate("QualcommBackend",reason.c_str()),true); return; }
                }
            }
        }
        for(const auto &value:targets) {
            const auto reference=value.toMap().value("reference").value<core::DeviceRef>();
            if(!reference) { emit toastRequested(tr("A selected device is no longer available; refresh and select the targets again"),true); return; }
            job.targets.push_back(reference);
        }
	}
    job.config=m_pConfig->snapshot();
	job.continueOnError = m_bBatchMode && m_bBatchContinue;
    m_tasks->start(job); m_rockchip->setCancelled(false);
	m_pObserver->BeginSelectionSession();

	AppendLog(tr("Start: %1").arg(OperationName(m_tasks->runningOperation())), false);
    if (QualcommSimulation(m_tasks->job())) {
        AppendLog(tr("Simulation only; no device will be flashed"), false);
        m_tasks->setProgress(-1, tr("Simulation"));
    }
	emit requestExecute(m_tasks->job());
}



void AppController::setBatchTargets(const QStringList &keys, bool bContinueOnError)
{
	if (m_tasks->busy()) return;
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
		emit toastRequested(tr("Batch mode: %1 selected").arg(strAlive.size()), false);
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
		emit toastRequested(tr("Single device selected"), false);
	} else {
		emit toastRequested(tr("No device selected"), false);
	}
}

void AppController::clearBatch()
{
	if (m_tasks->busy()) return;
	if (!m_bBatchMode && m_batchKeys.isEmpty())
		return;
	m_bBatchMode = false;
	m_batchKeys.clear();
	emit batchModeChanged();
	SetDeviceStatus();
}

void AppController::onTaskFinished(const core::TaskSummary &summary)
{
	const bool cancelled = summary.status == core::OperationStatus::Cancelled;
	const bool ok = summary.status == core::OperationStatus::Succeeded;
	m_tasks->setBusy(false);
	TranslatableText::Factory statusText = [cancelled, ok] {
		return cancelled ? tr("Cancelled") : ok ? tr("Operation complete") : tr("Operation failed; see the log");
	};
	QString text = statusText();
    if (QualcommSimulation(m_tasks->job())) text = tr("Simulation: %1").arg(text);
	AppendLog(text, !ok && !cancelled);
    if(!m_tasks->job().steps.empty() && m_tasks->job().steps.front().vendor()==core::Vendor::Rockchip) m_rockchip->applyFinished(summary,m_tasks->job());
	if (m_tasks->job().targets.size() > 1) {
		const int succeededCount = summary.succeeded;
		const int failedCount = summary.failed;
		TranslatableText::Factory batchText = [succeededCount, failedCount, cancelled] {
			return tr("Batch finished: %1 succeeded, %2 failed").arg(succeededCount).arg(failedCount)
			       + (cancelled ? tr(", remaining operations cancelled") : QString());
		};
		AppendLog(batchText(), summary.failed > 0);

	}
	if (m_tasks->runningOperation() >= core::Operation::AllwinnerFlash && m_tasks->runningOperation() <= core::Operation::AllwinnerReboot) {
		m_allwinner->applyFinished(summary, m_tasks->job().targets.size());
	}
	if (m_vendor == 2) m_tasks->setSpeed({});
	m_tasks->setProgress(ok ? 100 : qMax(0, m_tasks->progressValue()), text);
    m_tasks->finish(); m_rockchip->setCancelled(false);
	emit toastRequested(text, !ok && !cancelled);
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
		AppendLog(runText(), false);
	}
}

void AppController::onOutput(int iLevel, const QString &strText)
{
	if(m_vendor != 0) {
        AppendLog(strText, iLevel == static_cast<int>(core::LogLevel::Error));
        if(m_diskLog) m_diskLog->Record("[%s] %s", m_vendor == 2 ? "Qualcomm" : "Allwinner", strText.toUtf8().constData());
        return;
    }
    // Rockchip engine records its own detailed log.
	if (iLevel == (int)core::LogLevel::Error)
		AppendLog(strText, true);
}



void AppController::onUpgradePrompt(int prompt)
{
	bool failed = false;
	QString text = PromptText(prompt, failed);
	if (!text.isEmpty()) AppendLog(text, failed);
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
        if (result.status == core::OperationStatus::Failed && !result.diagnostic.empty())
            onOutput(static_cast<int>(core::LogLevel::Error), QString::fromStdString(result.diagnostic));
        m_qualcomm->applyResult(result); return;
    }
    if(result.operation >= core::Operation::AllwinnerFlash && result.operation <= core::Operation::AllwinnerReboot) {
        m_allwinner->applyResult(result); return;
    }
    m_rockchip->applyResult(result);
}


















void AppController::refreshEdl()
{
    if (m_vendor == 2 && !m_tasks->busy()) m_edl->refresh(m_pAdb->AdbPath());
}
void AppController::switchToEdl(const QString &target, int method, const QString &endpoint)
{
    if (m_vendor == 2 && !m_tasks->busy()) m_edl->start(target, method, endpoint, m_pAdb->AdbPath());
}
void AppController::requestEdlSwitch()
{
    if (m_vendor == 2 && !m_tasks->busy()) m_edl->requestSwitch(m_pAdb->AdbPath());
}

void AppController::BeginModeSwitch(const QString &message)
{
	m_bSwitching = true;
	m_tasks->resetProgress();
	m_tasks->setProgress(-1, message);
	m_tasks->setBusy(true);
	emit switchingChanged();
	AppendLog(message, false);
}

void AppController::UpdateModeSwitch(const QString &message)
{
	if (!m_bSwitching) return;
	m_tasks->setProgress(-1, message);
	AppendLog(message, false);
}

void AppController::FinishModeSwitch(bool ok, const QString &message)
{
	m_tasks->setProgress(ok ? 100 : 0, message);
	m_bSwitching = false;
	emit switchingChanged();
	m_tasks->setBusy(false);
	AppendLog(message, !ok);
	emit toastRequested(message, !ok);
}

void AppController::switchToLoader()
{
	if (m_tasks->busy() || m_bSwitching)
		return;
	if (!adbReady()) {
		emit toastRequested(tr("No ADB device detected; cannot switch."), true);
		return;
	}
	m_switchingFel = false;
	BeginModeSwitch(tr("Switching…"));
	m_pAdb->SwitchToLoader();
}

void AppController::switchToFel(int mode)
{
	if (m_vendor != 1 || m_tasks->busy() || m_bSwitching) return;
	if (mode < 0 || mode > 1 || !adbReady()) {
		emit toastRequested(tr("No ADB device detected or the switch method is invalid."), true);
		return;
	}
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
	m_bAdbKnown = true;
	m_bAdbAvailable = bAvailable;
	if (!bAvailable && !m_bAdbWarned) {
		m_bAdbWarned = true;
		AppendLog(strMessage, true);
		emit toastRequested(strMessage, true);
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

void AppController::onAdbFinished(bool bOk, const QString &strMessage)
{
	if (!m_bSwitching) return;
	const bool wasFel = m_switchingFel;
	if (m_switchingFel) {
		m_switchingFel = false;
		m_felSwitchHint.Set([strMessage] { return strMessage; });
		m_felSwitchError = !bOk;
		emit felSwitchStateChanged();
	}
	FinishModeSwitch(bOk, strMessage);
	if (wasFel) emit requestScan();
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
    if(m_tasks->busy() || !availableVendors().contains(vendor) || vendor==m_vendor) return;
    m_vendorSelection[m_vendor] = m_iDeviceIndex >= 0 ? m_deviceModel->keys().value(m_iDeviceIndex) : QString();
    m_vendorBatch[m_vendor] = m_batchKeys; m_vendorContinue[m_vendor] = m_bBatchContinue;
    m_vendor=vendor; m_allwinner->setActive(vendor==1); m_rockchip->setActive(vendor==0); m_pConfig->setUsbVendor(vendor == 2 ? "05c6" : vendor == 1 ? "1f3a" : "2207");
    m_bPermissionIgnored = false; m_bPermissionBlocked = PermissionMissing(); emit permissionChanged();
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
