#include "gui/backend/ConfigStore.h"

#include <memory>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QSettings>

#include "platform/PlatformPath.h"

namespace gui {

namespace {

const char *kLogDetailsKey = "ui/logDetails";

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

}

ConfigStore::ConfigStore(QObject *parent)
	: QObject(parent)
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
	m_uiLogDetails = settings.value(kLogDetailsKey, false).toBool();
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
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
	settings.sync();
	if (settings.status() != QSettings::NoError) {
		SetStatus([] { return tr("Load failed: cannot read user settings."); });
		return {{"ui_log_details", m_uiLogDetails}};
	}
	const bool details = settings.value(kLogDetailsKey, false).toBool();
	if (m_uiLogDetails != details) {
		m_uiLogDetails = details;
		emit uiLogDetailsChanged();
	}
	SetStatus([] { return tr("Settings loaded."); });
	return {{"ui_log_details", m_uiLogDetails}};
}

bool ConfigStore::save(const QVariantMap &values)
{
	const bool details = values.value("ui_log_details").toBool();
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
	const QVariant previous = settings.value(kLogDetailsKey);
	settings.setValue(kLogDetailsKey, details);
	settings.sync();
	if (settings.status() != QSettings::NoError) {
		// Roll back Qt's shared cache as well, so a failed save cannot look persisted.
		if (previous.isValid()) settings.setValue(kLogDetailsKey, previous);
		else settings.remove(kLogDetailsKey);
		SetStatus([] { return tr("Save failed: cannot write user settings."); });
		return false;
	}
	if (m_uiLogDetails != details) {
		m_uiLogDetails = details;
		emit uiLogDetailsChanged();
	}
	SetStatus([] { return tr("Settings saved."); });
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
