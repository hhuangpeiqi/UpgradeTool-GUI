#include "gui/backend/AppController.h"

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

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#include "gui/AdbRunner.h"
#include "gui/OperationSpec.h"
#include "gui/EngineWorker.h"
#include "gui/FirmwareEntries.h"
#include "gui/QtObserver.h"
#include "gui/backend/ConfigStore.h"
#include "gui/backend/ImageEntryModel.h"
#include "gui/backend/KeyValueModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/backend/LogModel.h"
#include "gui/backend/ThemeManager.h"
#include "core/UpgradeObserver.h"
#include "core/Version.h"
#include "core/allwinner/Firmware.h"
#include "Config.h"
#include "platform/PlatformPath.h"

namespace gui {

namespace {

const int kScanIntervalMs = 1000;

const char *kLastFirmwareKey = "firmware/last";

QString FormatRate(double dPerSec)
{
	if (dPerSec >= 1024.0 * 1024.0)
		return QString("%1M").arg(dPerSec / (1024.0 * 1024.0), 0, 'f', 1);
	if (dPerSec >= 1024.0)
		return QString("%1K").arg(dPerSec / 1024.0, 0, 'f', 1);
	return QString("%1").arg(dPerSec, 0, 'f', 0);
}

QString FormatEta(double dSeconds)
{
	int iSec = (int)(dSeconds + 0.5);
	if (iSec < 0)
		iSec = 0;
	if (iSec >= 3600)
		return QString("%1:%2:%3")
			.arg(iSec / 3600)
			.arg((iSec / 60) % 60, 2, 10, QChar('0'))
			.arg(iSec % 60, 2, 10, QChar('0'));
	return QString("%1:%2").arg(iSec / 60).arg(iSec % 60, 2, 10, QChar('0'));
}

QString ErrorText(core::ErrorCode code)
{
	switch (code) {
	case core::ErrorCode::InvalidInput: return QObject::tr("Invalid or out-of-range parameter");
	case core::ErrorCode::DeviceMissing: return QObject::tr("Target device disconnected; select a device again");
	case core::ErrorCode::DeviceAmbiguous: return QObject::tr("Device identity is ambiguous; select a device again");
	case core::ErrorCode::FileAccess: return QObject::tr("Cannot open file; check the path and permissions");
	case core::ErrorCode::InvalidFirmware: return QObject::tr("Invalid firmware format or checksum");
	case core::ErrorCode::UnsupportedDevice: return QObject::tr("The current device mode does not support this operation");
	default: return QObject::tr("Operation failed; see the log");
	}
}

QString PromptText(int prompt, bool &failed)
{
	failed = false;
	switch (prompt) {
	case PROMPT_DOWNLOAD_BOOT_START: return QObject::tr("Downloading Boot");
	case PROMPT_DOWNLOAD_BOOT_FAIL: failed = true; return QObject::tr("Download Boot failed");
	case PROMPT_DOWNLOAD_BOOT_SUCCESS: return QObject::tr("Download Boot succeeded");
	case PROMPT_DOWNLOAD_IDB_START: return QObject::tr("Downloading IDB");
	case PROMPT_DOWNLOAD_IDB_FAIL: failed = true; return QObject::tr("Download IDB failed");
	case PROMPT_DOWNLOAD_IDB_SUCCESS: return QObject::tr("Download IDB succeeded");
	case PROMPT_DOWNLOAD_FIRMWARE_START: return QObject::tr("Downloading firmware");
	case PROMPT_DOWNLOAD_FIRMWARE_FAIL: failed = true; return QObject::tr("Download firmware failed");
	case PROMPT_DOWNLOAD_FIRMWARE_SUCCESS: return QObject::tr("Download firmware succeeded");
	case PROMPT_TEST_DEVICE_START: return QObject::tr("Testing device");
	case PROMPT_TEST_DEVICE_FAIL: failed = true; return QObject::tr("Test device failed");
	case PROMPT_TEST_DEVICE_SUCCESS: return QObject::tr("Test device succeeded");
	case PROMPT_RESET_DEVICE_START: return QObject::tr("Resetting device");
	case PROMPT_RESET_DEVICE_FAIL: failed = true; return QObject::tr("Reset device failed");
	case PROMPT_RESET_DEVICE_SUCCESS: return QObject::tr("Reset device succeeded");
	case PROMPT_WAIT_MSC_START: return QObject::tr("Waiting for MSC mode");
	case PROMPT_WAIT_MSC_FAIL: failed = true; return QObject::tr("Waiting for MSC mode failed");
	case PROMPT_WAIT_MSC_SUCCESS: return QObject::tr("Entered MSC mode");
	case PROMPT_WAIT_LOADER_START: return QObject::tr("Waiting for Loader mode");
	case PROMPT_WAIT_LOADER_FAIL: failed = true; return QObject::tr("Waiting for Loader mode failed");
	case PROMPT_WAIT_LOADER_SUCCESS: return QObject::tr("Entered Loader mode");
	case PROMPT_WAIT_MASKROM_START: return QObject::tr("Waiting for Maskrom mode");
	case PROMPT_WAIT_MASKROM_FAIL: failed = true; return QObject::tr("Waiting for Maskrom mode failed");
	case PROMPT_WAIT_MASKROM_SUCCESS: return QObject::tr("Entered Maskrom mode");
	case PROMPT_ERASE_IDB_START: return QObject::tr("Erasing IDB");
	case PROMPT_ERASE_IDB_FAIL: failed = true; return QObject::tr("Erase IDB failed");
	case PROMPT_ERASE_IDB_SUCCESS: return QObject::tr("Erase IDB succeeded");
	case PROMPT_SWITCH_MSC_START: return QObject::tr("Switching to MSC mode");
	case PROMPT_SWITCH_MSC_FAIL: failed = true; return QObject::tr("Switch to MSC mode failed");
	case PROMPT_SWITCH_MSC_SUCCESS: return QObject::tr("Switched to MSC mode");
	case PROMPT_CHECK_CHIP_START: return QObject::tr("Checking chip");
	case PROMPT_CHECK_CHIP_FAIL: failed = true; return QObject::tr("Check chip failed");
	case PROMPT_CHECK_CHIP_SUCCESS: return QObject::tr("Check chip succeeded");
	case PROMPT_PREPARE_IDB_START: return QObject::tr("Preparing IDB");
	case PROMPT_PREPARE_IDB_FAIL: failed = true; return QObject::tr("Prepare IDB failed");
	case PROMPT_PREPARE_IDB_SUCCESS: return QObject::tr("Prepare IDB done");
	case PROMPT_ERASE_FLASH_START: return QObject::tr("Erasing Flash");
	case PROMPT_ERASE_FLASH_FAIL: failed = true; return QObject::tr("Erase Flash failed");
	case PROMPT_ERASE_FLASH_SUCCESS: return QObject::tr("Erase Flash succeeded");
	case PROMPT_GET_BLOCK_STATE_START: return QObject::tr("Reading block state");
	case PROMPT_GET_BLOCK_STATE_FAIL: failed = true; return QObject::tr("Read block state failed");
	case PROMPT_GET_BLOCK_STATE_SUCCESS: return QObject::tr("Read block state succeeded");
	case PROMPT_GET_FLASH_INFO_START: return QObject::tr("Reading Flash info");
	case PROMPT_GET_FLASH_INFO_FAIL: failed = true; return QObject::tr("Read Flash info failed");
	case PROMPT_GET_FLASH_INFO_SUCCESS: return QObject::tr("Read Flash info succeeded");
	case PROMPT_LOWER_FORMAT_START: return QObject::tr("Low-level formatting");
	case PROMPT_LOWER_FORMAT_FAIL: failed = true; return QObject::tr("Low-level format failed");
	case PROMPT_LOWER_FORMAT_SUCCESS: return QObject::tr("Low-level format succeeded");
	default: return QString();
	}
}

}

AppController::AppController(CRKLog *pLog, QObject *parent)
	: QObject(parent),
	  m_pWorker(new EngineWorker(pLog)),
	  m_pObserver(new QtObserver(this)),
	  m_pThread(new QThread(this)),
	  m_pAdb(new AdbRunner(this)),
	  m_pConfig(new ConfigStore(this)),
	  m_pTheme(new ThemeManager(this)),
	  m_pLanguage(new LanguageManager(this)),
	  m_pLog(new LogModel(this)),
	  m_pImageEntries(new ImageEntryModel(this)),
	  m_pFirmwareEntries(new KeyValueModel(this)),
	  m_pResults(new KeyValueModel(this)),
	  m_iDeviceIndex(-1),
	  m_bPermissionBlocked(false),
	  m_bPermissionIgnored(false),
	  m_bBusy(false),
	  m_bCancelRequested(false),
	  m_iProgressValue(0),
	  m_bBatchMode(false),
	  m_bBatchContinue(true),
	  m_llProgressBase(0),
	  m_bImageHintError(false),
	  m_bQueueActive(false),
	  m_bFirmwareHintError(false),
	  m_bFirmwareValid(false),
	  m_bFirmwareReboot(true),
	  m_bAdvancedHintError(false),
	  m_bSwitching(false),
	  m_bAdbKnown(false),
	  m_bAdbAvailable(false),
	  m_bAdbWarned(false)
{
	m_diskLog = pLog;
	const auto awSettings = allwinnerSettings();
    for (int i = 0; i < 4; ++i) {
        auto &state = m_allwinner[i];
        state.summary = new KeyValueModel(this);
        state.results = new KeyValueModel(this);
        if (i < 3) state.firmware = awSettings.value(QString("page%1/firmware").arg(i), awSettings.value("firmware")).toString();
    }
	m_pWorker->moveToThread(m_pThread);
	m_pWorker->SetObserver(m_pObserver);

	connect(m_pThread, SIGNAL(started()), this, SLOT(onThreadStarted()));
	connect(this, SIGNAL(requestScan()), m_pWorker, SLOT(scanDevices()));
	qRegisterMetaType<core::TaskJob>();
	qRegisterMetaType<core::OperationResult>();
	qRegisterMetaType<core::TaskSummary>();
	connect(this, &AppController::requestExecute, m_pWorker, &EngineWorker::execute);
	connect(this, &AppController::requestAbort, m_pWorker, &EngineWorker::abortTask, Qt::DirectConnection);
	connect(m_pThread, &QThread::finished, m_pWorker, &QObject::deleteLater);
	connect(m_pWorker, &EngineWorker::devicesReady, this, &AppController::onDevicesReady);
	connect(m_pWorker, &EngineWorker::taskFinished, this, &AppController::onTaskFinished);
	connect(m_pWorker, &EngineWorker::stepStarted, this, &AppController::onStepStarted);
	connect(m_pObserver, &QtObserver::outputReceived, this, &AppController::onOutput);
    connect(m_pObserver, &QtObserver::stageProgressReceived, this, [this](const QString &stage,long long total,long long current,int overall) {
        onProgress(stage,total,current,current == 0 ? CALL_FIRST : CALL_MIDDLE);
        if(overall >= 0) SetProgress(overall, m_strProgressText);
    });
	connect(m_pObserver, &QtObserver::progressReceived, this, &AppController::onProgress);
	connect(m_pObserver, &QtObserver::upgradePromptReceived, this, &AppController::onUpgradePrompt);
	connect(m_pObserver, &QtObserver::storageSelectionRequested, this, &AppController::onStorageSelection);
	connect(m_pObserver, &QtObserver::resultReceived, this, &AppController::onOperationResult);

	connect(m_pAdb, SIGNAL(finished(bool, QString)),
	        this, SLOT(onAdbFinished(bool, QString)));
	connect(m_pAdb, &AdbRunner::switchProgress, this, [this](const QString &message) {
		if (!m_switchingFel) return;
		m_felSwitchHint = message;
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

	connect(m_pLanguage, &gui::LanguageManager::languageChanged,
	        this, &AppController::onLanguageChanged);

	m_bPermissionBlocked = PermissionMissing();
	emit permissionChanged();

	SetImageHint(tr("Select the split package directory (with parameter.txt, per-partition .img and Loader .bin)."), false);
	SetFirmwareHint(tr("Select firmware."), false);
	SetAdvancedHint(tr("Ready"), false);
	SetDeviceStatus();

	m_strFwVersion = m_strFwLoader = m_strFwChip = m_strFwBuild = m_strFwSign = QString("-");

	{
		QSettings settings(QSettings::IniFormat, QSettings::UserScope,
		                   "upgrade_tool", "upgrade_tool");
		QString strLast = settings.value(kLastFirmwareKey).toString().trimmed();
		if (strLast.isEmpty()) { std::string path; if (GetConfigString("firmware", path)) strLast = QString::fromStdString(path); }
		if (!strLast.isEmpty()) {
			if (QFileInfo(strLast).isFile())
				setFirmware(strLast);
			else
				settings.remove(kLastFirmwareKey);
		}
	}

	{
		QString strStamp = QTime::currentTime().toString("HH:mm:ss");
		AppendLog(QString("[%1] %2").arg(strStamp).arg(tr("Application started")), false);
	}

	m_pThread->start();

	QTimer *pTimer = new QTimer(this);
	connect(pTimer, SIGNAL(timeout()), this, SLOT(refresh()));
	pTimer->start(kScanIntervalMs);

	m_pAdb->Start();
}

AppController::~AppController()
{
	m_job.cancelled->store(true);
	m_pObserver->CancelSelection();
	emit requestAbort();
	m_pThread->quit();
	m_pThread->wait();
}


QStringList AppController::storages() const
{
	return QStringList()
		<< "1.FLASH" << "2.EMMC" << "3.SD" << "4.SD1" << "5.SPIFLASH"
		<< "6.SPI/RAMD" << "7.RKNN" << "8.NFS" << "9.SATA" << "10.PCIE"
		<< "11.UFS" << "12.NVME";
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

QVariantList AppController::usageHelp() const
{
	struct MakeItem {
		static QVariantMap Run(const QString &strName, const QString &strDesc)
		{
			QVariantMap map;
			map["name"] = strName;
			map["desc"] = strDesc;
			return map;
		}
	};
	struct MakeGroup {
		static QVariantMap Run(const QString &strVendor, const QString &strTitle,
		                       const QString &strNote, const QVariantList &items)
		{
			QVariantMap map;
			map["vendor"] = strVendor;
			map["title"] = strTitle;
			map["note"] = strNote;
			map["items"] = items;
			return map;
		}
	};

#ifdef Q_OS_LINUX
	const QString connectionNote = tr("Rockchip devices must be in Rockusb mode (Loader or Maskrom) and Allwinner devices in FEL / FES mode, with the matching udev rules installed.");
	const QString connectionHelp = tr("The status bar scans automatically; if nothing appears, click \"Refresh\" and check the device mode and udev permissions.");
	const QString preferencesHelp = tr("Edit and save config.ini, switch theme and language, reload, and install udev rules in one click.");
	const QString missingHelp = tr("Confirm Rockchip is in Rockusb mode (Loader or Maskrom) and Allwinner in FEL / FES mode, then reconnect the device after installing the udev rules.");
#else
	const QString connectionNote = tr("Rockchip devices must be in Rockusb mode (Loader or Maskrom) and Allwinner devices in FEL / FES mode.");
	const QString connectionHelp = tr("The status bar scans automatically; if nothing appears, click \"Refresh\" and check the device mode, USB connection, and whether another program is using it.");
	const QString preferencesHelp = tr("Edit and save config.ini, switch theme and language, then reload.");
	const QString missingHelp = tr("Confirm Rockchip is in Rockusb mode (Loader or Maskrom) and Allwinner in FEL / FES mode; check the USB connection, close other programs using the device, then reconnect.");
#endif
	const QString vendorCommon = tr("General");
	const QString vendorRk = tr("Rockchip");
	const QString vendorAw = tr("Allwinner");

	QVariantList groups;

	groups.append(MakeGroup::Run(vendorCommon, tr("Getting Started"),
		connectionNote,
		QVariantList()
		<< MakeItem::Run(tr("① Connect the device"), connectionHelp)
		<< MakeItem::Run(tr("② Select the device"), tr("Choose the target in the status bar dropdown; to flash several at once, use \"Device → Batch Select…\"."))
		<< MakeItem::Run(tr("③ Run the operation"), tr("Open the matching page, choose the file or parameters, then click \"Execute\" / \"Upgrade\" / \"Start Flashing\"; progress, speed and ETA show at the bottom, and you can \"Cancel\" anytime."))
		<< MakeItem::Run(tr("④ Check the result"), tr("The result appears in the page hint area; raw engine output is written to the log file (see \"Help → Log\" for the directory)."))));

	groups.append(MakeGroup::Run(vendorRk, tr("Download Image"),
		tr("Download partition by partition using parameter.txt in the split package directory."), QVariantList()
		<< MakeItem::Run(tr("Browse…"), tr("Select the split package directory; it must contain parameter.txt, the per-partition images and the Loader."))
		<< MakeItem::Run(tr("Path column"), tr("Double-click a row to specify the image file for that partition manually."))
		<< MakeItem::Run(tr("Checkbox"), tr("Select the partitions to download; the header checkbox selects or clears all."))
		<< MakeItem::Run(tr("Switch"), tr("Switch an ADB-mode device to Loader mode (ADB must be installed on the host)."))
		<< MakeItem::Run(tr("Clear"), tr("Clear the current directory and partition selection."))
		<< MakeItem::Run(tr("Reboot device when done"), tr("Reboot the device after the queue finishes."))
		<< MakeItem::Run(tr("Execute"), tr("Download each checked partition image in turn."))));

	groups.append(MakeGroup::Run(vendorRk, tr("Upgrade Firmware"),
		tr("Upgrade the full firmware package; the UI parses and shows firmware info."), QVariantList()
		<< MakeItem::Run(tr("Browse…"), tr("Select a full .img firmware package."))
		<< MakeItem::Run(tr("Firmware Info"), tr("Show firmware version, Loader version, chip info, build time and signature."))
		<< MakeItem::Run(tr("View Firmware Info"), tr("Re-parse and show firmware info."))
		<< MakeItem::Run(tr("Extract Firmware…"), tr("Extract the firmware to a chosen directory."))
		<< MakeItem::Run(tr("Reboot device when done"), tr("Check to reboot the device automatically after the upgrade."))
		<< MakeItem::Run(tr("Upgrade"), tr("Flash the whole package; per-partition filtering is not supported."))));

	groups.append(MakeGroup::Run(vendorRk, tr("Advanced"),
		tr("Device operations needing parameters; results fill the result table."), QVariantList()
		<< MakeItem::Run(tr("Read buttons"), tr("Read Flash ID / Flash info / Chip info / Capability / Secure mode / OTP / serial number."))
		<< MakeItem::Run(tr("Test Device / Reset Device / Switch Storage / Switch Device"), tr("Run the corresponding operation on the current device."))
		<< MakeItem::Run(tr("Download Boot"), tr("Download Boot using the \"Boot\" file path above."))
		<< MakeItem::Run(tr("Erase All"), tr("Erase the device Flash."))
		<< MakeItem::Run(tr("Export COM Log"), tr("Save the current device's COM log to a file."))
		<< MakeItem::Run(tr("Write Serial"), tr("Write the contents of the \"Serial\" field to the device."))
		<< MakeItem::Run(tr("LBA / Sector"), tr("After entering the start sector and count, you can \"Export Image\" or \"Erase Sectors\"."))
		<< MakeItem::Run(tr("Expert Parameters"), tr("Write LBA / Write Sector / Erase Block / Run System / Reset Pipe; each needs its own parameters."))));

	groups.append(MakeGroup::Run(vendorAw, tr("Raw Image Flash"),
		tr("Write a single image to the chosen storage medium."), QVariantList()
		<< MakeItem::Run(tr("Boot package"), tr("Select the boot package (IMAGEWTY .img) that determines the partition layout and boot components."))
		<< MakeItem::Run(tr("Image to write"), tr("Select the image file to write."))
		<< MakeItem::Run(tr("Storage medium"), tr("Choose SD / eMMC, UFS or SPI NOR."))
		<< MakeItem::Run(tr("Advanced options"), tr("Set the write method (from sector 0 / at logic offset) and the logic offset (sectors, empty to auto-detect)."))
		<< MakeItem::Run(tr("Switch"), tr("Switch the device to FEL via ADB; when a T3 Buildroot is detected the compatible entry is used automatically, or choose \"Alternate\" manually."))
		<< MakeItem::Run(tr("Start Flashing"), tr("Write the selected image to the target medium."))));

	groups.append(MakeGroup::Run(vendorAw, tr("Flash Firmware"),
		tr("Flash the full firmware package; the UI parses and shows firmware info."), QVariantList()
		<< MakeItem::Run(tr("Firmware package"), tr("Select an IMAGEWTY firmware package (.img)."))
		<< MakeItem::Run(tr("Flash Mode"), tr("Flash selected partitions / Keep user data / Erase partitions then flash / Full erase then flash."))
		<< MakeItem::Run(tr("After Finish"), tr("Reboot, power off or shut down after flashing; default is reboot."))
		<< MakeItem::Run(tr("Verify after write"), tr("When checked, the write is verified; a verification failure stops the task."))
		<< MakeItem::Run(tr("Partition list"), tr("When \"Flash selected partitions\" is chosen, check the partitions to flash."))
		<< MakeItem::Run(tr("Start Flashing"), tr("Flash according to the settings."))));

	groups.append(MakeGroup::Run(vendorAw, tr("Advanced"),
		tr("View firmware offline, extract, convert programmer images, and reboot the device."), QVariantList()
		<< MakeItem::Run(tr("View partitions and embedded files"), tr("Expand to show the firmware's partition table and embedded files."))
		<< MakeItem::Run(tr("Extract Firmware"), tr("Choose an output directory and extract the current firmware into it."))
		<< MakeItem::Run(tr("Programmer Image Convert"), tr("Set the output image and target medium (SPI NOR / SD card / eMMC / SD NAND / UFS)."))
		<< MakeItem::Run(tr("Advanced options"), tr("Logic offset, U-Boot offset, NOR capacity, GPT target capacity, secure boot components."))
		<< MakeItem::Run(tr("Read Info Again / Convert Image"), tr("Re-parse firmware info / generate the programmer image."))
		<< MakeItem::Run(tr("Reboot Device"), tr("Reboot an Allwinner device in FES mode; FEL mode is not supported."))));

	groups.append(MakeGroup::Run(vendorCommon, tr("Config and Device menus"),
		tr("The dropdown menus on the right of the title bar."), QVariantList()
		<< MakeItem::Run(tr("Config → Preferences…"), preferencesHelp)
		<< MakeItem::Run(tr("Config → Generate GPT…"), tr("Rockchip only: generate a GPT image from parameter.txt."))
		<< MakeItem::Run(tr("Device → Device Info…"), tr("Allwinner only: view current device info."))
		<< MakeItem::Run(tr("Device → Refresh"), tr("Rescan the currently connected devices."))
		<< MakeItem::Run(tr("Device → Batch Select…"), tr("Check multiple devices to run the same operation in order; \"Continue on failure\" is available."))));

	groups.append(MakeGroup::Run(vendorCommon, tr("FAQ"), QString(), QVariantList()
		<< MakeItem::Run(tr("No device ever appears"), missingHelp)
		<< MakeItem::Run(tr("The \"Switch\" button is grayed out"), tr("ADB is not installed on the host, or no ADB device was detected."))
		<< MakeItem::Run(tr("Button grayed out with an unsupported notice"), tr("\"The current engine does not support this feature\" means the operation is not implemented yet."))
		<< MakeItem::Run(tr("Clicking \"Cancel\" during execution"), tr("It counts as \"Cancelled\", not a failure, and stops the current operation and the remaining queue."))));

	return groups;
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

QVariantList AppController::standardPlaces() const
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
	for (int i = 0; i < m_devices.size(); i++) {
		QVariantMap map = m_devices[i].toMap();
		list << QString("#%1 [%2] %3")
			.arg(map["index"].toInt())
			.arg(map["mode"].toString())
			.arg(map["key"].toString());
	}
	return list;
}


void AppController::SetDeviceStatus()
{
	if (m_bBatchMode && !m_batchKeys.isEmpty()) {
		m_strDeviceStatus = tr("Batch: %1 selected / %2 current")
		                    .arg(m_batchKeys.size())
		                    .arg(m_deviceKeys.size());
		emit devicesChanged();
		return;
	}

	// 按 rockusb 模式聚合，保持设备出现顺序
	QStringList strModes;
	QMap<QString, int> mapCount;
	for (int i = 0; i < m_devices.size(); i++) {
		QString strMode = m_devices[i].toMap()["mode"].toString();
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

	// 没有当前厂商的刷机模式设备时，提示共用的 ADB 状态
	if (strParts.isEmpty()) {
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

void AppController::SetProgress(int iPct, const QString &strText)
{
	m_iProgressValue = iPct;
	m_strProgressText = strText;
	emit progressChanged();
}

void AppController::ResetProgress()
{
	m_progressTimer.invalidate();
	m_strProgressStep.clear();
	m_llProgressBase = 0;
	m_strProgressSpeed.clear();
	SetProgress(0, QString());
}

void AppController::AppendLog(const QString &strText, bool bError)
{
	if (m_pLog)
		m_pLog->Append(strText, bError);
}

void AppController::SetBusy(bool bBusy)
{
	if (m_bBusy == bBusy)
		return;
	m_bBusy = bBusy;
	emit busyChanged();
}

void AppController::SetImageHint(const QString &strText, bool bError)
{
	m_strImageHint = strText;
	m_bImageHintError = bError;
	emit imageStateChanged();
}

void AppController::SetFirmwareHint(const QString &strText, bool bError)
{
	m_strFirmwareHint = strText;
	m_bFirmwareHintError = bError;
	emit firmwareStateChanged();
}

void AppController::SetAdvancedHint(const QString &strText, bool bError)
{
	m_strAdvancedHint = strText;
	m_bAdvancedHintError = bError;
	emit advancedStateChanged();
}

void AppController::onLanguageChanged()
{
	// Retranslate strings that AppController owns. The status bar and idle
	// hints are refreshed here; hints describing the last operation refresh on
	// the next action.
	SetDeviceStatus();

	if (!m_bBusy) {
		if (m_strImageDir.isEmpty())
			SetImageHint(tr("Select the split package directory (with parameter.txt, per-partition .img and Loader .bin)."), false);
		if (m_strFirmware.isEmpty())
			SetFirmwareHint(tr("Select firmware."), false);
	}

	emit imageStateChanged();
	emit firmwareStateChanged();
	emit advancedStateChanged();
	emit progressChanged();
	emit felSwitchStateChanged();
	emit allwinnerChanged();
}


void AppController::onThreadStarted()
{
	refresh();
}

void AppController::onDevicesReady(const QVariantList &incoming)
{
    m_allDevices = incoming;
    QVariantList devices;
    for(const auto &d:incoming) if(d.toMap().value("vendor",0).toInt()==m_vendor) devices.append(d);
	QStringList strSignature;
	for (int i = 0; i < devices.size(); i++) {
		QVariantMap map = devices[i].toMap();
		strSignature << QString("%1:%2:%3")
			.arg(map["index"].toInt())
			.arg(map["mode"].toString())
			.arg(map["key"].toString());
	}
	QString strNewSignature = strSignature.join('\n');
	if (strNewSignature == m_strDeviceSignature)
		return;
	m_strDeviceSignature = strNewSignature;

	QString strPrevious = m_vendorSelection[m_vendor];
	if (m_iDeviceIndex >= 0 && m_iDeviceIndex < m_deviceKeys.size())
		strPrevious = m_deviceKeys.value(m_iDeviceIndex);

	m_devices = devices;
	m_deviceKeys.clear();
	for (int i = 0; i < devices.size(); i++)
		m_deviceKeys << devices[i].toMap()["key"].toString();

	int iSelected = m_deviceKeys.indexOf(strPrevious);
	if (iSelected < 0 && !devices.isEmpty())
		iSelected = 0;

	if (iSelected != m_iDeviceIndex) {
		m_iDeviceIndex = iSelected;
		emit deviceIndexChanged();
	}


	if (m_bBatchMode) {
		QStringList strAlive;
		for (int i = 0; i < m_batchKeys.size(); i++)
			if (m_deviceKeys.contains(m_batchKeys[i]))
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
	if (m_bBusy) return;
	if (iIndex < -1 || iIndex >= m_devices.size())
		return;
	if (iIndex == m_iDeviceIndex)
		return;

	if (m_bBatchMode) {
		m_bBatchMode = false;
		m_batchKeys.clear();
		emit batchModeChanged();
	}
	m_iDeviceIndex = iIndex;
    m_vendorSelection[m_vendor] = iIndex >= 0 ? m_deviceKeys.value(iIndex) : QString();
	emit deviceIndexChanged();

	SetDeviceStatus();
}

void AppController::refresh()
{
	if (m_bBusy) return;
	emit requestScan();
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
	if (!m_bBusy)
		return;
	if (m_bSwitching) {
		m_pAdb->CancelSwitch();
		return;
	}
	m_bCancelRequested = true;
	m_job.cancelled->store(true);
	m_pObserver->CancelSelection();
	AppendLog(tr("Cancelling…"), false);
	emit requestAbort();
}


QVariantList AppController::BatchTargets() const
{
	QVariantList targets;
	for (int i = 0; i < m_batchKeys.size(); i++) {
		int iPos = m_deviceKeys.indexOf(m_batchKeys[i]);
		if (iPos < 0)
			continue;
		targets.append(m_devices[iPos].toMap());
	}
	return targets;
}

void AppController::StartJob(core::TaskJob job)
{
	if (m_bBusy || job.steps.empty()) return;
	const bool local = core::IsLocalOperation(job.steps.front().operation);
	if (!local) {
		QVariantList targets;
		if (m_bBatchMode) targets = BatchTargets();
		else if (m_iDeviceIndex >= 0 && m_iDeviceIndex < m_devices.size()) targets.append(m_devices[m_iDeviceIndex]);
		if (targets.isEmpty()) { emit toastRequested(tr("Select a device first"), true); return; }
		for (const auto &value : targets) {
			QVariantMap map = value.toMap();
			core::DeviceIdentity identity;
			identity.serial = map["serial"].toString().toStdString();
			identity.location = map["locationId"].toUInt();
            identity.vendor = static_cast<core::Vendor>(map.value("vendor",0).toInt());
            identity.bus = static_cast<uint8_t>(map["bus"].toUInt());
            for(const auto &p:map["ports"].toList()) identity.ports.push_back(static_cast<uint8_t>(p.toUInt()));
			job.targets.push_back(identity);
		}
	}
	job.continueOnError = m_bBatchMode && m_bBatchContinue;
	m_job = job;
	m_runningOperation = job.steps.front().operation;
	m_bCancelRequested = false;
	m_pObserver->BeginSelectionSession();
	SetBusy(true); ResetProgress();
	AppendLog(tr("Start: %1").arg(OperationName(m_runningOperation)), false);
	emit requestExecute(m_job);
}

void AppController::runOperation(int operation, const QVariantMap &parameters)
{
	if (m_bBusy) return;
	core::OperationRequest r;
	r.operation = static_cast<core::Operation>(operation);
	if (operation < 0 || operation >= static_cast<int>(core::Operation::AllwinnerFlash) || m_vendor != 0) return;
	r.file.path = parameters.value("path").toString().toStdString();
	r.file.output = parameters.value("output").toString().toStdString();
	r.partition = parameters.value("partition").toString().toStdString();
	r.serial = parameters.value("serial").toString().trimmed().toStdString();
	r.reboot = parameters.value("reboot", true).toBool();
	r.block.force = parameters.value("force", false).toBool();
	bool valid = true;
	auto number = [&](const char *key, uint32_t &value) {
		if (!parameters.contains(key)) return;
		QString text = parameters.value(key).toString().trimmed();
		bool ok = false;
		qulonglong parsed = text.toULongLong(&ok, text.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
		if (!ok || text.startsWith('-') || parsed > UINT32_MAX) valid = false;
		else value = static_cast<uint32_t>(parsed);
	};
	number("begin", r.lba.begin); number("count", r.lba.count);
	number("pageSize", r.sector.pageSize); number("spareSize", r.sector.spareSize);
	number("chipSelect", r.block.chipSelect); number("subcode", r.subcode); number("pipe", r.pipe);
	number("ubootAddress", r.system.ubootAddress); number("trustAddress", r.system.trustAddress); number("bootAddress", r.system.bootAddress);
	r.system.uboot = parameters.value("uboot").toString().toStdString();
	r.system.trust = parameters.value("trust").toString().toStdString();
	r.system.boot = parameters.value("boot").toString().toStdString();
	// Keep existing loader fallback, resolved before submitting a business request.
	if (r.file.path.empty() && (r.operation == core::Operation::EraseFlash || r.operation == core::Operation::DownloadBoot || r.operation == core::Operation::LoaderUpgrade))
		GetConfigString("loader", r.file.path);
	if (!valid) { SetAdvancedHint(tr("Numeric parameter invalid or out of range"), true); emit toastRequested(tr("Numeric parameter invalid or out of range"), true); return; }
	core::TaskJob job; job.steps.push_back(r); StartJob(job);
}

void AppController::setBatchTargets(const QStringList &keys, bool bContinueOnError)
{
	if (m_bBusy) return;
	QStringList strAlive;
	for (int i = 0; i < keys.size(); i++)
		if (m_deviceKeys.contains(keys[i]))
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
		int iPos = m_deviceKeys.indexOf(strAlive[0]);
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
	if (m_bBusy) return;
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
	SetBusy(false);
	QString text = cancelled ? tr("Cancelled") : ok ? tr("Operation complete") : tr("Operation failed; see the log");
	AppendLog(text, !ok && !cancelled);
	if (m_bQueueActive) {
		m_bQueueActive = false;
		SetImageHint(cancelled ? tr("Cancelled.") : ok ?
			(m_job.rebootAfter ? tr("Download complete; reboot command sent.") : tr("Download complete.")) : tr("Download finished with failures. Failed devices were not rebooted automatically."), !ok && !cancelled);
		emit imageStateChanged();
	} else if (cancelled) {
		if (m_runningOperation == core::Operation::FirmwareUpgrade || m_runningOperation == core::Operation::FirmwareExtract || m_runningOperation == core::Operation::FirmwareInfo)
			SetFirmwareHint(text, false);
		else SetAdvancedHint(text, false);
	}
	if (m_job.targets.size() > 1) {
		QString batch = tr("Batch finished: %1 succeeded, %2 failed").arg(summary.succeeded).arg(summary.failed);
		if (cancelled) batch += tr(", remaining operations cancelled");
		AppendLog(batch, summary.failed > 0);
		if (m_runningOperation == core::Operation::FirmwareUpgrade) SetFirmwareHint(batch, !ok && !cancelled);
		else if (m_runningOperation != core::Operation::LoaderUpgrade && m_runningOperation != core::Operation::DownloadImage) SetAdvancedHint(batch, !ok && !cancelled);
	}
    if(m_runningOperation >= core::Operation::AllwinnerFlash && m_runningOperation < core::Operation::Count) {
        auto &state = m_allwinner[m_allwinnerContext];
        state.error = !ok && !cancelled;
        if(cancelled) state.hint=tr("Cancelled");
        if(m_job.targets.size()>1) {
            state.hint=tr("Batch finished: %1 succeeded, %2 failed").arg(summary.succeeded).arg(summary.failed);
            if(cancelled) state.hint += tr(", remaining operations cancelled");
        }
        emit allwinnerChanged();
    }
	SetProgress(ok ? 100 : m_iProgressValue, text);
	m_bCancelRequested = false;
	emit toastRequested(text, !ok && !cancelled);
}

void AppController::onStepStarted(int step, int target, int total, const QString &device)
{
	ResetProgress();
	if (step < static_cast<int>(m_job.steps.size())) {
		const auto &request = m_job.steps[step];
		QString label = request.partition.empty() ? OperationName(request.operation) : QString::fromStdString(request.partition);
		QString text = tr("Running %1/%2: %3").arg(step + 1).arg(m_job.steps.size()).arg(label);
		if (total > 1) text += tr(", device %1/%2 %3").arg(target + 1).arg(total).arg(device);
		if (m_bQueueActive) SetImageHint(text, false);
		AppendLog(text, false);
	}
}

void AppController::onOutput(int iLevel, const QString &strText)
{
	if(m_vendor == 1) {
        AppendLog(strText, iLevel == static_cast<int>(core::LogLevel::Error));
        if(m_diskLog) m_diskLog->Record("[Allwinner] %s", strText.toUtf8().constData());
        return;
    }
    // Rockchip engine records its own detailed log.
	if (iLevel == (int)core::LogLevel::Error)
		AppendLog(strText, true);
}

void AppController::onProgress(const QString &strPrompt, long long llTotal,
                               long long llCurrent, int iCall)
{
	if (llTotal <= 0)
		return;
	int iPct = static_cast<int>(static_cast<double>(llCurrent) * 100.0 / llTotal);
	if (iPct > 100)
		iPct = 100;

	if (strPrompt != m_strProgressStep || iCall == (int)CALL_FIRST) {
		m_strProgressStep = strPrompt;
		m_llProgressBase = llCurrent;
		m_progressTimer.start();
	}

	QString strText = tr("%1  %2%").arg(strPrompt).arg(iPct);
	m_strProgressSpeed.clear();
	if (m_progressTimer.isValid()) {
		long long llDone = llCurrent - m_llProgressBase;
		qint64 llElapsed = m_progressTimer.elapsed();
		if (llElapsed > 0 && llDone > 0) {
			double dRate = llDone * 1000.0 / (double)llElapsed;
			strText += tr("  %1/s").arg(FormatRate(dRate));
			m_strProgressSpeed = tr("%1/s").arg(FormatRate(dRate));
			if (dRate > 0.0 && llCurrent < llTotal) {
				QString strEta = tr("  remaining %1").arg(FormatEta((llTotal - llCurrent) / dRate));
				strText += strEta;
				m_strProgressSpeed += strEta;
			}
		}
	}
	SetProgress(iPct, strText);
}

void AppController::onUpgradePrompt(int prompt)
{
	bool failed = false;
	QString text = PromptText(prompt, failed);
	if (!text.isEmpty()) AppendLog(text, failed);
}

void AppController::onStorageSelection(const QStringList &strOptions, int iCurrent)
{
	if (m_bBusy && !m_bCancelRequested) emit storageSelectionRequested(strOptions, iCurrent);
	else m_pObserver->SetSelectResult(0);
}

void AppController::answerStorage(int iIndex)
{
	m_pObserver->SetSelectResult(iIndex);
}


void AppController::ApplyAllwinnerInspectResult(int context, const core::OperationResult &result)
{
    auto &state = m_allwinner[context];
    QVariantList items;
    for(const auto &row:result.items) items.append(QVariantMap{{"label",QString::fromStdString(row.label)},{"value",QString::fromStdString(row.value)}});
    state.files.clear(); state.partitions.clear(); state.valid = result.ok();
    for(const auto &e:result.firmware.entries) state.files.append(QVariantMap{{"name",QString::fromStdString(e.fileName)},{"type",QString::fromStdString(e.type)},{"size",QString::number(e.size)}});
    for(const auto &p:result.partitions) {
        bool writable=false;
        for(const auto &e:result.firmware.entries) if(e.hasPart && e.partName==p.name) writable=true;
        state.partitions.append(QVariantMap{{"name",QString::fromStdString(p.name)},{"begin",QString::number(p.begin)},{"count",QString::number(p.count)},{"writable",writable}});
    }
    state.summary->SetItems(items);
    state.error = !result.ok() && result.status != core::OperationStatus::Cancelled;
    state.hint = result.ok() ? OperationName(result.operation) + tr("done") : QString::fromStdString(result.diagnostic);
    if(result.status==core::OperationStatus::Cancelled) state.hint=tr("Cancelled");
    emit allwinnerChanged();
}

void AppController::onOperationResult(const core::OperationResult &result)
{
    if(result.operation >= core::Operation::AllwinnerFlash && result.operation < core::Operation::Count) {
        if(result.operation == core::Operation::AllwinnerInspect) { ApplyAllwinnerInspectResult(m_allwinnerContext, result); return; }
        auto &state = m_allwinner[m_allwinnerContext];
        QVariantList items;
        for(const auto &row:result.items) items.append(QVariantMap{{"label",QString::fromStdString(row.label)},{"value",QString::fromStdString(row.value)}});
        state.results->SetItems(items);
        state.error = !result.ok() && result.status != core::OperationStatus::Cancelled;
        state.hint = result.ok() ? OperationName(result.operation) + tr("done") : QString::fromStdString(result.diagnostic);
        if(result.status==core::OperationStatus::Cancelled) state.hint=tr("Cancelled");
        emit allwinnerChanged(); return;
    }
	bool bOk = result.ok();
	core::Operation id = result.operation;
	QVariantList items;
	for (const auto &row : result.items) { QVariantMap item; item["label"] = QString::fromStdString(row.label); item["value"] = QString::fromStdString(row.value); items.append(item); }
	if (id == core::Operation::FirmwareInfo) {
		for (const auto &entry : result.firmware.entries) { QVariantMap item; item["label"] = QString::fromStdString(entry.partName); item["value"] = tr("%1, size %2, LBA 0x%3").arg(QString::fromStdString(entry.fileName)).arg(entry.size).arg(entry.lba, 0, 16); items.append(item); }
	}

	if (id == core::Operation::DownloadImage || id == core::Operation::LoaderUpgrade) {
		if (!bOk && !m_bCancelRequested) {
			QString strReason = ErrorText(result.error);
			SetImageHint(strReason.isEmpty() ? tr("Download failed; see the log.")
			                                 : tr("Download failed: %1").arg(strReason), true);
		}

		return;
	}

	if (id == core::Operation::ResetDevice) {
		SetImageHint(bOk ? tr("Reboot command sent.") : tr("Reset device failed."), !bOk);
		m_pResults->SetItems(items);
		SetAdvancedHint(bOk ? tr("Reset device succeeded") : tr("Reset device failed"), !bOk);

		return;
	}

	if (id == core::Operation::FirmwareUpgrade) {
		if (bOk)
			SetFirmwareHint(m_bFirmwareReboot ? tr("Upgrade succeeded; device rebooted.")
			                                  : tr("Upgrade succeeded; device not rebooted."), false);
		else if (!m_bCancelRequested) {
			QString strReason = ErrorText(result.error);
			SetFirmwareHint(strReason.isEmpty() ? tr("Upgrade failed; see the log.")
			                                    : tr("Upgrade failed: %1").arg(strReason), true);
		}

		return;
	}

	if (id == core::Operation::FirmwareExtract) {
		SetFirmwareHint(bOk ? tr("Extraction succeeded.") : tr("Extraction failed."), !bOk);

		return;
	}

	if (id == core::Operation::FirmwareInfo) {
		m_pFirmwareEntries->Clear();
		if (!bOk) {
			SetFirmwareHint(tr("Failed to read firmware info."), true);

			return;
		}
		m_pFirmwareEntries->SetItems(items);
		m_strFwChip = QString::fromStdString(result.firmware.chip);
		m_strFwVersion = QString::fromStdString(result.firmware.version);
		m_strFwLoader = QString::fromStdString(result.firmware.loaderVersion);
		m_strFwBuild = QString::fromStdString(result.firmware.buildTime);
		m_strFwSign = QString::fromStdString(result.firmware.sign);
		SetFirmwareHint(tr("Firmware info read successfully."), false);
		emit firmwareStateChanged();

		return;
	}

	m_pResults->SetItems(items);
	QString strName = OperationName(id);
	if (!bOk)
		SetAdvancedHint(tr("%1 failed: %2").arg(strName, ErrorText(result.error)), true);
	else if (items.isEmpty())
		SetAdvancedHint(tr("%1 succeeded").arg(strName), false);
	else
		SetAdvancedHint(tr("%1 read successfully").arg(strName), false);

}

void AppController::setImageDir(const QString &strDir)
{
	if (m_bBusy) return;
	QString strTrimmed = strDir.trimmed();
	m_strImageDir = strTrimmed;
	m_pImageEntries->Clear();

	if (m_strImageDir.isEmpty()) {
		SetImageHint(tr("Select the split package directory (with parameter.txt, per-partition .img and Loader .bin)."), false);
		emit imageStateChanged();
		return;
	}

	QList<FirmwareEntry> entries;
	if (!ParseImageFolder(m_strImageDir, entries)) {
		SetImageHint(tr("parameter.txt not found or failed to parse: %1").arg(m_strImageDir), true);
		emit imageStateChanged();
		return;
	}

	QDir dir(m_strImageDir);
	QList<ImageRow> rows;
	for (int i = 0; i < entries.size(); i++) {
		const FirmwareEntry &e = entries[i];
		ImageRow row;
		row.storage = QString("-");
		row.addr = (e.lba == 0xFFFFFFFF)
		           ? QString("-")
		           : QString("0x%1").arg(e.lba, 8, 16, QChar('0'));
		row.name = e.partName;
		row.path = e.imageLike ? dir.filePath(e.fileName) : QString();
		row.selectable = e.imageLike;
		row.checked = e.imageLike;
		row.isLoader = e.isLoader;
		rows.append(row);
	}
	m_pImageEntries->SetRows(rows);

	SetImageHint(tr("Parsed %2 entries from %1; check them and click \"Execute\" to download one by one.")
	             .arg(m_strImageDir).arg(entries.size()), false);
	emit imageStateChanged();
}

void AppController::clearImage()
{
	if (m_bBusy) return;
	m_pImageEntries->Clear();
	SetImageHint(tr("Cleared."), false);
	emit imageStateChanged();
}

void AppController::executeImageDownload(bool reboot)
{
	if (m_bBusy) return;
	core::TaskJob job;
	for (const auto &row : m_pImageEntries->rows()) {
		if (!row.selectable || !row.checked || row.path.isEmpty()) continue;
		core::OperationRequest request;
		request.operation = row.isLoader ? core::Operation::LoaderUpgrade : core::Operation::DownloadImage;
		request.file.path = row.path.toStdString(); request.partition = row.name.toStdString();
		job.steps.push_back(request);
	}
	if (job.steps.empty()) { SetImageHint(tr("Check entries and specify image files first."), true); return; }
	job.rebootAfter = reboot;
	StartJob(job);
	if (m_bBusy) { m_bQueueActive = true; emit imageStateChanged(); }
}

void AppController::setFirmware(const QString &strPath)
{
	if (m_bBusy) return;
	m_strFirmware = strPath.trimmed();
	if (!m_strFirmware.isEmpty()) {
		QSettings settings(QSettings::IniFormat, QSettings::UserScope,
		                   "upgrade_tool", "upgrade_tool");
		settings.setValue(kLastFirmwareKey, m_strFirmware);
	}
	m_pFirmwareEntries->Clear();
	m_bFirmwareValid = false;
	m_strFwVersion = m_strFwLoader = m_strFwChip = m_strFwBuild = m_strFwSign = QString("-");
	emit firmwareStateChanged();

	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint(tr("Select firmware."), false);
		return;
	}

	QList<FirmwareEntry> entries;
	FirmwareMeta meta;
	if (!ParseFirmware(m_strFirmware, entries, meta)) {
		SetFirmwareHint(tr("Invalid firmware."), true);
		return;
	}

	m_strFwVersion = meta.version;
	m_strFwLoader = meta.loaderVersion;
	m_strFwChip = meta.chip;
	m_strFwBuild = meta.buildTime;
	m_strFwSign = meta.sign;
	m_bFirmwareValid = true;
	SetFirmwareHint(tr("Firmware loaded with %1 partitions.").arg(entries.size()), false);
	emit firmwareStateChanged();
}

void AppController::clearFirmware()
{
	setFirmware(QString());
}

void AppController::executeFirmwareUpgrade(bool bReboot)
{
	if (m_bBusy) return;
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint(tr("Select firmware first"), true);
		return;
	}
	m_bFirmwareReboot = bReboot;

	SetFirmwareHint(tr("Upgrading firmware…"), false);
	runOperation(static_cast<int>(core::Operation::FirmwareUpgrade), {{"path", m_strFirmware}, {"reboot", bReboot}});
}

void AppController::extractFirmware(const QString &strDir)
{
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint(tr("Select firmware first"), true);
		return;
	}
	if (strDir.trimmed().isEmpty())
		return;

	SetFirmwareHint(tr("Extracting firmware…"), false);
	runOperation(static_cast<int>(core::Operation::FirmwareExtract), {{"path", m_strFirmware}, {"output", strDir.trimmed()}});
}

void AppController::showFirmwareInfo()
{
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint(tr("Select firmware first"), true);
		return;
	}

	SetFirmwareHint(tr("Reading firmware info…"), false);
	runOperation(static_cast<int>(core::Operation::FirmwareInfo), {{"path", m_strFirmware}});
}


void AppController::switchToLoader()
{
	if (m_bBusy || m_bSwitching)
		return;
	if (!adbReady()) {
		emit toastRequested(tr("No ADB device detected; cannot switch."), true);
		return;
	}
	m_bSwitching = true;
	m_switchingFel = false;
	emit switchingChanged();
	SetBusy(true);
	SetAdvancedHint(tr("Switching device to Loader mode…"), false);
	SetImageHint(tr("Switching device to Loader mode…"), false);
	SetFirmwareHint(tr("Switching device to Loader mode…"), false);
	m_pAdb->SwitchToLoader();
}

void AppController::switchToFel(int mode)
{
	if (m_vendor != 1 || m_bBusy || m_bSwitching) return;
	if (mode < 0 || mode > 1 || !adbReady()) {
		emit toastRequested(tr("No ADB device detected or the switch method is invalid."), true);
		return;
	}
	m_bSwitching = true;
	m_switchingFel = true;
	emit switchingChanged();
	m_felSwitchHint = tr("Preparing to switch ADB device to FEL mode…");
	m_felSwitchError = false;
	emit felSwitchStateChanged();
	SetBusy(true);
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
	m_bSwitching = false;
	emit switchingChanged();
	if (m_switchingFel) {
		m_switchingFel = false;
		m_felSwitchHint = strMessage;
		m_felSwitchError = !bOk;
		emit felSwitchStateChanged();
		SetBusy(false);
		emit requestScan();
		emit toastRequested(strMessage, !bOk);
		return;
	}
	SetBusy(false);
	SetAdvancedHint(strMessage, !bOk);
	SetImageHint(strMessage, !bOk);
	SetFirmwareHint(strMessage, !bOk);
	emit toastRequested(strMessage, !bOk);
}

void AppController::exportComLog(const QString &strFile)
{
	if (strFile.trimmed().isEmpty())
		return;

	runOperation(static_cast<int>(core::Operation::ReadComLog), {{"output", strFile.trimmed()}});
}

void AppController::exportImage(const QString &strBegin, const QString &strCount,
                                const QString &strFile)
{
	if (strBegin.trimmed().isEmpty() || strCount.trimmed().isEmpty() ||
	    strFile.trimmed().isEmpty()) {
		SetAdvancedHint(tr("Enter the start sector and count and choose an export file"), true);
		return;
	}

	runOperation(static_cast<int>(core::Operation::ReadLba), {{"begin", strBegin}, {"count", strCount}, {"output", strFile.trimmed()}});
}

void AppController::createGpt(const QString &strInput, const QString &strOutput)
{
	if (strInput.trimmed().isEmpty() || strOutput.trimmed().isEmpty()) {
		emit toastRequested(tr("Enter the input parameter and output GPT"), true);
		return;
	}

	runOperation(static_cast<int>(core::Operation::CreateGpt), {{"path", strInput.trimmed()}, {"output", strOutput.trimmed()}});
}

void AppController::setVendor(int vendor)
{
    if(m_bBusy || vendor<0 || vendor>1 || vendor==m_vendor) return;
    m_vendorSelection[m_vendor] = m_iDeviceIndex >= 0 ? m_deviceKeys.value(m_iDeviceIndex) : QString();
    m_vendorBatch[m_vendor] = m_batchKeys; m_vendorContinue[m_vendor] = m_bBatchContinue;
    m_vendor=vendor; m_pConfig->setUsbVendor(vendor == 1 ? "1f3a" : "2207");
    m_bPermissionIgnored = false; m_bPermissionBlocked = PermissionMissing(); emit permissionChanged();
    m_devices.clear(); m_deviceKeys.clear(); m_iDeviceIndex=-1; m_strDeviceSignature=QStringLiteral("changed");
    m_batchKeys=m_vendorBatch[m_vendor]; m_bBatchContinue=m_vendorContinue[m_vendor]; m_bBatchMode=m_batchKeys.size()>1;
    emit vendorChanged(); emit deviceIndexChanged(); emit batchModeChanged();
    onDevicesReady(m_allDevices); refresh();
}
QVariantMap AppController::allwinnerSettings() const
{
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"upgrade_tool","upgrade_tool");
    settings.beginGroup("allwinner"); QVariantMap result;
    for(const auto &key:settings.allKeys()) result[key]=settings.value(key);
    return result;
}
void AppController::saveAllwinnerSettings(const QVariantMap &values)
{
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"upgrade_tool","upgrade_tool");
    settings.beginGroup("allwinner");
    for(auto it=values.begin();it!=values.end();++it) settings.setValue(it.key(),it.value());
}
QVariantList AppController::allwinnerStates() const
{
    QVariantList states;
    for (const auto &state : m_allwinner) {
        states.append(QVariantMap{{"firmware", state.firmware}, {"hint", state.hint},
            {"valid", state.valid}, {"error", state.error}, {"partitions", state.partitions},
            {"files", state.files}, {"summary", QVariant::fromValue(state.summary)},
            {"results", QVariant::fromValue(state.results)}});
    }
    return states;
}

void AppController::setAllwinnerFirmware(const QString &path)
{
    setAllwinnerContextFirmware(1, path);
}

void AppController::setAllwinnerContextFirmware(int context, const QString &path)
{
    if(m_bBusy || m_vendor!=1 || context<0 || context>2) return;
    auto &state=m_allwinner[context];
    const QString trimmed=path.trimmed();
    if(state.firmware==trimmed && state.inspected && state.valid) return;
    state.firmware=trimmed; state.valid=false; state.inspected=false; state.error=false;
    state.hint.clear(); state.files.clear(); state.partitions.clear();
    state.summary->Clear(); state.results->Clear();
    saveAllwinnerSettings({{QString("page%1/firmware").arg(context),state.firmware}});
    emit allwinnerChanged();
    ensureAllwinnerContext(context);
}

void AppController::inspectAllwinnerFirmware(int context)
{
    if(m_vendor!=1 || context<0 || context>2) return;
    auto &state=m_allwinner[context];
    if(state.firmware.isEmpty()) return;
    state.inspected=true;
    core::OperationResult result;
    result.operation=core::Operation::AllwinnerInspect;
    try {
        result=core::allwinner::InspectFirmware(state.firmware.toStdString());
    } catch(const std::exception &e) {
        result.operation=core::Operation::AllwinnerInspect;
        result.status=core::OperationStatus::Failed;
        result.diagnostic=e.what();
    }
    ApplyAllwinnerInspectResult(context, result);
}

void AppController::ensureAllwinnerContext(int context)
{
    if(m_bBusy || m_vendor!=1 || context<0 || context>2) return;
    auto &state=m_allwinner[context];
    if(!state.inspected && !state.firmware.isEmpty())
        inspectAllwinnerFirmware(context);
}

void AppController::runAllwinner(int operation,const QVariantMap &p)
{
    if(m_bBusy || m_vendor!=1 || operation<static_cast<int>(core::Operation::AllwinnerFlash) || operation>=static_cast<int>(core::Operation::Count)) return;
    core::OperationRequest r; r.operation=static_cast<core::Operation>(operation); r.vendor=core::Vendor::Allwinner;
    bool contextOk=true;
    const int context=r.operation==core::Operation::AllwinnerDeviceInfo ? 3 : p.value("context",1).toInt(&contextOk);
    if(!contextOk || context<0 || context>3 || (context==3 && r.operation!=core::Operation::AllwinnerDeviceInfo)) return;
    auto &state=m_allwinner[context];
    r.file.path=p.value("path",state.firmware).toString().trimmed().toStdString();
    r.file.output=p.value("output").toString().trimmed().toStdString();
    auto &o=r.allwinner; bool valid=true;
    auto choice=[&](const char *key,int fallback,int maximum) { bool ok=true; int value=p.value(key,fallback).toInt(&ok); if(!ok||value<0||value>maximum) valid=false; return value; };
    auto number=[&](const char *key,uint64_t &value,bool *present=nullptr) {
        QString text=p.value(key).toString().trimmed(); if(text.isEmpty()) return;
        bool ok; auto n=text.toULongLong(&ok,text.startsWith("0x",Qt::CaseInsensitive)?16:10);
        if(!ok||text.startsWith('-')||n>uint64_t(INT64_MAX)) valid=false;
        else { value=n; if(present) *present=true; }
    };
    switch(r.operation) {
    case core::Operation::AllwinnerFlash:
        o.mode=static_cast<core::AllwinnerFlashMode>(choice("mode",3,3));
        o.postAction=static_cast<core::PostAction>(choice("postAction",0,2));
        o.verify=p.value("verify",true).toBool();
        if(o.mode==core::AllwinnerFlashMode::Partition) {
            for(const auto &name:p.value("partitions").toStringList()) o.partitions.push_back(name.toStdString());
            if(o.partitions.empty()) valid=false;
        }
        break;
    case core::Operation::AllwinnerRaw:
        o.rawMode=static_cast<core::RawMode>(choice("rawMode",1,1));
        o.rawStorage=static_cast<core::RawStorage>(choice("rawStorage",0,2));
        o.rawImage=p.value("rawImage").toString().trimmed().toStdString();
        if(o.rawImage.empty()) valid=false;
        if(o.rawMode==core::RawMode::LogicOffset) number("logicOffset",o.logicOffset,&o.hasLogicOffset);
        break;
    case core::Operation::AllwinnerConvert:
        o.convertTarget=static_cast<core::ConvertTarget>(choice("target",2,4));
        number("logicOffset",o.logicOffset,&o.hasLogicOffset);
        if(o.convertTarget==core::ConvertTarget::Spinor) {
            number("ubootOffset",o.ubootOffset,&o.hasUbootOffset); number("norMiB",o.norMiB);
        } else {
            o.secure=static_cast<core::SecureMode>(choice("secure",0,2));
            o.storageSize=p.value("storageSize","auto").toString().toStdString();
        }
        if(r.file.output.empty()) valid=false;
        break;
    case core::Operation::AllwinnerExtract:
        if(r.file.output.empty()) valid=false;
        break;
    default: break;
    }
    if(r.operation!=core::Operation::AllwinnerDeviceInfo && r.operation!=core::Operation::AllwinnerReboot && r.file.path.empty()) valid=false;
    if(!valid) { state.error=true; state.hint=tr("Check the file paths and parameters for this operation; numbers must be in range"); emit allwinnerChanged(); emit toastRequested(state.hint,true); return; }
    if(!core::IsLocalOperation(r.operation) && !hasDevice()) {
        state.error=true; state.hint=tr("Connect and select an Allwinner device first"); emit allwinnerChanged(); emit toastRequested(state.hint,true); return;
    }
    m_allwinnerContext=context;
    if(r.operation==core::Operation::AllwinnerInspect) state.inspected=true;
    state.error=false; state.hint=tr("Running: %1").arg(OperationName(r.operation));
    if(r.operation!=core::Operation::AllwinnerInspect) state.results->Clear();
    emit allwinnerChanged();
    core::TaskJob job; job.steps.push_back(r); StartJob(job);
    if(!m_bBusy) {
        state.error=true; state.hint=tr("Select an online Allwinner device first"); emit allwinnerChanged();
    }
}

}
