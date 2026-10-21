#include "tests/support/TestFiles.h"
#include "AndroidDevice.h"
#include "Upgrade.h"
#include "platform/FileLog.h"
#include "crc.h"
#include "MD5Checksum.h"

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>


static TemporaryDirectory temporary;

static int g_fail = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_fail++; \
	} \
} while (0)

struct WriteCall {
	DWORD pos;
	DWORD count;
};

class FakeComm : public CRKComm
{
public:
	FakeComm() : CRKComm(NULL), readCalls(0), testReadyCurrent(0), writeResult(ERR_SUCCESS) {}
	virtual ~FakeComm() {}

	std::vector<WriteCall> writeCalls;
	std::vector<WriteCall> eraseCalls;
	std::vector<unsigned char> lastWriteData;
	int readCalls;
	DWORD testReadyCurrent;
	int writeResult;
	int resetCalls = 0;
	int resetResult = ERR_SUCCESS;

	int RKU_WriteLBA(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer, BYTE = RWMETHOD_IMAGE)
	{
		if (writeResult != ERR_SUCCESS) return writeResult;
		WriteCall c = { dwPos, dwCount };
		writeCalls.push_back(c);
		lastWriteData.assign(lpBuffer, lpBuffer + (size_t)dwCount * SECTOR_SIZE);
		for (DWORD i = 0; i < dwCount; i++)
			store[dwPos + i].assign(lpBuffer + (size_t)i * SECTOR_SIZE,
			                        lpBuffer + (size_t)(i + 1) * SECTOR_SIZE);
		return ERR_SUCCESS;
	}
	int RKU_EraseLBA(DWORD dwPos, DWORD dwCount)
	{
		WriteCall c = { dwPos, dwCount };
		eraseCalls.push_back(c);
		for (DWORD i = 0; i < dwCount; i++)
			store.erase(dwPos + i);
		return ERR_SUCCESS;
	}
	int RKU_ReadLBA(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer, BYTE = RWMETHOD_IMAGE)
	{
		readCalls++;
		for (DWORD i = 0; i < dwCount; i++) {
			std::map<DWORD, std::vector<unsigned char> >::iterator it = store.find(dwPos + i);
			if (it != store.end())
				memcpy(lpBuffer + (size_t)i * SECTOR_SIZE, &it->second[0], SECTOR_SIZE);
			else
				memset(lpBuffer + (size_t)i * SECTOR_SIZE, 0, SECTOR_SIZE);
		}
		return ERR_SUCCESS;
	}
	int RKU_EraseBlock(BYTE, DWORD, DWORD, BYTE) { return ERR_SUCCESS; }
	int RKU_ReadChipInfo(BYTE *) { return ERR_SUCCESS; }
	int RKU_ReadFlashID(BYTE *) { return ERR_SUCCESS; }
	int RKU_ReadCapability(BYTE *) { return ERR_SUCCESS; }
	int RKU_ReadFlashInfo(BYTE *lpBuffer, UINT *)
	{
		memset(lpBuffer, 0, 512);
		return ERR_SUCCESS;
	}
	int RKU_ResetDevice(BYTE = RST_NONE_SUBCODE) { ++resetCalls; return resetResult; }
	int RKU_TestDeviceReady(DWORD *dwTotal = NULL, DWORD *dwCurrent = NULL, BYTE = TU_NONE_SUBCODE)
	{
		if (dwTotal) *dwTotal = 1;
		if (dwCurrent) *dwCurrent = testReadyCurrent;
		return ERR_SUCCESS;
	}
	int RKU_WriteSector(DWORD, DWORD, BYTE *) { return ERR_SUCCESS; }
	int RKU_DeviceRequest(DWORD, BYTE *, DWORD) { return ERR_SUCCESS; }
	bool Reset_Usb_Config(STRUCT_RKDEVICE_DESC) { return false; }
	bool Reset_Usb_Device() { return false; }
	int RKU_SwitchStorage(BYTE) { return ERR_SUCCESS; }
	int RKU_ReadStorageList(BYTE *) { return ERR_SUCCESS; }
	int RKU_ReadOtpChip(BYTE *) { return ERR_SUCCESS; }
	int RKU_ReadComData(BYTE *, DWORD) { return ERR_SUCCESS; }
	int RKU_ReadSector(DWORD, DWORD, BYTE *) { return ERR_SUCCESS; }
	int RKU_TestBadBlock(BYTE, DWORD, DWORD, BYTE *) { return ERR_SUCCESS; }
	int RKU_ReadNewEfuse(USHORT, USHORT, BYTE *, BYTE) { return ERR_SUCCESS; }
	int RKU_WriteNewEfuse(USHORT, USHORT, BYTE *, BYTE) { return ERR_SUCCESS; }
	int RKU_WriteSDRam(DWORD, BYTE *) { return ERR_SUCCESS; }
	int RKU_RunSDRam() { return ERR_SUCCESS; }
	int RKU_ResetPipe(BYTE) { return ERR_SUCCESS; }
	int RKU_MscToRockusb() { return ERR_SUCCESS; }
	int RKU_UvcToRockusb() { return ERR_SUCCESS; }

