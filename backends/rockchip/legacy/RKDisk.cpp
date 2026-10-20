#include "RKDisk.h"

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_lastDiskErr = 0;

namespace {

const char *kLsblkDisk = "lsblk -n -p -r -o name,type,rm,maj:min,vendor";
const char *kLsblkSize = "lsblk -n -p -r -b -o name,size ";
const char *kLsblkMount = "lsblk -n -p -r -o name,mountpoint ";
const char *kLsblkVol = "lsblk -n -I %d -p -r -o name,type,rm,maj:min,mountpoint";
const char *kPartprobe = "partprobe ";
const char *kUmount = "umount -A ";

int RunCommand(std::string sCmd, std::string &sOut)
{
	sOut.clear();
	FILE *fp = popen(sCmd.c_str(), "r");
	if (!fp) {
		g_lastDiskErr = errno;
		return -1;
	}
	char buf[256];
	while (fgets(buf, sizeof(buf), fp))
		sOut += buf;
	if (pclose(fp) != 0) {
		g_lastDiskErr = errno;
		return -2;
	}
	return 0;
}

void split_token(std::string sSrc, std::string sSep, std::vector<std::string> &vOut)
{
	size_t pos = 0;
	while (true) {
		size_t start = sSrc.find_first_not_of(sSep, pos);
		if (start == std::string::npos)
			break;
		size_t end = sSrc.find(sSep, start);
		if (end == std::string::npos) {
			vOut.push_back(sSrc.substr(start));
			break;
		}
		vOut.push_back(sSrc.substr(start, end - start));
		pos = end;
	}
}

void ParseLSblkResult(std::string sOut, std::vector<STRUCT_DISK_BASE> &vDisk,
		      unsigned int uiType)
{
	std::istringstream stream(sOut);
	std::string sLine;
	while (std::getline(stream, sLine)) {
		if (sLine.empty())
			continue;
		std::vector<std::string> vTok;
		split_token(sLine, " ", vTok);
		if (vTok.size() < 4)
			continue;
		if (vTok[2] == "1")		// removable
			continue;
		bool bKeep = false;
		if (uiType & 1) {
			if (vTok[1] == "disk")
				bKeep = true;
			else if ((uiType & 2) && vTok[1] == "part")
				bKeep = true;
		} else if ((uiType & 2) && vTok[1] == "part") {
			bKeep = true;
		}
		if (!bKeep)
			continue;

		int iMajor = 0, iMinor = 0;
		if (sscanf(vTok[3].c_str(), "%d:%d", &iMajor, &iMinor) != 2)
			continue;

		STRUCT_DISK_BASE disk;
		disk.diskId = iMajor * 100 + iMinor;
		disk.name = vTok[0];
		disk.vendor.clear();
		if (vTok.size() == 5) {
			size_t backslash = vTok[4].find('\\');
			disk.vendor = (backslash == std::string::npos)
				      ? vTok[4] : vTok[4].substr(0, backslash);
		}
		vDisk.push_back(disk);
	}
}

void ParseSizeResult(std::string sOut, std::vector<unsigned long> &vSize)
{
	std::istringstream stream(sOut);
	std::string sLine;
	while (std::getline(stream, sLine)) {
		if (sLine.empty())
			continue;
		std::vector<std::string> vTok;
		split_token(sLine, " ", vTok);
		if (vTok.size() < 2)
			continue;
		unsigned long long ullSize = 0;
		if (sscanf(vTok[1].c_str(), "%llu", &ullSize) != 1)
			continue;
		vSize.push_back((unsigned long)ullSize);
	}
}

void ParseMPResult(std::string sOut, std::string &sMount)
{
	std::istringstream stream(sOut);
	std::string sLine;
	while (std::getline(stream, sLine)) {
		if (sLine.empty())
			continue;
		std::vector<std::string> vTok;
		split_token(sLine, " ", vTok);
		if (vTok.size() != 2)
			continue;
		sMount = vTok[1];
	}
}

} // namespace

CRKDisk::CRKDisk(STRUCT_DISK_BASE diskBase)
{
	m_iDiskId = diskBase.diskId;
	m_iFd = -1;
	m_sDevPath = diskBase.name;
	m_sVolume = diskBase.vendor;
	m_ullDiskSize = 0;
	m_iRemovable = 0;
}

CRKDisk::~CRKDisk()
{
	if (m_iFd >= 0) {
		close(m_iFd);
		m_iFd = -1;
	}
}

