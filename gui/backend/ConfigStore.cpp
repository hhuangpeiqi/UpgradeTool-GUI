#include "gui/backend/ConfigStore.h"

#include <memory>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include "application/ConfigService.h"
#include "platform/PlatformPath.h"

namespace gui {

namespace {

const char *kKeys[] = {
	"lba_parity", "rb_check_off", "nor_single_idb", "log_off",
	"stdout_buffer_off", "ui_log_details", "force_data_band", "log_dir",
	"firmware", "loader", "device"
};

bool IsKey(const QString &strName)
{
	for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++)
		if (strName == kKeys[i])
			return true;
	return false;
}

#ifdef Q_OS_LINUX
QString FindUdevRule(const QString &name)
{
	QString strDir = QString::fromStdString(platform::GetExecutableDir());
	QStringList strCandidates;
	strCandidates << strDir + "/scripts/" + name;
	strCandidates << strDir + "/../scripts/" + name;
	strCandidates << "/usr/share/upgrade_tool/" + name;
	for (int i = 0; i < strCandidates.size(); i++) {
		if (QFileInfo(strCandidates[i]).isFile())
			return QFileInfo(strCandidates[i]).absoluteFilePath();
	}
	return QString();
}

const char *kUdevRuleDirs[] = {
	"/etc/udev/rules.d",
	"/run/udev/rules.d",
	"/usr/lib/udev/rules.d",
	"/lib/udev/rules.d"
};

bool RuleGrantsUsbAccess(const QString &strText, const QString &vendor)
{
	const QRegularExpression reVendor("idVendor\\}\\s*==\\s*\"" + vendor + "\"");
	const QStringList strLines = strText.split('\n');
	for (int i = 0; i < strLines.size(); i++) {
		QString strLine = strLines[i];
		int iComment = strLine.indexOf('#');
		if (iComment >= 0)
			strLine = strLine.left(iComment);
		if (!reVendor.match(strLine).hasMatch())
			continue;
		if (strLine.contains("MODE=") || strLine.contains("GROUP="))
			return true;
	}
	return false;
}

#endif

QString Key(const QVariantMap &values, const char *pszKey)
{
	return values.value(pszKey).toString().trimmed();
}

bool Flag(const QVariantMap &values, const char *pszKey)
{
	return values.value(pszKey).toBool();
}

}

ConfigStore::ConfigStore(QObject *parent)
	: QObject(parent)
{
	std::string strExisting = platform::FindConfigFile();
	m_strFile = strExisting.empty()
	            ? QString::fromStdString(platform::GetUserConfigFile())
	            : QString::fromStdString(strExisting);
    try { m_snapshot=core::ReadConfig(m_strFile.toStdString()); } catch(const std::exception &) {}
}

void ConfigStore::SetStatus(TranslatableText::Factory factory)
{
	m_status.Set(std::move(factory));
	emit statusChanged();
}

void ConfigStore::retranslate()
{
	m_status.retranslate();
	emit statusChanged();
}

QVariantMap ConfigStore::load()
{
	const bool previousDetails = uiLogDetails();
	try { m_snapshot=core::ReadConfig(m_strFile.toStdString()); }
    catch(const std::exception &error) { SetStatus([text=QString::fromUtf8(error.what())] { return text; }); return {}; }
	if (previousDetails != uiLogDetails()) emit uiLogDetailsChanged();

	QVariantMap map;
    for(const auto *key:{"lba_parity","rb_check_off","nor_single_idb","log_off","stdout_buffer_off","ui_log_details"}) map[key]=m_snapshot.Flag(key);
    for(const auto *key:{"force_data_band","log_dir","firmware","loader","device"}) map[key]=QString::fromStdString(m_snapshot.String(key));

	SetStatus([this] { return tr("Loaded: %1").arg(m_strFile); });
	return map;
}

