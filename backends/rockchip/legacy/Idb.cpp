#include "Idb.h"
#include "RKDevice.h"
#include "crc.h"

bool MakeSector0(PBYTE pSector, USHORT usFlashDataSec, USHORT usFlashBootSec, bool rc4Flag)
{
	PRK28_IDB_SEC0 pSec0;
	memset(pSector, 0, SECTOR_SIZE);
	pSec0 = (PRK28_IDB_SEC0)pSector;
	pSec0->dwTag = 0x0FF0AA55;
	pSec0->uiRc4Flag = rc4Flag;
	pSec0->usBootCode1Offset = 0x4;
	pSec0->usBootCode2Offset = 0x4;
	pSec0->usBootDataSize = usFlashDataSec;
	pSec0->usBootCodeSize = usFlashDataSec + usFlashBootSec;
	return true;
}

bool MakeSector1(PBYTE pSector)
{
	PRK28_IDB_SEC1 pSec1;
	memset(pSector, 0, SECTOR_SIZE);
	pSec1 = (PRK28_IDB_SEC1)pSector;
	pSec1->usSysReservedBlock = 0xC;
	pSec1->usDisk0Size = 0xFFFF;
	pSec1->uiChipTag = 0x38324B52;
	return true;
}

bool MakeSector2(PBYTE pSector)
{
	PRK28_IDB_SEC2 pSec2;
	memset(pSector, 0, SECTOR_SIZE);
	pSec2 = (PRK28_IDB_SEC2)pSector;
	strcpy(pSec2->szVcTag, "VC");
	strcpy(pSec2->szCrcTag, "CRC");
	return true;
}

bool MakeSector3(PBYTE pSector)
{
	memset(pSector, 0, SECTOR_SIZE);
	return true;
}

int MakeIDBlockData(PBYTE pDDR, PBYTE pLoader, PBYTE lpIDBlock, USHORT usFlashDataSec,
                    USHORT usFlashBootSec, DWORD dwLoaderDataSize, DWORD dwLoaderSize, bool rc4Flag)
{
	RK28_IDB_SEC0 sector0Info;
	RK28_IDB_SEC1 sector1Info;
	RK28_IDB_SEC2 sector2Info;
	RK28_IDB_SEC3 sector3Info;
	UINT i;
	MakeSector0((PBYTE)&sector0Info, usFlashDataSec, usFlashBootSec, rc4Flag);
	MakeSector1((PBYTE)&sector1Info);
	if (!MakeSector2((PBYTE)&sector2Info))
		return -6;
	if (!MakeSector3((PBYTE)&sector3Info))
		return -7;
	sector2Info.usSec0Crc = CRC_16((PBYTE)&sector0Info, SECTOR_SIZE);
	sector2Info.usSec1Crc = CRC_16((PBYTE)&sector1Info, SECTOR_SIZE);
	sector2Info.usSec3Crc = CRC_16((PBYTE)&sector3Info, SECTOR_SIZE);

	memcpy(lpIDBlock, &sector0Info, SECTOR_SIZE);
	memcpy(lpIDBlock + SECTOR_SIZE, &sector1Info, SECTOR_SIZE);
	memcpy(lpIDBlock + SECTOR_SIZE * 3, &sector3Info, SECTOR_SIZE);

	if (rc4Flag) {
		for (i = 0; i < dwLoaderDataSize / SECTOR_SIZE; i++)
			P_RC4(pDDR + i * SECTOR_SIZE, SECTOR_SIZE);
		for (i = 0; i < dwLoaderSize / SECTOR_SIZE; i++)
			P_RC4(pLoader + i * SECTOR_SIZE, SECTOR_SIZE);
	}

	memcpy(lpIDBlock + SECTOR_SIZE * 4, pDDR, dwLoaderDataSize);
	memcpy(lpIDBlock + SECTOR_SIZE * (4 + usFlashDataSec), pLoader, dwLoaderSize);

	sector2Info.uiBootCodeCrc = CRC_32((PBYTE)(lpIDBlock + SECTOR_SIZE * 4), sector0Info.usBootCodeSize * SECTOR_SIZE);
	memcpy(lpIDBlock + SECTOR_SIZE * 2, &sector2Info, SECTOR_SIZE);
	for (i = 0; i < 4; i++) {
		if (i == 1)
			continue;
		P_RC4(lpIDBlock + SECTOR_SIZE * i, SECTOR_SIZE);
	}
	return 0;
}
