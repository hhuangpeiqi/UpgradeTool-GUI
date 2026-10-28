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
#include "application/BuildFeatures.h"

namespace gui {

namespace {

const char *kLogDetailsKey = "ui/logDetails";

#ifdef Q_OS_LINUX
struct UdevRule {
    const char *vendor;
    const char *file;
};

const UdevRule kUdevRules[] = {
#if UPGRADE_TOOL_ROCKCHIP
    {"2207", "99-rk-rockusb-all.rules"},
#endif
#if UPGRADE_TOOL_ALLWINNER
    {"1f3a", "99-allwinner.rules"},
#endif
#if UPGRADE_TOOL_QUALCOMM
    {"05c6", "99-qualcomm.rules"},
#endif
};

QString ShellQuote(QString value)
{
    value.replace("'", "'\"'\"'");
    return "'" + value + "'";
}

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
    m_logFileMiB = settings.value("logging/fileMiB", 5).toInt();
    m_logTotalMiB = settings.value("logging/totalMiB", 50).toInt();
    if (m_logFileMiB < 1 || m_logFileMiB > 100 || m_logTotalMiB < m_logFileMiB || m_logTotalMiB > 10240) {
        m_logFileMiB = 5; m_logTotalMiB = 50;
    }
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
		return {{"ui_log_details", m_uiLogDetails}, {"log_file_mib", m_logFileMiB}, {"log_total_mib", m_logTotalMiB}};
	}
	const bool details = settings.value(kLogDetailsKey, false).toBool();
	if (m_uiLogDetails != details) {
		m_uiLogDetails = details;
		emit uiLogDetailsChanged();
	}
	SetStatus([] { return tr("Settings loaded."); });
	return {{"ui_log_details", m_uiLogDetails}, {"log_file_mib", m_logFileMiB}, {"log_total_mib", m_logTotalMiB}};
}

bool ConfigStore::save(const QVariantMap &values)
{
	if (m_logBusy) return false;
    const int file = values.value("log_file_mib", m_logFileMiB).toInt();
    const int total = values.value("log_total_mib", m_logTotalMiB).toInt();
    if (file < 1 || file > 100 || total < file || total > 10240) {
        SetStatus([] { return tr("Invalid log limits: file 1–100 MiB, total at least file size and at most 10240 MiB."); });
        return false;
    }
    const bool details = values.value("ui_log_details").toBool();
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
	const QVariant previousFile = settings.value("logging/fileMiB");
    const QVariant previousTotal = settings.value("logging/totalMiB");
    const QVariant previous = settings.value(kLogDetailsKey);
    settings.setValue("logging/fileMiB", file);
    settings.setValue("logging/totalMiB", total);
	settings.setValue(kLogDetailsKey, details);
	settings.sync();
	if (settings.status() != QSettings::NoError) {
		if (previousFile.isValid()) settings.setValue("logging/fileMiB", previousFile); else settings.remove("logging/fileMiB");
        if (previousTotal.isValid()) settings.setValue("logging/totalMiB", previousTotal); else settings.remove("logging/totalMiB");
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
	m_logFileMiB = file; m_logTotalMiB = total;
    if (m_logService) runLogOperation(false, true);
    SetStatus([] { return tr("Settings saved."); });
	return true;
}

platform::LogLimits ConfigStore::limits() const {
    return {uint64_t(m_logFileMiB) * 1024 * 1024, uint64_t(m_logTotalMiB) * 1024 * 1024};
}
void ConfigStore::attachLog(platform::LogService *log) {
    m_logService = log;
    const auto result = log->setLimits(limits());
    m_logBytes = result.bytes;
    m_logError = QString::fromStdString(result.error);
}
void ConfigStore::refreshLogStorage() { runLogOperation(false); }
void ConfigStore::clearLogFiles() { runLogOperation(true); }
void ConfigStore::runLogOperation(bool clear, bool apply) {
    if (!m_logService || m_logBusy) return;
    m_logBusy = true;
    emit logStorageChanged();
    const auto policy = limits();
    m_logFuture = std::async(std::launch::async, [this, clear, apply, policy] {
        const auto operation = clear ? m_logService->clearFiles(policy) : (apply ? m_logService->setLimits(policy) : m_logService->storage());
        auto result = m_logService->storage();
        if (!operation.error.empty()) result.error = operation.error;
        QMetaObject::invokeMethod(this, [this, result] {
            m_logBytes = result.bytes;
            m_logError = QString::fromStdString(result.error);
            m_logBusy = false;
            emit logStorageChanged();
        }, Qt::QueuedConnection);
    });
}

void ConfigStore::installUdev()
{
#ifndef Q_OS_LINUX
	TranslatableText::Factory msg = [] { return tr("No udev rules are needed on this platform."); };
	SetStatus(msg);
	emit udevInstallFinished(false, msg());
#else
    QStringList sources;
    for (const auto &rule : kUdevRules) {
        const QString path = FindUdevRule(rule.file);
        if (path.isEmpty()) {
            TranslatableText::Factory msg = [] { return tr("USB rule files not found; copy the rules from scripts/ to /etc/udev/rules.d/ manually"); };
            SetStatus(msg);
            emit udevInstallFinished(false, msg());
            return;
        }
        sources << ShellQuote(path);
    }
    const QString strRule = sources.join(' ');
    const QString strScript = "cp " + strRule
        + " /etc/udev/rules.d/ && udevadm control --reload-rules && udevadm trigger";
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
    QStringList missing;
    for (const auto &rule : kUdevRules) missing << rule.vendor;
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
            for (const auto &vendor : QStringList(missing)) {
                if (RuleGrantsUsbAccess(strText, vendor)) missing.removeAll(vendor);
            }
            if (missing.isEmpty()) return true;
		}
	}
#endif
	return false;
}

QString ConfigStore::udevCommand() const
{
#ifdef Q_OS_LINUX
    QStringList sources;
    for (const auto &rule : kUdevRules) {
        QString path = FindUdevRule(rule.file);
        if (path.isEmpty()) path = "scripts/" + QString::fromLatin1(rule.file);
        sources << ShellQuote(path);
    }
    return "sudo cp " + sources.join(' ') + " /etc/udev/rules.d/ && "
           "sudo udevadm control --reload-rules && sudo udevadm trigger";
#else
	return QString();
#endif
}

}
