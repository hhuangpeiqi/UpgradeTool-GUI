#ifndef GUI_FIRMWARE_ENTRIES_H
#define GUI_FIRMWARE_ENTRIES_H

#include <QList>
#include <QString>

namespace gui {

struct FirmwareEntry {
	QString partName;
	QString fileName;
	unsigned int size;
	unsigned int lba;
	bool hasPart;
	bool imageLike;
	bool isLoader;
	QString type;
};

struct FirmwareMeta {
	bool valid;
	QString chip;
	QString version;
	QString loaderVersion;
	QString buildTime;
	QString sign;
	unsigned int storage;
};

bool ParseFirmware(const QString &strPath, QList<FirmwareEntry> &entries, FirmwareMeta &meta);

bool ParseImageFolder(const QString &strDir, QList<FirmwareEntry> &entries);

QString StorageCodeName(unsigned int uiStorage);

QString FormatSize(unsigned int uiSize);

}

#endif
