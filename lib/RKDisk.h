#ifndef CRKDISK_HEADER
#define CRKDISK_HEADER

#include "DefineHeader.h"

struct STRUCT_DISK_BASE {
	int         diskId;
	std::string name;
	std::string vendor;
};

// Disk error codes written to g_lastDiskErr by the CRKDisk methods.
#define DISK_ERR_OPEN_CMD	(-1)	// popen failed
#define DISK_ERR_CMD		(-2)	// command exited non-zero
#define DISK_ERR_NO_DISK	(-3)	// m_iDiskId < 0
#define DISK_ERR_NOT_OPEN	(-6)	// m_iFd < 0
#define DISK_ERR_VOLUME		(-7)	// GetAllVolumeFromDisk failed
#define DISK_ERR_READ		(-9)	// short read
#define DISK_ERR_WRITE		(-10)	// short write

extern int g_lastDiskErr;

class CRKDisk
{
public:
	CRKDisk(STRUCT_DISK_BASE diskBase);
	virtual ~CRKDisk();

	bool OpenDisk();
	void CloseDisk();
	bool ReadDisk(unsigned long ulOffset, int iSize, unsigned char *pBuf);
	bool WriteDisk(unsigned long ulOffset, int iSize, unsigned char *pBuf);
	bool OffsetDisk(unsigned long ulOffset, unsigned int uiWhence);
	unsigned long long GetDiskSize();
	int  GetLastDiskError();
	bool CheckDiskRemovable(int iDiskId, bool &bRemovable);
	bool IsRemovedDisk(bool &bRemoved);

	int  GetAllDisk(std::vector<STRUCT_DISK_BASE> &vDisk);
	int  GetDiskFromVolume(std::string sVolume);
	bool GetAllDrive(std::vector<std::string> &vDrive, bool bIsVolume);
	int  GetAllVolumeFromDisk(int iDiskId, std::vector<std::string> &vVolume);
	std::string VolumeRoot(std::string sVolume);
	bool FormatVolume(std::string sVolume);
	unsigned long long GetDriveSize(std::string sDrive);
	bool UpdateDiskLayout();
	bool LockAndDismount();

private:
	int                     m_iDiskId;		// +0x08
	int                     m_iFd;			// +0x0c
	std::vector<std::string> m_vVolume;		// +0x10
	std::string             m_sDevPath;		// +0x28
	std::string             m_sVolume;		// +0x48
	unsigned long long      m_ullDiskSize;	// +0x68
	int                     m_iRemovable;	// +0x70
};

#endif