	std::map<DWORD, std::vector<unsigned char> > store;

private:
	bool RKU_Write(BYTE *, DWORD) { return false; }
	bool RKU_Read(BYTE *, DWORD) { return false; }
};

struct Probe : public CRKAndroidDevice {
	Probe(STRUCT_RKDEVICE_DESC &x) : CRKAndroidDevice(x) {}
	const FW_ENTRY &Entry(int i) const { return m_entries[i]; }
	UINT Band() const { return m_uiBand; }
	using CRKAndroidDevice::WriteParameter;
	using CRKAndroidDevice::ExtractEntry;
};

class ConnectionProbe : public CRKDevice {
public:
	explicit ConnectionProbe(STRUCT_RKDEVICE_DESC &desc) : CRKDevice(desc) {
		auto *original = new FakeComm;
		original->resetResult = ERR_FAILED;
		SetObject(nullptr, original, nullptr);
	}
	FakeComm *current = nullptr;
	int completedResets = 0;
	int bootResult = ERR_SUCCESS;
	std::stop_source *cancelAfterBoot = nullptr;
	int DownloadBoot() override {
		// Simulate the upgrade's connection replacement after USB re-enumeration.
		completedResets += static_cast<FakeComm *>(m_pComm)->resetCalls;
		delete m_pComm;
		current = new FakeComm;
		m_pComm = current;
		SetUsbType(RKUSB_LOADER);
		if (cancelAfterBoot) cancelAfterBoot->request_stop();
		return bootResult;
	}
};

