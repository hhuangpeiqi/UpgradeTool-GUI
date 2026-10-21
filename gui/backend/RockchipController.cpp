#include "gui/backend/RockchipController.h"
#include "gui/OperationSpec.h"
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
namespace gui {
namespace {
const char *kLastFirmwareKey="firmware/last";
QString AppTr(const char *text) { return QCoreApplication::translate("gui::AppController",text); }
QString ErrorText(core::ErrorCode code)
{
	switch (code) {
	case core::ErrorCode::Timeout: return AppTr("Timed out waiting for the target device to enter Maskrom");
	case core::ErrorCode::InvalidInput: return QObject::tr("Invalid or out-of-range parameter");
	case core::ErrorCode::DeviceMissing: return QObject::tr("Target device disconnected; select a device again");
	case core::ErrorCode::DeviceAmbiguous: return QObject::tr("Device identity is ambiguous; select a device again");
	case core::ErrorCode::FileAccess: return QObject::tr("Cannot open file; check the path and permissions");
	case core::ErrorCode::InvalidFirmware: return QObject::tr("Invalid firmware format or checksum");
	case core::ErrorCode::UnsupportedDevice: return QObject::tr("The current device mode does not support this operation");
	default: return QObject::tr("Operation failed; see the log");
	}
}
}
RockchipController::RockchipController(ConfigStore *config,QObject *parent) : QObject(parent),m_config(config),m_pImageEntries(new ImageEntryModel(this)),m_pFirmwareEntries(new KeyValueModel(this)),m_pResults(new KeyValueModel(this)) {
    m_strFwVersion=m_strFwLoader=m_strFwChip=m_strFwBuild=m_strFwSign="-";
    SetImageHint([] { return AppTr("Select the split package directory (with parameter.txt, per-partition .img and Loader .bin)."); },false);
    SetFirmwareHint([] { return AppTr("Select firmware."); },false);
    SetAdvancedHint([] { return AppTr("Ready"); },false);
}
RockchipController::~RockchipController() { cancelInspections(); }
void RockchipController::cancelInspections() { if(m_folderInspection) m_folderInspection->request_stop(); if(m_firmwareInspection) m_firmwareInspection->request_stop(); }
void RockchipController::restore() {
	{
		QSettings settings(QSettings::IniFormat, QSettings::UserScope,
		                   "upgrade_tool", "upgrade_tool");
		QString strLast = settings.value(kLastFirmwareKey).toString().trimmed();
		if (strLast.isEmpty()) { strLast = QString::fromStdString(m_config->snapshot().String("firmware")); }
		if (!strLast.isEmpty()) {
			if (QFileInfo(strLast).isFile())
				setFirmware(strLast);
			else
				settings.remove(kLastFirmwareKey);
		}
	}

}
void RockchipController::retranslate() {
    m_imageHint.retranslate(); m_firmwareHint.retranslate(); m_advancedHint.retranslate();
    if(m_firmwareInfo.bound()) m_pFirmwareEntries->SetItems(m_firmwareInfo.value());
    emit imageStateChanged(); emit firmwareStateChanged(); emit advancedStateChanged();
}
void RockchipController::applyInspection(int context,quint64 generation,const core::OperationResult &result) {
    if(context==4 && generation==m_folderGeneration) {
        if(!result.ok() || !result.firmware.valid) { SetImageHint([this] { return AppTr("parameter.txt not found or failed to parse: %1").arg(m_strImageDir); },true); return; }
        const auto &entries=result.firmware.entries;
	QDir dir(m_strImageDir);
	QList<ImageRow> rows;
	for (const auto &e : entries) {
		ImageRow row;
		row.storage = QString("-");
		row.addr = (e.lba == 0xFFFFFFFF)
		           ? QString("-")
		           : QString("0x%1").arg(e.lba, 8, 16, QChar('0'));
		row.name = QString::fromStdString(e.partName);
		row.path = e.imageLike ? dir.filePath(QString::fromStdString(e.fileName)) : QString();
		row.selectable = e.imageLike;
		row.checked = e.imageLike;
		row.isLoader = e.isLoader;
		rows.append(row);
	}
	m_pImageEntries->SetRows(rows);

	const int entryCount = entries.size();
	SetImageHint([this, entryCount] {
		return AppTr("Parsed %2 entries from %1; check them and click \"Execute\" to download one by one.")
			.arg(m_strImageDir).arg(entryCount);
	}, false);
	emit imageStateChanged();
    } else if(context==5 && generation==m_firmwareGeneration) {
        if(result.status==core::OperationStatus::Cancelled) {
            SetFirmwareHint([] { return AppTr("Cancelled"); },false); return;
        }
        if(!result.ok()) {
            const auto error=result.error;
            SetFirmwareHint([error] {
                return error==core::ErrorCode::InvalidFirmware ? AppTr("Invalid firmware.") : ErrorText(error);
            },true); return;
        }
        const auto &meta=result.firmware;
        m_strFwVersion=QString::fromStdString(meta.version); m_strFwLoader=QString::fromStdString(meta.loaderVersion);
        m_strFwChip=QString::fromStdString(meta.chip); m_strFwBuild=QString::fromStdString(meta.buildTime); m_strFwSign=QString::fromStdString(meta.sign);
        m_bFirmwareValid=true;
        const auto count=meta.entries.size(); SetFirmwareHint([count] { return AppTr("Firmware loaded with %1 partitions.").arg(count); },false);
        emit firmwareStateChanged();
    }
}
QStringList RockchipController::storages() const
{
	return QStringList()
		<< "1.FLASH" << "2.EMMC" << "3.SD" << "4.SD1" << "5.SPIFLASH"
		<< "6.SPI/RAMD" << "7.RKNN" << "8.NFS" << "9.SATA" << "10.PCIE"
		<< "11.UFS" << "12.NVME";
}

void RockchipController::SetImageHint(TranslatableText::Factory factory, bool bError)
{
	m_imageHint.Set(std::move(factory));
	m_bImageHintError = bError;
	emit imageStateChanged();
}

void RockchipController::SetFirmwareHint(TranslatableText::Factory factory, bool bError)
{
	m_firmwareHint.Set(std::move(factory));
	m_bFirmwareHintError = bError;
	emit firmwareStateChanged();
}

void RockchipController::SetAdvancedHint(TranslatableText::Factory factory, bool bError)
{
	m_advancedHint.Set(std::move(factory));
	m_bAdvancedHintError = bError;
	emit advancedStateChanged();
}

void RockchipController::runOperation(int operation, const QVariantMap &parameters)
{
	if (m_locked) return;
	core::OperationRequest r;
	r=core::OperationRequest(static_cast<core::Operation>(operation));
	if (r.operation() == core::Operation::Count || r.vendor() != core::Vendor::Rockchip || !m_active) return;
	r.file().path = parameters.value("path").toString().toStdString();
	r.file().output = parameters.value("output").toString().toStdString();
	r.rockchip().partition = parameters.value("partition").toString().toStdString();
	r.rockchip().serial = parameters.value("serial").toString().trimmed().toStdString();
	r.rockchip().reboot = parameters.value("reboot", true).toBool();
	r.rockchip().block.force = parameters.value("force", false).toBool();
	bool valid = true;
	auto number = [&](const char *key, uint32_t &value) {
		if (!parameters.contains(key)) return;
		QString text = parameters.value(key).toString().trimmed();
		bool ok = false;
		qulonglong parsed = text.toULongLong(&ok, text.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
		if (!ok || text.startsWith('-') || parsed > UINT32_MAX) valid = false;
		else value = static_cast<uint32_t>(parsed);
	};
	number("begin", r.rockchip().lba.begin); number("count", r.rockchip().lba.count);
	number("pageSize", r.rockchip().sector.pageSize); number("spareSize", r.rockchip().sector.spareSize);
	number("chipSelect", r.rockchip().block.chipSelect); number("subcode", r.rockchip().subcode); number("pipe", r.rockchip().pipe);
	number("ubootAddress", r.rockchip().system.ubootAddress); number("trustAddress", r.rockchip().system.trustAddress); number("bootAddress", r.rockchip().system.bootAddress);
	r.rockchip().system.uboot = parameters.value("uboot").toString().toStdString();
	r.rockchip().system.trust = parameters.value("trust").toString().toStdString();
	r.rockchip().system.boot = parameters.value("boot").toString().toStdString();
	// Keep existing loader fallback, resolved before submitting a business request.
	if (r.file().path.empty() && (r.operation()== core::Operation::EraseFlash || r.operation() == core::Operation::DownloadBoot || r.operation() == core::Operation::LoaderUpgrade))
		r.file().path=m_config->snapshot().String("loader");
	if (!valid) { SetAdvancedHint([] { return AppTr("Numeric parameter invalid or out of range"); }, true); emit toastRequested(AppTr("Numeric parameter invalid or out of range"), true); return; }
	core::TaskJob job; job.steps.push_back(r); emit executeRequested(job);
}

void RockchipController::setImageDir(const QString &strDir)
{
	if (m_locked) return;
	QString strTrimmed = strDir.trimmed();
	m_strImageDir = strTrimmed;
    if(m_folderInspection) m_folderInspection->request_stop();
    ++m_folderGeneration;
	m_pImageEntries->Clear();

	if (m_strImageDir.isEmpty()) {
		SetImageHint([] { return AppTr("Select the split package directory (with parameter.txt, per-partition .img and Loader .bin)."); }, false);
		emit imageStateChanged();
		return;
	}

    core::InspectionRequest inspection;
    inspection.context=4; inspection.generation=m_folderGeneration; inspection.directory=true;
    inspection.request=core::OperationRequest(core::Operation::FirmwareInfo);
    inspection.request.file().path=m_strImageDir.toStdString();
    m_folderInspection=inspection.cancelled;
    emit inspectRequested(inspection);
}

void RockchipController::clearImage()
{
	if (m_locked) return;
    if(m_folderInspection) m_folderInspection->request_stop();
    ++m_folderGeneration;
	m_pImageEntries->Clear();
	SetImageHint([] { return AppTr("Cleared."); }, false);
	emit imageStateChanged();
}

void RockchipController::executeImageDownload(bool reboot)
{
	if (m_locked) return;
	core::TaskJob job;
	for (const auto &row : m_pImageEntries->rows()) {
		if (!row.selectable || !row.checked || row.path.isEmpty()) continue;
		core::OperationRequest request;
		request=core::OperationRequest(row.isLoader ? core::Operation::LoaderUpgrade : core::Operation::DownloadImage);
		request.file().path = row.path.toStdString(); request.rockchip().partition = row.name.toStdString();
		job.steps.push_back(request);
	}
	if (job.steps.empty()) { SetImageHint([] { return AppTr("Check entries and specify image files first."); }, true); return; }
	job.rebootAfter = reboot;
	emit executeRequested(job);
	if (m_locked) { m_bQueueActive = true; emit imageStateChanged(); }
}

void RockchipController::setFirmware(const QString &strPath)
{
	if (m_locked) return;
	m_strFirmware = strPath.trimmed();
    if(m_firmwareInspection) m_firmwareInspection->request_stop();
    ++m_firmwareGeneration;
	if (!m_strFirmware.isEmpty()) {
		QSettings settings(QSettings::IniFormat, QSettings::UserScope,
		                   "upgrade_tool", "upgrade_tool");
		settings.setValue(kLastFirmwareKey, m_strFirmware);
	}
	m_pFirmwareEntries->Clear();
	m_firmwareInfo.Clear();
	m_bFirmwareValid = false;
	m_strFwVersion = m_strFwLoader = m_strFwChip = m_strFwBuild = m_strFwSign = QString("-");
	emit firmwareStateChanged();

	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint([] { return AppTr("Select firmware."); }, false);
		return;
	}

    core::InspectionRequest inspection;
    inspection.context=5; inspection.generation=m_firmwareGeneration;
    inspection.request=core::OperationRequest(core::Operation::FirmwareInfo);
    inspection.request.file().path=m_strFirmware.toStdString();
    m_firmwareInspection=inspection.cancelled;
    emit inspectRequested(inspection);
}

void RockchipController::clearFirmware()
{
	setFirmware(QString());
}

void RockchipController::executeFirmwareUpgrade(bool bReboot)
{
	if (m_locked) return;
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint([] { return AppTr("Select firmware first"); }, true);
		return;
	}
	m_bFirmwareReboot = bReboot;

