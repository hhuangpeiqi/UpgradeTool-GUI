#include "gui/FirmwareEntries.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QObject>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "Parameter.h"
#include "core/FirmwareService.h"

namespace gui {

namespace {

QString ImageKey(const QString &strBaseName)
{
	QString str = strBaseName.toLower();
	str.replace('-', '_');
	return str;
}

QString FindLoaderFile(const QDir &dir)
{
	const QStringList strFiles = dir.entryList(QStringList() << "*.bin", QDir::Files);
	for (int i = 0; i < strFiles.size(); i++)
		if (strFiles[i].compare("MiniLoaderAll.bin", Qt::CaseInsensitive) == 0)
			return strFiles[i];
	for (int i = 0; i < strFiles.size(); i++)
		if (strFiles[i].contains("loader", Qt::CaseInsensitive))
			return strFiles[i];
	return QString();
}

}

QString StorageCodeName(unsigned int uiStorage)
{
	switch (uiStorage) {
	case 0x001: return QString("FLASH");
	case 0x002: return QString("EMMC");
	case 0x004: return QString("SD");
	case 0x008: return QString("SD1");
	case 0x040: return QString("RAM");
	case 0x080: return QString("USB");
	case 0x100: return QString("SPINAND");
	case 0x200: return QString("SPINOR");
	case 0x400: return QString("SATA");
	case 0x800: return QString("PCIE");
	default:    return QString();
	}
}

QString FormatSize(unsigned int uiSize)
{
	if (uiSize >= 1024u * 1024u)
		return QString("%1MB").arg(uiSize / (1024.0 * 1024.0), 0, 'f', 1);
	if (uiSize >= 1024u)
		return QString("%1KB").arg(uiSize / 1024.0, 0, 'f', 1);
	return QString("%1B").arg(uiSize);
}

bool ParseFirmware(const QString &path, QList<FirmwareEntry> &entries, FirmwareMeta &meta)
{
	core::FirmwareInfo info;
	entries.clear(); meta = FirmwareMeta();
	if (!core::FirmwareService::Inspect(path.toStdString(), info)) return false;
	meta.valid = info.valid; meta.chip = QString::fromStdString(info.chip);
	meta.version = QString::fromStdString(info.version); meta.loaderVersion = QString::fromStdString(info.loaderVersion);
	meta.buildTime = QString::fromStdString(info.buildTime); meta.sign = QString::fromStdString(info.sign); meta.storage = info.storage;
	for (const auto &entry : info.entries) {
		FirmwareEntry e;
		e.partName = QString::fromStdString(entry.partName); e.fileName = QString::fromStdString(entry.fileName);
		e.size = entry.size; e.lba = entry.lba; e.hasPart = entry.hasPart; e.imageLike = entry.imageLike;
		e.isLoader = entry.isLoader; e.type = QString::fromStdString(entry.type); entries.append(e);
	}
	return true;
}

bool ParseImageFolder(const QString &strDir, QList<FirmwareEntry> &entries)
{
	entries.clear();

	QDir dir(strDir);
	QString strParam = dir.filePath("parameter.txt");
	if (!QFileInfo(strParam).isFile())
		return false;

	QByteArray baPath = strParam.toLocal8Bit();
	PARAM_ITEM_VECTOR vecItems;
	CONFIG_ITEM_VECTOR vecUuid;
	if (!parse_parameter_file(baPath.data(), vecItems, vecUuid))
		return false;

	QMap<QString, QString> mapImages;
	const QStringList strImgs = dir.entryList(QStringList() << "*.img", QDir::Files);
	for (int i = 0; i < strImgs.size(); i++) {
		const QString &f = strImgs[i];
		if (f.length() <= 4)
			continue;
		mapImages.insert(ImageKey(f.left(f.length() - 4)), f);
	}

	const QString strLoader = FindLoaderFile(dir);
	if (!strLoader.isEmpty()) {
		FirmwareEntry l;
		l.partName = "Loader";
		l.fileName = strLoader;
		l.size = 0;
		l.lba = 0xFFFFFFFF;
		l.hasPart = false;
		l.imageLike = true;
		l.isLoader = true;
		l.type = QObject::tr("loader");
		entries.append(l);
	}

	FirmwareEntry param;
	param.partName = "parameter";
	param.fileName = "parameter.txt";
	param.size = 0;
	param.lba = 0xFFFFFFFF;
	param.hasPart = false;
	param.imageLike = true;
	param.isLoader = false;
	param.type = QObject::tr("parameter");
	entries.append(param);

	for (size_t i = 0; i < vecItems.size(); i++) {
		const STRUCT_PARAM_ITEM &it = vecItems[i];

		QString strName = QString::fromLatin1(it.szItemName);
		int iColon = strName.indexOf(':');
		if (iColon >= 0)
			strName = strName.left(iColon);

		FirmwareEntry e;
		e.partName = strName;
		const QString strResolved = mapImages.value(ImageKey(strName));
		e.fileName = strResolved.isEmpty() ? (strName + ".img") : strResolved;
		e.size = it.uiItemSize;
		e.lba = it.uiItemOffset;
		e.hasPart = true;
		e.isLoader = false;
		e.imageLike = !strResolved.isEmpty() || QFileInfo(dir.filePath(e.fileName)).isFile();
		e.type = e.imageLike ? QObject::tr("image") : QObject::tr("Other");
		entries.append(e);
	}
	return true;
}

}