static void test_upgrade_reboot_connection()
{
	STRUCT_UPGRADE_STEP boot{};
	boot.uiStep = UPGRADE_STEP_DOWNLOAD_BOOT; boot.uiMask = 3;
	STRUCT_UPGRADE_STEP reset{};
	reset.uiStep = UPGRADE_STEP_RESET_DEVICE; reset.uiMask = 3;
	for (auto mode : {RKUSB_MASKROM, RKUSB_LOADER}) {
		for (bool reboot : {false, true}) {
			STRUCT_RKDEVICE_DESC desc{}; desc.emUsbType = mode;
			ConnectionProbe device(desc);
			CRKUpgrade upgrade({boot, reset}, WORKFLOW_UPGRADE, reboot);
			upgrade.SetDeviceObject(&device);
			CHECK(upgrade.Run());
			CHECK(device.current->resetCalls == (reboot ? 1 : 0));
		}
		for (bool cancel : {false, true}) {
			STRUCT_RKDEVICE_DESC desc{}; desc.emUsbType = mode;
			ConnectionProbe device(desc);
			std::stop_source cancellation;
			device.SetCancellation(cancellation.get_token());
			if (cancel) device.cancelAfterBoot = &cancellation;
			else device.bootResult = ERR_FAILED;
			CRKUpgrade upgrade({boot, reset}, WORKFLOW_UPGRADE);
			upgrade.SetDeviceObject(&device);
			CHECK(!upgrade.Run());
			CHECK(device.current->resetCalls == 0);
		}
		{
			STRUCT_RKDEVICE_DESC desc{}; desc.emUsbType = mode;
			Probe device(desc);
			auto *comm = new FakeComm;
			comm->resetResult = ERR_FAILED;
			device.SetObject(nullptr, comm, nullptr);
			CRKUpgrade upgrade({reset, reset}, WORKFLOW_UPGRADE);
			upgrade.SetDeviceObject(&device);
			CHECK(!upgrade.Run());
			CHECK(comm->resetCalls == 1);
		}
		{
			STRUCT_RKDEVICE_DESC desc{}; desc.emUsbType = mode;
			ConnectionProbe device(desc);
			CRKUpgrade upgrade({boot, reset, boot, reset}, WORKFLOW_UPGRADE, false);
			upgrade.SetDeviceObject(&device);
			CHECK(upgrade.Run()); // Intermediate reset is kept even without final reboot.
			CHECK(device.completedResets == 1);
			CHECK(device.current->resetCalls == 0);
		}
	}
}

static void put_le16(unsigned char *p, unsigned short v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_le32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void put_entry(unsigned char *e, const char *part, const char *file,
                      unsigned int partSize, unsigned int partLba,
                      unsigned int dataOff, unsigned int dataSize)
{
	memcpy(e, part, strlen(part));
	memcpy(e + 0x20, file, strlen(file));
	put_le32(e + 0x5C, partSize);
	put_le32(e + 0x60, dataOff);
	put_le32(e + 0x64, partLba);
	put_le32(e + 0x6C, dataSize);
}

static bool write_package(const char *path)
{
	const unsigned int fwOff = 0x1000;
	unsigned char buf[0x4000];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, "RKFW", 4);
	put_le32(buf + 0x21, fwOff);

	put_le32(buf + fwOff + 0x88, 3);

	unsigned char *e0 = buf + fwOff + 0x8C;
	put_entry(e0, "rootfs", "rootfs.img", 0xffffffff, 0x286000, 0x800, 0x400);

	unsigned char *e1 = e0 + 0x70;
	put_entry(e1, "other", "other.img", 0x10, 0xffffffff, 0x1000, 0x200);

	unsigned char *e2 = e1 + 0x70;
	put_entry(e2, "sparse", "sparse.img", 0xffffffff, 0x100000, 0x1800, 0x1028);

	memset(buf + fwOff + 0x800, 0xAA, 0x400);
	memset(buf + fwOff + 0x1000, 0xBB, 0x200);

	unsigned char *sp = buf + fwOff + 0x1800;
	put_le32(sp + 0x00, SPARSE_HEADER_MAGIC);
	put_le16(sp + 0x04, 1);
	put_le16(sp + 0x06, 0);
	put_le16(sp + 0x08, 28);
	put_le16(sp + 0x0A, 12);
	put_le32(sp + 0x0C, 4096);
	put_le32(sp + 0x10, 1);
	put_le32(sp + 0x14, 1);
	put_le32(sp + 0x18, 0);
	put_le16(sp + 0x1C, CHUNK_TYPE_RAW);
	put_le16(sp + 0x1E, 0);
	put_le32(sp + 0x20, 1);
	put_le32(sp + 0x24, 12 + 4096);
	memset(sp + 0x28, 0xCC, 4096);

	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(buf, 1, sizeof(buf), f) == sizeof(buf);
	fclose(f);
	return ok;
}