	SetFirmwareHint([] { return AppTr("Upgrading firmware…"); }, false);
	runOperation(static_cast<int>(core::Operation::FirmwareUpgrade), {{"path", m_strFirmware}, {"reboot", bReboot}});
}

void RockchipController::extractFirmware(const QString &strDir)
{
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint([] { return AppTr("Select firmware first"); }, true);
		return;
	}
	if (strDir.trimmed().isEmpty())
		return;

	SetFirmwareHint([] { return AppTr("Extracting firmware…"); }, false);
	runOperation(static_cast<int>(core::Operation::FirmwareExtract), {{"path", m_strFirmware}, {"output", strDir.trimmed()}});
}

void RockchipController::showFirmwareInfo()
{
	if (m_strFirmware.isEmpty()) {
		SetFirmwareHint([] { return AppTr("Select firmware first"); }, true);
		return;
	}

	SetFirmwareHint([] { return AppTr("Reading firmware info…"); }, false);
	runOperation(static_cast<int>(core::Operation::FirmwareInfo), {{"path", m_strFirmware}});
}

void RockchipController::exportComLog(const QString &strFile)
{
	if (strFile.trimmed().isEmpty())
		return;

	runOperation(static_cast<int>(core::Operation::ReadComLog), {{"output", strFile.trimmed()}});
}