bool CRKDisk::OpenDisk()
{
#ifdef _WIN32
	m_iFd = open(m_sDevPath.c_str(), O_RDWR | _O_BINARY);
#else
	m_iFd = open(m_sDevPath.c_str(), O_RDWR | O_SYNC);
#endif
	if (m_iFd < 0) {
		g_lastDiskErr = errno;
		printf("open failed, %s err=%d\r\n", m_sDevPath.c_str(), errno);
		return false;
	}
	off_t off = lseek(m_iFd, 0, SEEK_END);
	if (off == (off_t)-1) {
		g_lastDiskErr = errno;
		printf("seek failed,%d %s err=%d\r\n", m_iFd, m_sDevPath.c_str(), errno);
		return false;
	}
	m_ullDiskSize = (unsigned long long)off;
	lseek(m_iFd, 0, SEEK_SET);
	return true;
}

void CRKDisk::CloseDisk()
{
	if (m_iFd >= 0) {
		close(m_iFd);
		m_iFd = -1;
	}
	m_vVolume.clear();
	m_iDiskId = -1;
	m_sVolume.clear();
	m_sDevPath = m_sVolume;
	m_ullDiskSize = 0;
	m_iRemovable = 0;
}

bool CRKDisk::ReadDisk(unsigned long ulOffset, int iSize, unsigned char *pBuf)
{
	if (m_iDiskId < 0) {
		g_lastDiskErr = DISK_ERR_NO_DISK;
		return false;
	}
	if (m_iFd < 0) {
		g_lastDiskErr = DISK_ERR_NOT_OPEN;
		return false;
	}
	if (lseek(m_iFd, (off_t)ulOffset, SEEK_SET) == (off_t)-1) {
		g_lastDiskErr = errno;
		return false;
	}
	ssize_t n = read(m_iFd, pBuf, (size_t)iSize);
	if (n == (ssize_t)iSize || n == 0)
		return true;
	g_lastDiskErr = DISK_ERR_READ;
	return false;
}

bool CRKDisk::WriteDisk(unsigned long ulOffset, int iSize, unsigned char *pBuf)
{
	if (m_iDiskId < 0) {
		g_lastDiskErr = DISK_ERR_NO_DISK;
		return false;
	}
	if (m_iFd < 0) {
		g_lastDiskErr = DISK_ERR_NOT_OPEN;
		return false;
	}
	if (lseek(m_iFd, (off_t)ulOffset, SEEK_SET) == (off_t)-1) {
		g_lastDiskErr = errno;
		return false;
	}
	ssize_t n = write(m_iFd, pBuf, (size_t)iSize);
	if (n == (ssize_t)iSize) {
#ifdef _WIN32
		if (_commit(m_iFd) != 0) {
			g_lastDiskErr = errno;
			return false;
		}
#endif
		return true;
	}
	g_lastDiskErr = DISK_ERR_WRITE;
	return false;
}

bool CRKDisk::OffsetDisk(unsigned long ulOffset, unsigned int uiWhence)
{
	if (m_iDiskId < 0) {
		g_lastDiskErr = DISK_ERR_NO_DISK;
		return false;
	}
	if (m_iFd < 0) {
		g_lastDiskErr = DISK_ERR_NOT_OPEN;
		return false;
	}
	if (lseek(m_iFd, (off_t)ulOffset, (int)uiWhence) == (off_t)-1) {
		g_lastDiskErr = errno;
		return false;
	}
	return true;
}

unsigned long long CRKDisk::GetDiskSize()
{
	if (m_iDiskId < 0) {
		g_lastDiskErr = DISK_ERR_NO_DISK;
		return 0;
	}
	if (m_iFd < 0) {
		g_lastDiskErr = DISK_ERR_NOT_OPEN;
		return 0;
	}
	return m_ullDiskSize;
}

int CRKDisk::GetLastDiskError()
{
	return g_lastDiskErr;
}

bool CRKDisk::CheckDiskRemovable(int iDiskId, bool &bRemovable)
{
	(void)iDiskId;
	bRemovable = true;
	return true;
}

bool CRKDisk::IsRemovedDisk(bool &bRemoved)
{
	bRemoved = true;
	return true;
}

int CRKDisk::GetAllDisk(std::vector<STRUCT_DISK_BASE> &vDisk)
{
	std::string sOut;
	int iRet = RunCommand(kLsblkDisk, sOut);
	if (iRet != 0)
		return iRet;
	ParseLSblkResult(sOut, vDisk, 1);
	return (int)vDisk.size();
}