static void test_entry_mapping()
{
	const auto pathStorage = temporary.filePath("ut_test_download.img");
	const char *path = pathStorage.c_str();
	CHECK(write_package(path));

	STRUCT_RKDEVICE_DESC d;
	memset(&d, 0, sizeof(d));
	d.usVid = 0x2207;
	d.emUsbType = RKUSB_LOADER;
	d.emDeviceType = RK32_DEVICE;

	Probe dev(d);
	CHECK(dev.LoadFirmwarePackage(path));
	CHECK(dev.GetEntryCount() == 3);

	CHECK(strcmp(dev.Entry(0).szPart, "rootfs") == 0);
	CHECK(dev.Entry(0).uiPartLba == 0x286000);
	CHECK(dev.Entry(0).uiPartSize == 0xffffffff);
	CHECK(dev.Entry(0).uiSize == 0x400);
	CHECK(dev.Entry(1).uiPartLba == 0xffffffff);
	CHECK(dev.Entry(2).uiPartLba == 0x100000);

	CHECK(dev.Entry(0).uiDataOff == (u64)0x1000 + 0x800);
	CHECK(dev.Entry(1).uiDataOff == (u64)0x1000 + 0x1000);
	CHECK(dev.Entry(2).uiDataOff == (u64)0x1000 + 0x1800);

	FakeComm *comm = new FakeComm;
	dev.SetObject(NULL, comm, NULL);

	CHECK(dev.DownloadImage() == 0);

	CHECK(comm->writeCalls.size() == 2);
	CHECK(comm->writeCalls[0].pos == 0x286000);
	CHECK(comm->writeCalls[0].count == 2);
	CHECK(comm->writeCalls[1].pos == 0x100000);
	CHECK(comm->writeCalls[1].count == 8);

	CHECK(comm->eraseCalls.size() == 1);
	CHECK(comm->eraseCalls[0].pos == 0x100000);
	CHECK(comm->lastWriteData.size() == 4096);
	bool allCc = true;
	for (size_t i = 0; i < comm->lastWriteData.size(); i++)
		if (comm->lastWriteData[i] != 0xCC) { allCc = false; break; }
	CHECK(allCc);
}

static bool write_shared_package(const char *path)
{
	const unsigned int fwOff = 0x1000;
	unsigned char buf[0x4000];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, "RKFW", 4);
	put_le32(buf + 0x21, fwOff);
	put_le32(buf + fwOff + 0x88, 2);

	unsigned char *e0 = buf + fwOff + 0x8C;
	put_entry(e0, "uboot_a", "uboot.img", 0x2000, 0x100, 0x800, 0x400);
	unsigned char *e1 = e0 + 0x70;
	put_entry(e1, "uboot_b", "uboot.img", 0x2000, 0x140, 0x800, 0x400);

	memset(buf + fwOff + 0x800, 0x5A, 0x400);

	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(buf, 1, sizeof(buf), f) == sizeof(buf);
	fclose(f);
	return ok;
}

static void test_shared_payload()
{
	const auto pathStorage = temporary.filePath("ut_test_shared.img");
	const char *path = pathStorage.c_str();
	CHECK(write_shared_package(path));

	STRUCT_RKDEVICE_DESC d;
	memset(&d, 0, sizeof(d));
	d.usVid = 0x2207;
	d.emUsbType = RKUSB_LOADER;
	d.emDeviceType = RK32_DEVICE;

	Probe dev(d);
	CHECK(dev.LoadFirmwarePackage(path));
	CHECK(dev.GetEntryCount() == 2);

	CHECK(dev.Entry(0).uiDataOff == (u64)0x1000 + 0x800);
	CHECK(dev.Entry(1).uiDataOff == (u64)0x1000 + 0x800);

	FakeComm *comm = new FakeComm;
	dev.SetObject(NULL, comm, NULL);
	CHECK(dev.DownloadImage() == 0);
	CHECK(comm->writeCalls.size() == 2);
	CHECK(comm->writeCalls[0].pos == 0x100);
	CHECK(comm->writeCalls[1].pos == 0x140);

	for (DWORD s = 0; s < 2; s++) {
		CHECK(comm->store[0x100 + s][0] == 0x5A);
		CHECK(comm->store[0x140 + s][0] == 0x5A);
	}
}