bool ConfigStore::save(const QVariantMap &values)
{
	QStringList strUnknown;
	QFile in(m_strFile);
	if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream stream(&in);
		while (!stream.atEnd()) {
			QString strLine = stream.readLine().trimmed();
			if (strLine.isEmpty() || strLine.startsWith('#'))
				continue;
			int iEq = strLine.indexOf('=');
			if (iEq < 0) {
				strUnknown << strLine;
				continue;
			}
			if (!IsKey(strLine.left(iEq).trimmed()))
				strUnknown << strLine;
		}
		in.close();
	}

	QString strBand = Key(values, "force_data_band");
	QString strLogDir = Key(values, "log_dir");
	QString strFirmware = Key(values, "firmware");
	QString strLoader = Key(values, "loader");
	QString strDevice = Key(values, "device");

	QStringList strNew;
	strNew << "# upgrade_tool configuration (managed by the GUI)";
	strNew << (Flag(values, "lba_parity")          ? "lba_parity=true"          : "#lba_parity=");
	strNew << (Flag(values, "rb_check_off")        ? "rb_check_off=true"        : "#rb_check_off=");
	strNew << (Flag(values, "nor_single_idb")      ? "nor_single_idb=true"      : "#nor_single_idb=");
	strNew << (Flag(values, "log_off")             ? "log_off=true"             : "#log_off=");
	strNew << (Flag(values, "stdout_buffer_off")   ? "stdout_buffer_off=true"   : "#stdout_buffer_off=");
	strNew << (Flag(values, "ui_log_details")      ? "ui_log_details=true"      : "ui_log_details=false");
	strNew << (strBand.isEmpty()     ? "#force_data_band=" : "force_data_band=" + strBand);
	strNew << (strLogDir.isEmpty()   ? "#log_dir="         : "log_dir=" + strLogDir);
	strNew << (strFirmware.isEmpty() ? "#firmware="        : "firmware=" + strFirmware);
	strNew << (strLoader.isEmpty()   ? "#loader="          : "loader=" + strLoader);
	strNew << (strDevice.isEmpty()   ? "#device="          : "device=" + strDevice);
	strNew += strUnknown;

	platform::EnsureDirRecursive(platform::GetUserConfigDir());
	QString strTarget = QString::fromStdString(platform::GetUserConfigFile());

	QFile out(strTarget);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		SetStatus([strTarget] { return tr("Save failed: cannot write %1").arg(strTarget); });
		return false;
	}
	QTextStream stream(&out);
	for (int i = 0; i < strNew.size(); i++)
		stream << strNew[i] << "\n";
	out.close();

	m_strFile = strTarget;
	emit filePathChanged();
	const bool previousDetails = uiLogDetails();
	m_snapshot=core::ReadConfig(m_strFile.toStdString());
	if (previousDetails != uiLogDetails()) emit uiLogDetailsChanged();
	SetStatus([strTarget] { return tr("Saved: %1").arg(strTarget); });
	return true;
}

void ConfigStore::installUdev()
{
#ifndef Q_OS_LINUX
	TranslatableText::Factory msg = [] { return tr("No udev rules are needed on this platform."); };
	SetStatus(msg);
	emit udevInstallFinished(false, msg());
#else
	QString strRule = FindUdevRule(m_usbVendor == "05c6" ? "99-qualcomm.rules" : m_usbVendor == "1f3a" ? "99-allwinner.rules" : "99-rk-rockusb-all.rules");
	if (strRule.isEmpty()) {
		TranslatableText::Factory msg = [] { return tr("USB rule files not found; copy the rules from scripts/ to /etc/udev/rules.d/ manually"); };
		SetStatus(msg);
		emit udevInstallFinished(false, msg());
		return;
	}

	QString strScript = "cp '" + strRule
	                    + "' /etc/udev/rules.d/ && udevadm control --reload-rules && udevadm trigger";
	SetStatus([] { return tr("Installing udev rules…"); });

	QProcess *pProc = new QProcess(this);
	std::shared_ptr<bool> pbFinished = std::make_shared<bool>(false);
	auto finish = [this, pProc, pbFinished](bool bOk, TranslatableText::Factory factory) {
		if (*pbFinished)
			return;
		*pbFinished = true;
		SetStatus(factory);
		emit udevInstallFinished(bOk, factory());
		pProc->deleteLater();
	};
	connect(pProc, &QProcess::errorOccurred, this,
	        [finish](QProcess::ProcessError error) {
		        if (error == QProcess::FailedToStart)
			        finish(false, [] { return tr("Cannot launch pkexec; run the install command manually with sudo."); });
	        });
	connect(pProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
	        [finish, strRule](int iExit, QProcess::ExitStatus status) {
		        if (status == QProcess::NormalExit && iExit == 0)
			        finish(true, [strRule] { return tr("udev rules installed successfully: %1").arg(strRule); });
		        else
			        finish(false, [iExit] { return tr("udev rule install failed (exit code %1); run it manually with sudo.").arg(iExit); });
	        });
	pProc->start("pkexec", QStringList() << "sh" << "-c" << strScript);
#endif
}

bool ConfigStore::udevRuleInstalled() const
{
#ifdef Q_OS_LINUX
	for (size_t i = 0; i < sizeof(kUdevRuleDirs) / sizeof(kUdevRuleDirs[0]); i++) {
		QDir dir(kUdevRuleDirs[i]);
		QStringList strFiles = dir.entryList(QStringList() << "*.rules",
		                                     QDir::Files | QDir::Readable);
		for (int j = 0; j < strFiles.size(); j++) {
			QFile file(dir.filePath(strFiles[j]));
			if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
				continue;
			QString strText = QString::fromUtf8(file.readAll());
			file.close();
			if (RuleGrantsUsbAccess(strText, m_usbVendor))
				return true;
		}
	}
#endif
	return false;
}

QString ConfigStore::udevCommand() const
{
#ifdef Q_OS_LINUX
	QString strRule = FindUdevRule(m_usbVendor == "05c6" ? "99-qualcomm.rules" : m_usbVendor == "1f3a" ? "99-allwinner.rules" : "99-rk-rockusb-all.rules");
	if (strRule.isEmpty())
		strRule = m_usbVendor == "05c6" ? "scripts/99-qualcomm.rules" : m_usbVendor == "1f3a" ? "scripts/99-allwinner.rules" : "scripts/99-rk-rockusb-all.rules";
	return "sudo cp '" + strRule + "' /etc/udev/rules.d/ && "
	       "sudo udevadm control --reload-rules && sudo udevadm trigger";
#else
	return QString();
#endif
}

}