int CRKDisk::GetDiskFromVolume(std::string sVolume)
{
	std::string sOut;
	int iRet = RunCommand(kLsblkDisk, sOut);
	if (iRet != 0)
		return iRet;
	std::vector<STRUCT_DISK_BASE> vDisk;
	ParseLSblkResult(sOut, vDisk, 1);
	for (size_t i = 0; i < vDisk.size(); ++i) {
		if (vDisk[i].name == sVolume)
			return vDisk[i].diskId;
	}
	return 0;
}

bool CRKDisk::GetAllDrive(std::vector<std::string> &vDrive, bool bIsVolume)
{
	if (m_iDiskId < 0) {
		g_lastDiskErr = DISK_ERR_NO_DISK;
		return false;
	}
	if (m_iFd < 0) {
		g_lastDiskErr = DISK_ERR_NOT_OPEN;
		return false;
	}
	vDrive.clear();
	if (bIsVolume) {
		if (GetAllVolumeFromDisk(m_iDiskId, m_vVolume) < 0) {
			g_lastDiskErr = DISK_ERR_VOLUME;
			return false;
		}
	}
	for (size_t i = 0; i < m_vVolume.size(); ++i)
		vDrive.push_back(m_vVolume[i]);
	return true;
}

int CRKDisk::GetAllVolumeFromDisk(int iDiskId, std::vector<std::string> &vVolume)
{
	vVolume.clear();
	char szCmd[256];
	int iMajor = iDiskId / 100;
	int iMinor = iDiskId % 100;
	snprintf(szCmd, sizeof(szCmd), kLsblkVol, iDiskId);
	std::string sOut;
	int iRet = RunCommand(szCmd, sOut);
	if (iRet != 0)
		return false;

	std::vector<STRUCT_DISK_BASE> vDisk;
		ParseLSblkResult(sOut, vDisk, 2);
	for (size_t i = 0; i < vDisk.size(); ++i) {
		int id = vDisk[i].diskId;
		if (id / 100 != iMajor)
			continue;
		int part = id % 100;
		if (part > iMinor && part <= iMinor + 15)
			vVolume.push_back(vDisk[i].name);
	}
	return (int)vVolume.size();
}

std::string CRKDisk::VolumeRoot(std::string sVolume)
{
	std::string sCmd = std::string(kLsblkMount) + sVolume;
	std::string sOut;
	if (RunCommand(sCmd, sOut) != 0)
		return std::string();
	ParseMPResult(sOut, sVolume);
	return sVolume;
}

bool CRKDisk::FormatVolume(std::string sVolume)
{
	char szCmd[256];
	snprintf(szCmd, sizeof(szCmd), "mkfs.vfat %s", sVolume.c_str());
	std::string sOut;
	return RunCommand(std::string(szCmd), sOut) == 0;
}

unsigned long long CRKDisk::GetDriveSize(std::string sDrive)
{
	std::string sCmd = std::string(kLsblkSize) + sDrive;
	std::string sOut;
	if (RunCommand(sCmd, sOut) != 0)
		return 0;
	std::vector<unsigned long> vSize;
	ParseSizeResult(sOut, vSize);
	if (vSize.empty())
		return 0;
	return (unsigned long long)vSize[0];
}

bool CRKDisk::UpdateDiskLayout()
{
	if (m_iDiskId < 0) {
		g_lastDiskErr = DISK_ERR_NO_DISK;
		return false;
	}
	if (m_iFd < 0) {
		g_lastDiskErr = DISK_ERR_NOT_OPEN;
		return false;
	}
	std::string sOut;
	return RunCommand(std::string(kPartprobe) + m_sDevPath, sOut) == 0;
}

bool CRKDisk::LockAndDismount()
{
	if (m_iDiskId < 0) {
		g_lastDiskErr = DISK_ERR_NO_DISK;
		return false;
	}
	if (m_iFd < 0) {
		g_lastDiskErr = DISK_ERR_NOT_OPEN;
		return false;
	}
	if (GetAllVolumeFromDisk(m_iDiskId, m_vVolume) < 0) {
		g_lastDiskErr = DISK_ERR_VOLUME;
		return false;
	}

	std::string sDevRoot = VolumeRoot(m_sDevPath);
	std::string sOut;
	for (size_t i = 0; i < m_vVolume.size(); ++i) {
		std::string sRoot = VolumeRoot(m_vVolume[i]);
		if (sRoot.empty())
			continue;
		if (RunCommand(std::string(kUmount) + m_vVolume[i], sOut) != 0)
			return false;
	}
	if (!sDevRoot.empty()) {
		if (RunCommand(std::string(kUmount) + m_sDevPath, sOut) != 0)
			return false;
	}
	return true;
}
