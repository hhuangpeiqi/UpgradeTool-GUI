#ifndef IDB_HEADER
#define IDB_HEADER

#include "DefineHeader.h"

bool MakeSector0(PBYTE pSector, USHORT usFlashDataSec, USHORT usFlashBootSec, bool rc4Flag);
bool MakeSector1(PBYTE pSector);
bool MakeSector2(PBYTE pSector);
bool MakeSector3(PBYTE pSector);
int MakeIDBlockData(PBYTE pDDR, PBYTE pLoader, PBYTE lpIDBlock, USHORT usFlashDataSec,
                    USHORT usFlashBootSec, DWORD dwLoaderDataSize, DWORD dwLoaderSize, bool rc4Flag);

#endif
