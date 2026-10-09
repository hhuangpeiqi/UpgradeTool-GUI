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
#include "gui/backend/LogModel.h"
#include "gui/backend/ThemeManager.h"
#include "core/UpgradeObserver.h"
#include "core/Version.h"
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
	case core::ErrorCode::InvalidInput: return QObject::tr("参数无效或超出范围");
	case core::ErrorCode::DeviceMissing: return QObject::tr("目标设备已断开，请重新选择设备");
	case core::ErrorCode::DeviceAmbiguous: return QObject::tr("设备身份不明确，请重新选择设备");
	case core::ErrorCode::FileAccess: return QObject::tr("无法打开文件，请检查路径和权限");
	case core::ErrorCode::InvalidFirmware: return QObject::tr("固件格式或校验无效");
	case core::ErrorCode::UnsupportedDevice: return QObject::tr("当前设备模式不支持此操作");
	default: return QObject::tr("操作失败，详见日志");
	}
}

QString PromptText(int prompt, bool &failed)
{
	failed = false;
	switch (prompt) {
	case PROMPT_DOWNLOAD_BOOT_START: return QObject::tr("开始下载 Boot");
	case PROMPT_DOWNLOAD_BOOT_FAIL: failed = true; return QObject::tr("下载 Boot 失败");
	case PROMPT_DOWNLOAD_BOOT_SUCCESS: return QObject::tr("下载 Boot 成功");
	case PROMPT_DOWNLOAD_IDB_START: return QObject::tr("开始下载 IDB");
	case PROMPT_DOWNLOAD_IDB_FAIL: failed = true; return QObject::tr("下载 IDB 失败");
	case PROMPT_DOWNLOAD_IDB_SUCCESS: return QObject::tr("下载 IDB 成功");
	case PROMPT_DOWNLOAD_FIRMWARE_START: return QObject::tr("开始下载固件");
	case PROMPT_DOWNLOAD_FIRMWARE_FAIL: failed = true; return QObject::tr("下载固件失败");
	case PROMPT_DOWNLOAD_FIRMWARE_SUCCESS: return QObject::tr("下载固件成功");
	case PROMPT_TEST_DEVICE_START: return QObject::tr("开始检测设备");
	case PROMPT_TEST_DEVICE_FAIL: failed = true; return QObject::tr("检测设备失败");
	case PROMPT_TEST_DEVICE_SUCCESS: return QObject::tr("检测设备成功");
	case PROMPT_RESET_DEVICE_START: return QObject::tr("正在重启设备");
	case PROMPT_RESET_DEVICE_FAIL: failed = true; return QObject::tr("重启设备失败");
	case PROMPT_RESET_DEVICE_SUCCESS: return QObject::tr("重启设备成功");
	case PROMPT_WAIT_MSC_START: return QObject::tr("等待进入 MSC 模式");
	case PROMPT_WAIT_MSC_FAIL: failed = true; return QObject::tr("等待 MSC 模式失败");
	case PROMPT_WAIT_MSC_SUCCESS: return QObject::tr("已进入 MSC 模式");
	case PROMPT_WAIT_LOADER_START: return QObject::tr("等待进入 Loader 模式");
	case PROMPT_WAIT_LOADER_FAIL: failed = true; return QObject::tr("等待 Loader 模式失败");
	case PROMPT_WAIT_LOADER_SUCCESS: return QObject::tr("已进入 Loader 模式");
	case PROMPT_WAIT_MASKROM_START: return QObject::tr("等待进入 Maskrom 模式");
	case PROMPT_WAIT_MASKROM_FAIL: failed = true; return QObject::tr("等待 Maskrom 模式失败");
	case PROMPT_WAIT_MASKROM_SUCCESS: return QObject::tr("已进入 Maskrom 模式");
	case PROMPT_ERASE_IDB_START: return QObject::tr("开始擦除 IDB");
	case PROMPT_ERASE_IDB_FAIL: failed = true; return QObject::tr("擦除 IDB 失败");
	case PROMPT_ERASE_IDB_SUCCESS: return QObject::tr("擦除 IDB 成功");
	case PROMPT_SWITCH_MSC_START: return QObject::tr("开始切换到 MSC 模式");
	case PROMPT_SWITCH_MSC_FAIL: failed = true; return QObject::tr("切换到 MSC 模式失败");
	case PROMPT_SWITCH_MSC_SUCCESS: return QObject::tr("已切换到 MSC 模式");
	case PROMPT_CHECK_CHIP_START: return QObject::tr("开始校验芯片");
	case PROMPT_CHECK_CHIP_FAIL: failed = true; return QObject::tr("校验芯片失败");
	case PROMPT_CHECK_CHIP_SUCCESS: return QObject::tr("校验芯片成功");
	case PROMPT_PREPARE_IDB_START: return QObject::tr("开始准备 IDB");
	case PROMPT_PREPARE_IDB_FAIL: failed = true; return QObject::tr("准备 IDB 失败");
	case PROMPT_PREPARE_IDB_SUCCESS: return QObject::tr("准备 IDB 完成");
	case PROMPT_ERASE_FLASH_START: return QObject::tr("开始擦除 Flash");
	case PROMPT_ERASE_FLASH_FAIL: failed = true; return QObject::tr("擦除 Flash 失败");
	case PROMPT_ERASE_FLASH_SUCCESS: return QObject::tr("擦除 Flash 成功");
	case PROMPT_GET_BLOCK_STATE_START: return QObject::tr("读取块状态");
	case PROMPT_GET_BLOCK_STATE_FAIL: failed = true; return QObject::tr("读取块状态失败");
	case PROMPT_GET_BLOCK_STATE_SUCCESS: return QObject::tr("读取块状态成功");
	case PROMPT_GET_FLASH_INFO_START: return QObject::tr("读取 Flash 信息");
	case PROMPT_GET_FLASH_INFO_FAIL: failed = true; return QObject::tr("读取 Flash 信息失败");
	case PROMPT_GET_FLASH_INFO_SUCCESS: return QObject::tr("读取 Flash 信息成功");
	case PROMPT_LOWER_FORMAT_START: return QObject::tr("开始低级格式化");
	case PROMPT_LOWER_FORMAT_FAIL: failed = true; return QObject::tr("低级格式化失败");
	case PROMPT_LOWER_FORMAT_SUCCESS: return QObject::tr("低级格式化成功");
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
	connect(m_pObserver, &QtObserver::progressReceived, this, &AppController::onProgress);
	connect(m_pObserver, &QtObserver::upgradePromptReceived, this, &AppController::onUpgradePrompt);
	connect(m_pObserver, &QtObserver::storageSelectionRequested, this, &AppController::onStorageSelection);
	connect(m_pObserver, &QtObserver::resultReceived, this, &AppController::onOperationResult);

	connect(m_pAdb, SIGNAL(finished(bool, QString)),
	        this, SLOT(onAdbFinished(bool, QString)));
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

	SetImageHint(tr("请选择分体包目录（内含 parameter.txt、各分区 .img 与 Loader .bin）。"), false);
	SetFirmwareHint(tr("请选择固件。"), false);
	SetAdvancedHint(tr("就绪"), false);
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
		AppendLog(QString("[%1] %2").arg(strStamp).arg(tr("应用已启动")), false);
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
		static QVariantMap Run(const QString &strTitle, const QString &strNote,
		                       const QVariantList &items)
		{
			QVariantMap map;
			map["title"] = strTitle;
			map["note"] = strNote;
			map["items"] = items;
			return map;
		}
	};

#ifdef Q_OS_LINUX
	const QString connectionNote = tr("设备需处于 Rockusb 模式（Loader 或 Maskrom），且系统已安装 udev 规则。");
	const QString connectionHelp = tr("底部状态栏会自动扫描设备；没有出现时点击「刷新」，并确认设备模式与 udev 权限。");
	const QString preferencesHelp = tr("编辑并保存 config.ini、重新加载、一键安装 udev 规则。");
	const QString missingHelp = tr("确认设备处于 Rockusb 模式（Loader 或 Maskrom），并已安装 udev 规则后重新插拔。");
#else
	const QString connectionNote = tr("设备需处于 Rockusb 模式（Loader 或 Maskrom）。");
	const QString connectionHelp = tr("底部状态栏会自动扫描设备；没有出现时点击「刷新」，并确认设备模式、USB 连接以及是否被其他程序占用。");
	const QString preferencesHelp = tr("编辑并保存 config.ini、重新加载。");
	const QString missingHelp = tr("确认设备处于 Rockusb 模式（Loader 或 Maskrom），检查 USB 连接并关闭占用设备的其他程序后重新插拔。");
#endif
	QVariantList groups;

	groups.append(MakeGroup::Run(tr("快速上手"),
		connectionNote,
		QVariantList()
		<< MakeItem::Run(tr("① 连接设备"), connectionHelp)
		<< MakeItem::Run(tr("② 选择设备"), tr("在底部设备栏的下拉框中选择目标设备；要同时烧录多台，用标题栏「设备 → 批量选择…」。"))
		<< MakeItem::Run(tr("③ 执行操作"), tr("切到对应功能页，选好文件或参数后点击「执行」/「升级」；进度、速率与预计剩余显示在底部，随时可「取消」。"))
		<< MakeItem::Run(tr("④ 查看结果"), tr("中文结论显示在页面提示区，引擎原始输出写入日志文件（「帮助 → 日志」查看目录）。"))));

	groups.append(MakeGroup::Run(tr("下载镜像"),
		tr("按分体包目录内的 parameter.txt 分区表逐分区下载。"), QVariantList()
		<< MakeItem::Run(tr("浏览…"), tr("选择分体包目录，目录内需包含 parameter.txt、各分区镜像与 Loader。"))
		<< MakeItem::Run(tr("路径列"), tr("双击某一行可手动指定该分区的镜像文件。"))
		<< MakeItem::Run(tr("勾选框"), tr("勾选要下载的分区；表头勾选框可全选或全不选。"))
		<< MakeItem::Run(tr("切换"), tr("把 ADB 模式的设备切到 Loader 模式（主机需安装 ADB）。"))
		<< MakeItem::Run(tr("清空"), tr("清除当前目录与分区选择。"))
		<< MakeItem::Run(tr("烧录完成后重启设备"), tr("队列结束后自动重启设备。"))
		<< MakeItem::Run(tr("执行"), tr("按勾选的分区逐个下载镜像。"))));

	groups.append(MakeGroup::Run(tr("升级固件"),
		tr("整包固件升级，界面会解析并显示固件信息。"), QVariantList()
		<< MakeItem::Run(tr("浏览…"), tr("选择 .img 整包固件。"))
		<< MakeItem::Run(tr("固件信息"), tr("显示固件版本、Loader 版本、芯片信息、构建时间与签名。"))
		<< MakeItem::Run(tr("查看固件信息"), tr("重新解析并显示固件信息。"))
		<< MakeItem::Run(tr("解包固件…"), tr("把固件解包到指定目录。"))
		<< MakeItem::Run(tr("烧录完成后重启设备"), tr("勾选表示升级完成后自动重启设备。"))
		<< MakeItem::Run(tr("升级"), tr("整包烧录；不支持按分区过滤。"))));

	groups.append(MakeGroup::Run(tr("高级功能"),
		tr("需要具体参数的设备操作，结果回填到结果表。"), QVariantList()
		<< MakeItem::Run(tr("读取类按钮"), tr("读取 Flash ID / Flash 信息 / Chip 信息 / Capability / 安全模式 / OTP / 序列号。"))
		<< MakeItem::Run(tr("测试设备 / 重启设备 / 切换存储 / 切换设备"), tr("对当前设备执行对应操作。"))
		<< MakeItem::Run(tr("下载 Boot"), tr("配合上方「Boot」文件路径下载 Boot。"))
		<< MakeItem::Run(tr("擦除所有"), tr("擦除设备 Flash。"))
		<< MakeItem::Run(tr("导出串口日志"), tr("把当前设备的串口日志保存到文件。"))
		<< MakeItem::Run(tr("写入序列号"), tr("把「序列号」输入框中的内容写入设备。"))
		<< MakeItem::Run(tr("LBA / 扇区"), tr("输入起始扇区与扇区数后，可「导出镜像」或「擦除扇区」。"))
		<< MakeItem::Run(tr("专业操作参数"), tr("写入 LBA / 写入扇区 / 擦除块 / 运行系统 / 重置管道，需填写各自参数。"))));

	groups.append(MakeGroup::Run(tr("配置与设备菜单"),
		tr("标题栏右侧的下拉菜单。"), QVariantList()
		<< MakeItem::Run(tr("配置 → 首选项…"), preferencesHelp)
		<< MakeItem::Run(tr("配置 → 生成 GPT…"), tr("由 parameter.txt 生成 GPT 镜像。"))
		<< MakeItem::Run(tr("配置 → 外观"), tr("在 跟随系统 / 浅色 / 深色 之间切换主题。"))
		<< MakeItem::Run(tr("设备 → 刷新"), tr("重新扫描当前连接的设备。"))
		<< MakeItem::Run(tr("设备 → 批量选择…"), tr("勾选多台设备，按顺序执行同一操作，可勾选「失败继续」。"))));

	groups.append(MakeGroup::Run(tr("常见问题"), QString(), QVariantList()
		<< MakeItem::Run(tr("底部一直没有设备"), missingHelp)
		<< MakeItem::Run(tr("「切换」按钮置灰"), tr("主机未安装 ADB，或未检测到 ADB 设备。"))
		<< MakeItem::Run(tr("按钮置灰并提示不支持"), tr("提示「当前引擎不支持该功能」，表示该操作暂未实现。"))
		<< MakeItem::Run(tr("执行中点「取消」"), tr("属于「已取消」而非失败，会停止当前操作与后续队列。"))));

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

	addPlace(tr("主目录"), QDir::homePath());
	addPlace(tr("桌面"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
	addPlace(tr("文档"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	addPlace(tr("下载"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
	addPlace(tr("图片"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
	addPlace(tr("根目录"), QStringLiteral("/"));

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
		m_strDeviceStatus = tr("批量: 已选 %1 台 / 当前 %2 台")
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
		             ? tr("发现 %1 设备").arg(strModes[i])
		             : tr("发现 %1 台 %2 设备").arg(iCount).arg(strModes[i]));
	}

	// 没有 rockusb 设备时，才提示 ADB 状态
	if (strParts.isEmpty()) {
		if (!m_bAdbKnown)
			strParts << tr("检测 ADB 中…");
		else if (!m_bAdbAvailable)
			strParts << tr("未检测到 ADB 环境");
		else if (!m_strAdbDevices.isEmpty())
			strParts << (m_strAdbDevices.size() == 1
			             ? tr("发现 ADB 设备")
			             : tr("发现 %1 台 ADB 设备").arg(m_strAdbDevices.size()));
	}

	m_strDeviceStatus = strParts.isEmpty() ? tr("未发现设备")
	                                       : strParts.join(tr("、"));
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


void AppController::onThreadStarted()
{
	refresh();
}

void AppController::onDevicesReady(const QVariantList &devices)
{
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

	QString strPrevious;
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
	m_bCancelRequested = true;
	m_job.cancelled->store(true);
	m_pObserver->CancelSelection();
	AppendLog(tr("正在取消…"), false);
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
		if (targets.isEmpty()) { emit toastRequested(tr("请先选择设备"), true); return; }
		for (const auto &value : targets) {
			QVariantMap map = value.toMap();
			core::DeviceIdentity identity;
			identity.serial = map["serial"].toString().toStdString();
			identity.location = map["locationId"].toUInt();
			job.targets.push_back(identity);
		}
	}
	job.continueOnError = m_bBatchMode && m_bBatchContinue;
	m_job = job;
	m_runningOperation = job.steps.front().operation;
	m_bCancelRequested = false;
	m_pObserver->BeginSelectionSession();
	SetBusy(true); ResetProgress();
	AppendLog(tr("开始：%1").arg(OperationName(m_runningOperation)), false);
	emit requestExecute(m_job);
}

void AppController::runOperation(int operation, const QVariantMap &parameters)
{
	if (m_bBusy) return;
	core::OperationRequest r;
	r.operation = static_cast<core::Operation>(operation);
	if (operation < 0 || operation >= static_cast<int>(core::Operation::Count)) return;
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
	if (!valid) { SetAdvancedHint(tr("数值参数无效或超出范围"), true); emit toastRequested(tr("数值参数无效或超出范围"), true); return; }
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
		emit toastRequested(tr("批量模式：已选 %1 台").arg(strAlive.size()), false);
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
		emit toastRequested(tr("已选择单台设备"), false);
	} else {
		emit toastRequested(tr("未选择设备"), false);
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
	QString text = cancelled ? tr("已取消") : ok ? tr("操作完成") : tr("操作失败，详见日志");
	AppendLog(text, !ok && !cancelled);
	if (m_bQueueActive) {
		m_bQueueActive = false;
		SetImageHint(cancelled ? tr("已取消。") : ok ?
			(m_job.rebootAfter ? tr("下载完成，已发送重启指令。") : tr("下载完成。")) : tr("下载结束，存在失败。失败设备未自动重启。"), !ok && !cancelled);
		emit imageStateChanged();
	} else if (cancelled) {
		if (m_runningOperation == core::Operation::FirmwareUpgrade || m_runningOperation == core::Operation::FirmwareExtract || m_runningOperation == core::Operation::FirmwareInfo)
			SetFirmwareHint(text, false);
		else SetAdvancedHint(text, false);
	}
	if (m_job.targets.size() > 1) {
		QString batch = tr("批量结束：成功 %1 台，失败 %2 台").arg(summary.succeeded).arg(summary.failed);
		if (cancelled) batch += tr("，已取消后续操作");
		AppendLog(batch, summary.failed > 0);
		if (m_runningOperation == core::Operation::FirmwareUpgrade) SetFirmwareHint(batch, !ok && !cancelled);
		else if (m_runningOperation != core::Operation::LoaderUpgrade && m_runningOperation != core::Operation::DownloadImage) SetAdvancedHint(batch, !ok && !cancelled);
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
		QString text = tr("正在执行 %1/%2：%3").arg(step + 1).arg(m_job.steps.size()).arg(label);
		if (total > 1) text += tr("，设备 %1/%2 %3").arg(target + 1).arg(total).arg(device);
		if (m_bQueueActive) SetImageHint(text, false);
		AppendLog(text, false);
	}
}

void AppController::onOutput(int iLevel, const QString &strText)
{
	// 引擎原始输出只写入日志文件（界面不再显示），仅保留错误级供排查。
	if (iLevel == (int)core::LogLevel::Error)
		AppendLog(strText, true);
}

void AppController::onProgress(const QString &strPrompt, long long llTotal,
                               long long llCurrent, int iCall)
{
	if (llTotal <= 0)
		return;
	int iPct = (int)(llCurrent * 100 / llTotal);
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
				QString strEta = tr("  剩余 %1").arg(FormatEta((llTotal - llCurrent) / dRate));
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


void AppController::onOperationResult(const core::OperationResult &result)
{
	bool bOk = result.ok();
	core::Operation id = result.operation;
	QVariantList items;
	for (const auto &row : result.items) { QVariantMap item; item["label"] = QString::fromStdString(row.label); item["value"] = QString::fromStdString(row.value); items.append(item); }
	if (id == core::Operation::FirmwareInfo) {
		for (const auto &entry : result.firmware.entries) { QVariantMap item; item["label"] = QString::fromStdString(entry.partName); item["value"] = tr("%1，大小 %2，LBA 0x%3").arg(QString::fromStdString(entry.fileName)).arg(entry.size).arg(entry.lba, 0, 16); items.append(item); }
	}

	if (id == core::Operation::DownloadImage || id == core::Operation::LoaderUpgrade) {
		if (!bOk && !m_bCancelRequested) {
			QString strReason = ErrorText(result.error);
			SetImageHint(strReason.isEmpty() ? tr("下载失败，详见日志。")
			                                 : tr("下载失败：%1").arg(strReason), true);
		}

		return;
	}

	if (id == core::Operation::ResetDevice) {
		SetImageHint(bOk ? tr("已发送重启指令。") : tr("重启设备失败。"), !bOk);
		m_pResults->SetItems(items);
		SetAdvancedHint(bOk ? tr("重启设备成功") : tr("重启设备失败"), !bOk);

		return;
	}

	if (id == core::Operation::FirmwareUpgrade) {
		if (bOk)
			SetFirmwareHint(m_bFirmwareReboot ? tr("升级成功，设备已重启。")
			                                  : tr("升级成功，设备未重启。"), false);
		else if (!m_bCancelRequested) {
			QString strReason = ErrorText(result.error);
			SetFirmwareHint(strReason.isEmpty() ? tr("升级失败，详见日志。")
			                                    : tr("升级失败：%1").arg(strReason), true);
		}

		return;
	}

	if (id == core::Operation::FirmwareExtract) {
		SetFirmwareHint(bOk ? tr("解包成功。") : tr("解包失败。"), !bOk);

		return;
	}

	if (id == core::Operation::FirmwareInfo) {
		m_pFirmwareEntries->Clear();
		if (!bOk) {
			SetFirmwareHint(tr("读取固件信息失败。"), true);

			return;
		}
		m_pFirmwareEntries->SetItems(items);
		m_strFwChip = QString::fromStdString(result.firmware.chip);
		m_strFwVersion = QString::fromStdString(result.firmware.version);
		m_strFwLoader = QString::fromStdString(result.firmware.loaderVersion);
		m_strFwBuild = QString::fromStdString(result.firmware.buildTime);
		m_strFwSign = QString::fromStdString(result.firmware.sign);
		SetFirmwareHint(tr("固件信息读取成功。"), false);
		emit firmwareStateChanged();

		return;
	}

	m_pResults->SetItems(items);
	QString strName = OperationName(id);
	if (!bOk)
		SetAdvancedHint(tr("%1 失败：%2").arg(strName, ErrorText(result.error)), true);
	else if (items.isEmpty())
		SetAdvancedHint(tr("%1 成功").arg(strName), false);
	else
		SetAdvancedHint(tr("%1 读取成功").arg(strName), false);

}

void AppController::setImageDir(const QString &strDir)
{
	if (m_bBusy) return;
	QString strTrimmed = strDir.trimmed();
	m_strImageDir = strTrimmed;
	m_pImageEntries->Clear();

	if (m_strImageDir.isEmpty()) {
		SetImageHint(tr("请选择分体包目录（内含 parameter.txt、各分区 .img 与 Loader .bin）。"), false);
		emit imageStateChanged();
		return;
	}

	QList<FirmwareEntry> entries;
	if (!ParseImageFolder(m_strImageDir, entries)) {
		SetImageHint(tr("未找到 parameter.txt 或解析失败：%1").arg(m_strImageDir), true);
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

	SetImageHint(tr("已从 %1 解析 %2 个条目，勾选后点「执行」逐条下载。")
	             .arg(m_strImageDir).arg(entries.size()), false);
	emit imageStateChanged();
}

void AppController::clearImage()
{
	if (m_bBusy) return;
	m_pImageEntries->Clear();
	SetImageHint(tr("已清空。"), false);
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
	if (job.steps.empty()) { SetImageHint(tr("请先勾选条目并指定镜像文件。"), true); return; }
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
		SetFirmwareHint(tr("请选择固件。"), false);
		return;
	}

	QList<FirmwareEntry> entries;
	FirmwareMeta meta;
	if (!ParseFirmware(m_strFirmware, entries, meta)) {
		SetFirmwareHint(tr("固件无效。"), true);
		return;
	}

	m_strFwVersion = meta.version;
	m_strFwLoader = meta.loaderVersion;
	m_strFwChip = meta.chip;
	m_strFwBuild = meta.buildTime;
	m_strFwSign = meta.sign;
	m_bFirmwareValid = true;
	SetFirmwareHint(tr("固件已加载，共 %1 个分区。").arg(entries.size()), false);
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
		SetFirmwareHint(tr("请先选择固件"), true);
		return;
	}
	m_bFirmwareReboot = bReboot;

	SetFirmwareHint(tr("正在升级固件…"), false);
	runOperation(static_cast<int>(core::Operation::FirmwareUpgrade), {{"path", m_strFirmware}, {"reboot", bReboot}});
}

void AppController::extractFirmware(const QString &strDir)
{
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint(tr("请先选择固件"), true);
		return;
	}
	if (strDir.trimmed().isEmpty())
		return;

	SetFirmwareHint(tr("正在解包固件…"), false);
	runOperation(static_cast<int>(core::Operation::FirmwareExtract), {{"path", m_strFirmware}, {"output", strDir.trimmed()}});
}

void AppController::showFirmwareInfo()
{
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint(tr("请先选择固件"), true);
		return;
	}

	SetFirmwareHint(tr("正在读取固件信息…"), false);
	runOperation(static_cast<int>(core::Operation::FirmwareInfo), {{"path", m_strFirmware}});
}


void AppController::switchToLoader()
{
	if (m_bSwitching)
		return;
	if (!adbReady()) {
		emit toastRequested(tr("未检测到 ADB 设备，无法切换。"), true);
		return;
	}
	m_bSwitching = true;
	SetAdvancedHint(tr("正在将设备切换到 Loader 模式…"), false);
	SetImageHint(tr("正在将设备切换到 Loader 模式…"), false);
	SetFirmwareHint(tr("正在将设备切换到 Loader 模式…"), false);
	m_pAdb->SwitchToLoader();
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
	m_bSwitching = false;
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
		SetAdvancedHint(tr("请输入起始扇区、扇区数并选择导出文件"), true);
		return;
	}

	runOperation(static_cast<int>(core::Operation::ReadLba), {{"begin", strBegin}, {"count", strCount}, {"output", strFile.trimmed()}});
}

void AppController::createGpt(const QString &strInput, const QString &strOutput)
{
	if (strInput.trimmed().isEmpty() || strOutput.trimmed().isEmpty()) {
		emit toastRequested(tr("请填写输入 parameter 与输出 GPT"), true);
		return;
	}

	runOperation(static_cast<int>(core::Operation::CreateGpt), {{"path", strInput.trimmed()}, {"output", strOutput.trimmed()}});
}

}