static void test_device_band()
{
	const auto pathStorage = temporary.filePath("ut_test_shared.img");
	const char *path = pathStorage.c_str();
	CHECK(write_shared_package(path));

	STRUCT_RKDEVICE_DESC d;
	memset(&d, 0, sizeof(d));
	d.usVid = 0x2207;
	d.emUsbType = RKUSB_LOADER;
	d.emDeviceType = RK32_DEVICE;

	{
		Probe dev(d);
		CHECK(dev.LoadFirmwarePackage(path));
		FakeComm *comm = new FakeComm;
		comm->testReadyCurrent = 6;
		dev.SetObject(NULL, comm, NULL);
		CHECK(dev.DownloadImage() == 0);
		CHECK(dev.Band() == 64);
	}

	{
		Probe dev(d);
		CHECK(dev.LoadFirmwarePackage(path));
		FakeComm *comm = new FakeComm;
		dev.SetObject(NULL, comm, NULL);
		CHECK(dev.DownloadImage() == 0);
		CHECK(dev.Band() == 1);
	}
}

static void test_sparse_block_size()
{
	const auto pathStorage = temporary.filePath("ut_test_sparse_band.img");
	const char *path = pathStorage.c_str();
	const unsigned int fwOff = 0x1000;
	const unsigned int dataOff = 0x2000;
	const unsigned int blk = 4096;
	const unsigned int totalBlks = 256;
	const unsigned int chunkBytes = blk * totalBlks;
	const unsigned int spSize = 28 + 12 + chunkBytes;

	FILE *f = fopen(path, "wb");
	CHECK(f != NULL);
	if (!f)
		return;

	unsigned char hdr[0x1000];
	memset(hdr, 0, sizeof(hdr));
	memcpy(hdr, "RKFW", 4);
	put_le32(hdr + 0x21, fwOff);
	fwrite(hdr, 1, sizeof(hdr), f);

	unsigned char area[0x2000];
	memset(area, 0, sizeof(area));
	put_le32(area + 0x88, 1);
	put_entry(area + 0x8C, "sparse", "sparse.img", 0xffffffff, 0x100000, dataOff, spSize);
	fwrite(area, 1, sizeof(area), f);

	unsigned char sh[28 + 12];
	memset(sh, 0, sizeof(sh));
	put_le32(sh + 0x00, SPARSE_HEADER_MAGIC);
	put_le16(sh + 0x04, 1);
	put_le16(sh + 0x08, 28);
	put_le16(sh + 0x0A, 12);
	put_le32(sh + 0x0C, blk);
	put_le32(sh + 0x10, totalBlks);
	put_le32(sh + 0x14, 1);
	put_le16(sh + 0x1C, CHUNK_TYPE_RAW);
	put_le32(sh + 0x20, totalBlks);
	put_le32(sh + 0x24, 12 + chunkBytes);
	fwrite(sh, 1, sizeof(sh), f);

	unsigned char fill[65536];
	memset(fill, 0xDD, sizeof(fill));
	for (unsigned int w = 0; w < chunkBytes; w += sizeof(fill))
		fwrite(fill, 1, sizeof(fill), f);
	fclose(f);

	STRUCT_RKDEVICE_DESC d;
	memset(&d, 0, sizeof(d));
	d.usVid = 0x2207;
	d.emUsbType = RKUSB_LOADER;
	d.emDeviceType = RK32_DEVICE;

	Probe dev(d);
	CHECK(dev.LoadFirmwarePackage(path));
	FakeComm *comm = new FakeComm;
	comm->testReadyCurrent = 6;
	dev.SetObject(NULL, comm, NULL);
	CHECK(dev.DownloadImage() == 0);
	CHECK(dev.Band() == 64);

	CHECK(comm->writeCalls.size() == 1);
	CHECK(comm->writeCalls[0].pos == 0x100000);
	CHECK(comm->writeCalls[0].count == 2048);
	CHECK(comm->lastWriteData.size() == chunkBytes);
}