void RockchipController::exportImage(const QString &strBegin, const QString &strCount,
                                const QString &strFile)
{
	if (strBegin.trimmed().isEmpty() || strCount.trimmed().isEmpty() ||
	    strFile.trimmed().isEmpty()) {
		SetAdvancedHint([] { return AppTr("Enter the start sector and count and choose an export file"); }, true);
		return;
	}

	runOperation(static_cast<int>(core::Operation::ReadLba), {{"begin", strBegin}, {"count", strCount}, {"output", strFile.trimmed()}});
}

void RockchipController::createGpt(const QString &strInput, const QString &strOutput)
{
	if (strInput.trimmed().isEmpty() || strOutput.trimmed().isEmpty()) {
		emit toastRequested(AppTr("Enter the input parameter and output GPT"), true);
		return;
	}

	runOperation(static_cast<int>(core::Operation::CreateGpt), {{"path", strInput.trimmed()}, {"output", strOutput.trimmed()}});
}
void RockchipController::applyResult(const core::OperationResult &result)
{
	bool bOk = result.ok();
	core::Operation id = result.operation;
	QVariantList items;
	for (const auto &row : result.items) { QVariantMap item; item["label"] = QString::fromStdString(row.label); item["value"] = QString::fromStdString(row.value); items.append(item); }
	QVariantList infoRows;
	if (id == core::Operation::FirmwareInfo) {
		for (const auto &entry : result.firmware.entries) {
			QVariantMap row;
			row["part"] = QString::fromStdString(entry.partName);
			row["file"] = QString::fromStdString(entry.fileName);
			row["size"] = QVariant::fromValue<qulonglong>(entry.size);
			row["lba"] = QVariant::fromValue<qulonglong>(entry.lba);
			infoRows.append(row);
		}
	}

	if (id == core::Operation::DownloadImage || id == core::Operation::LoaderUpgrade) {
		if (!bOk && !m_bCancelRequested) {
			QString strReason = ErrorText(result.error);
			SetImageHint([strReason] {
				return strReason.isEmpty() ? AppTr("Download failed; see the log.")
				                           : AppTr("Download failed: %1").arg(strReason);
			}, true);
		}

		return;
	}

	if (id == core::Operation::EnterMaskrom) {
        m_pResults->SetItems(items);
        if (result.status == core::OperationStatus::Cancelled)
            SetAdvancedHint([] { return AppTr("Cancelled"); }, false);
        else if (bOk)
            SetAdvancedHint([] { return AppTr("Entered Maskrom mode"); }, false);
        else {
            const auto error = result.error;
            SetAdvancedHint([error] { return AppTr("Enter Maskrom failed: %1").arg(ErrorText(error)); }, true);
        }
        return;
    }

	if (id == core::Operation::ResetDevice) {
		SetImageHint([bOk] { return bOk ? AppTr("Reboot command sent.") : AppTr("Reset device failed."); }, !bOk);
		m_pResults->SetItems(items);
		SetAdvancedHint([bOk] { return bOk ? AppTr("Reset device succeeded") : AppTr("Reset device failed"); }, !bOk);

		return;
	}

	if (id == core::Operation::FirmwareUpgrade) {
		if (bOk)
			SetFirmwareHint([this] {
				return m_bFirmwareReboot ? AppTr("Upgrade succeeded; device rebooted.")
				                         : AppTr("Upgrade succeeded; device not rebooted.");
			}, false);
		else if (!m_bCancelRequested) {
			QString strReason = ErrorText(result.error);
			SetFirmwareHint([strReason] {
				return strReason.isEmpty() ? AppTr("Upgrade failed; see the log.")
				                           : AppTr("Upgrade failed: %1").arg(strReason);
			}, true);
		}

		return;
	}

	if (id == core::Operation::FirmwareExtract) {
		SetFirmwareHint([bOk] { return bOk ? AppTr("Extraction succeeded.") : AppTr("Extraction failed."); }, !bOk);

		return;
	}

	if (id == core::Operation::FirmwareInfo) {
		m_pFirmwareEntries->Clear();
		m_firmwareInfo.Clear();
		if (!bOk) {
			SetFirmwareHint([] { return AppTr("Failed to read firmware info."); }, true);

			return;
		}
		m_firmwareInfo.Set([items, infoRows] {
			QVariantList rows = items;
			for (const QVariant &value : infoRows) {
				const QVariantMap row = value.toMap();
				QVariantMap item;
				item["label"] = row["part"];
				item["value"] = AppTr("%1, size %2, LBA 0x%3")
					.arg(row["file"].toString())
					.arg(row["size"].toULongLong())
					.arg(row["lba"].toULongLong(), 0, 16);
				rows.append(item);
			}
			return rows;
		});
		m_pFirmwareEntries->SetItems(m_firmwareInfo.value());
		m_strFwChip = QString::fromStdString(result.firmware.chip);
		m_strFwVersion = QString::fromStdString(result.firmware.version);
		m_strFwLoader = QString::fromStdString(result.firmware.loaderVersion);
		m_strFwBuild = QString::fromStdString(result.firmware.buildTime);
		m_strFwSign = QString::fromStdString(result.firmware.sign);
		SetFirmwareHint([] { return AppTr("Firmware info read successfully."); }, false);
		emit firmwareStateChanged();

		return;
	}

	m_pResults->SetItems(items);
	QString strName = OperationName(id);
	const core::ErrorCode errorCode = result.error;
	const bool itemsEmpty = items.isEmpty();
	if (!bOk)
		SetAdvancedHint([strName, errorCode] { return AppTr("%1 failed: %2").arg(strName, ErrorText(errorCode)); }, true);
	else if (itemsEmpty)
		SetAdvancedHint([strName] { return AppTr("%1 succeeded").arg(strName); }, false);
	else
		SetAdvancedHint([strName] { return AppTr("%1 read successfully").arg(strName); }, false);

}
void RockchipController::applyFinished(const core::TaskSummary &summary,const core::TaskJob &job) {
    const bool cancelled=summary.status==core::OperationStatus::Cancelled;
    const bool ok=summary.status==core::OperationStatus::Succeeded;
    const auto runningOperation=job.steps.empty()?core::Operation::Count:job.steps.front().operation();
    TranslatableText::Factory statusText=[cancelled,ok] { return cancelled?AppTr("Cancelled"):ok?AppTr("Operation complete"):AppTr("Operation failed; see the log"); };
    TranslatableText::Factory batchText=[summary,cancelled] { return AppTr("Batch finished: %1 succeeded, %2 failed").arg(summary.succeeded).arg(summary.failed)+(cancelled?AppTr(", remaining operations cancelled"):QString()); };
	if (m_bQueueActive) {
		m_bQueueActive = false;
		SetImageHint([reboot=job.rebootAfter, cancelled, ok] {
			return cancelled ? AppTr("Cancelled.") : ok ?
				(reboot ? AppTr("Download complete; reboot command sent.") : AppTr("Download complete."))
				: AppTr("Download finished with failures. Failed devices were not rebooted automatically.");
		}, !ok && !cancelled);
		emit imageStateChanged();
	} else if (cancelled) {
		if (runningOperation == core::Operation::FirmwareUpgrade || runningOperation == core::Operation::FirmwareExtract || runningOperation == core::Operation::FirmwareInfo)
			SetFirmwareHint(statusText, false);
		else SetAdvancedHint(statusText, false);
	}
    if(job.targets.size()>1) {
        if (runningOperation == core::Operation::FirmwareUpgrade) SetFirmwareHint(batchText, !ok && !cancelled);
		else if (runningOperation != core::Operation::LoaderUpgrade && runningOperation != core::Operation::DownloadImage) SetAdvancedHint(batchText, !ok && !cancelled);
    }
    m_bCancelRequested=false;
}
}