static bool write_bytes(const char *path, const std::vector<BYTE> &bytes)
{
	std::ofstream file(platform::Path(path), std::ios::binary);
	file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	file.close(); return bool(file);
}

static void test_parameter_stack()
{
	// Exceeds ExtractEntry's transfer chunk so the streaming loop runs twice.
	std::vector<BYTE> text(70 * 1024, 'x');
	const auto parameterStorage = temporary.filePath("\u6d4b\u8bd5 parameter.txt");
	const char *parameter = parameterStorage.c_str();
	const auto packageStorage = temporary.filePath("\u56fa\u4ef6 package.img");
	const char *package = packageStorage.c_str();
	std::vector<BYTE> wrapped(text.size() + 12, 0);
	put_le32(wrapped.data(), 0x4D524150);
	put_le32(wrapped.data() + 4, text.size());
	memcpy(wrapped.data() + 8, text.data(), text.size());
	put_le32(wrapped.data() + 8 + text.size(), CRC_32(text.data(), text.size()));
	std::vector<BYTE> image(0x1800 + wrapped.size(), 0);
	memcpy(image.data(), "RKFW", 4);
	put_le32(image.data() + 0x21, 0x1000);
	put_le32(image.data() + 0x1088, 1);
	put_entry(image.data() + 0x108C, "parameter", "parameter", 1024, 0x2000,
	          0x800, wrapped.size());
	memcpy(image.data() + 0x1800, wrapped.data(), wrapped.size());
	CHECK(write_bytes(package, image));

	STRUCT_RKDEVICE_DESC d = {};
	d.usVid = 0x2207;
	d.emUsbType = RKUSB_LOADER;
	d.emDeviceType = RK32_DEVICE;
	Probe dev(d);
	FakeComm *comm = new FakeComm;
	dev.SetObject(NULL, comm, NULL);
	CHECK(dev.LoadFirmwarePackage(package));
	CHECK(dev.DownloadImage() == 0);
	CHECK(comm->writeCalls.size() == 1);
	if (!comm->writeCalls.empty()) {
		CHECK(comm->writeCalls[0].pos == 0x2000);
		CHECK(comm->writeCalls[0].count == (wrapped.size() + 511) / 512);
	}
	CHECK(comm->lastWriteData.size() >= wrapped.size());
	if (comm->lastWriteData.size() >= wrapped.size()) {
		CHECK(memcmp(comm->lastWriteData.data(), wrapped.data(), wrapped.size()) == 0);
		for (size_t i = wrapped.size(); i < comm->lastWriteData.size(); ++i)
			CHECK(comm->lastWriteData[i] == 0);
	}

	// File MD5 must agree with the memory path after changing its buffer.
	FILE *f = platform::OpenFile(package, "rb");
	CHECK(f != NULL);
	if (f) {
		CMD5Checksum md5;
		std::string fileDigest = md5.GetMD5(f, 0, image.size());
		CHECK(fileDigest == md5.GetMD5(image.data(), image.size()));
		fclose(f);
	}
	// An unwrapped parameter takes the temporary-file rename branch.
	auto plainImage = image;
	put_le32(plainImage.data() + 0x1800, 0x78787878);
	const auto plainPath = temporary.filePath("\u672a\u5c01\u88c5 package.img");
	CHECK(write_bytes(plainPath.c_str(), plainImage));
	Probe plainDevice(d);
	auto *plainComm = new FakeComm;
	plainDevice.SetObject(NULL, plainComm, NULL);
	CHECK(plainDevice.LoadFirmwarePackage(plainPath.c_str()));
	CHECK(plainDevice.DownloadImage() == 0);
	CHECK(plainComm->writeCalls.size() == 1);
	Probe::FW_ENTRY entry = dev.Entry(0);
	CHECK(!dev.ExtractEntry(entry, temporary.filePath("missing-directory/output").c_str()));
	Probe::FW_ENTRY truncated = dev.Entry(0);
	truncated.uiSize += 1;
	CHECK(!dev.ExtractEntry(truncated, temporary.filePath("ut_test_stack_truncated").c_str()));

	auto paramPath = parameterStorage;
	std::filesystem::remove(platform::Path(parameter));
	CHECK(!dev.WriteParameter(paramPath.data()));
	text.resize(512 * 1024 - 12);
	CHECK(write_bytes(parameter, text));
	CHECK(dev.WriteParameter(paramPath.data()));
	CHECK(comm->lastWriteData.size() == 512 * 1024);
	text.push_back('x');
	CHECK(write_bytes(parameter, text));
	size_t writes = comm->writeCalls.size();
	CHECK(!dev.WriteParameter(paramPath.data()));
	CHECK(comm->writeCalls.size() == writes);
	text.resize(513);
	CHECK(write_bytes(parameter, text));
	comm->writeResult = -1;
	CHECK(!dev.WriteParameter(paramPath.data()));
	std::filesystem::remove(platform::Path(parameter));
	// The device still holds the package open; TemporaryDirectory cleans it after destruction.
	remove(temporary.filePath("ut_test_stack_truncated").c_str());
}

#if defined(__unix__) || defined(__APPLE__)
static void *parameter_thread(void *)
{
	test_parameter_stack();
	return NULL;
}

static void test_small_stack()
{
	pthread_attr_t attr;
	int result = pthread_attr_init(&attr);
	CHECK(result == 0);
	if (result != 0) return;
	result = pthread_attr_setstacksize(&attr, 512 * 1024);
	CHECK(result == 0);
	if (result == 0) {
		pthread_t thread;
		result = pthread_create(&thread, &attr, parameter_thread, NULL);
		CHECK(result == 0);
		if (result == 0) CHECK(pthread_join(thread, NULL) == 0);
	}
	CHECK(pthread_attr_destroy(&attr) == 0);
}
#else
static void test_small_stack() { test_parameter_stack(); }
#endif

int main()
{
	test_upgrade_reboot_connection();
	test_entry_mapping();
	test_shared_payload();
	test_device_band();
	test_sparse_block_size();
#ifdef _WIN32
	// Force the download loop's temporary files under a Unicode user-style path.
	const std::wstring oldTemp = _wgetenv(L"TEMP") ? _wgetenv(L"TEMP") : L"";
	const std::wstring oldTmp = _wgetenv(L"TMP") ? _wgetenv(L"TMP") : L"";
	const auto tempRoot = temporary.path / platform::Path("\u6d4b\u8bd5 temp");
	CHECK(std::filesystem::create_directory(tempRoot));
	CHECK(_wputenv_s(L"TEMP", tempRoot.c_str()) == 0);
	CHECK(_wputenv_s(L"TMP", tempRoot.c_str()) == 0);
	CHECK(std::filesystem::equivalent(std::filesystem::temp_directory_path(), tempRoot));
#endif
	test_small_stack();
#ifdef _WIN32
	CHECK(_wputenv_s(L"TEMP", oldTemp.c_str()) == 0);
	CHECK(_wputenv_s(L"TMP", oldTmp.c_str()) == 0);
	CHECK(std::filesystem::is_empty(tempRoot));
#endif

	if (g_fail == 0) {
		printf("test_download: PASS\n");
		return 0;
	}
	printf("test_download: %d FAILURES\n", g_fail);
	return 1;
}
