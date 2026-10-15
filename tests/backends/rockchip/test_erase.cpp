#include "AndroidDevice.h"
#include "platform/FileLog.h"

#include <cstdio>
#include <cstring>
#include <vector>


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

struct BlockCall {
	BYTE cs;
	DWORD pos;
	DWORD count;
	BYTE type;
};

class FakeComm : public CRKComm
{
public:
	FakeComm() : CRKComm(NULL), eraseRet(ERR_SUCCESS), writeRet(ERR_SUCCESS),
	             blockRet(ERR_SUCCESS), flashIdIsEmmc(false),
	             flashInfoFlashSize(0), flashInfoBlockSize(0),
	             testReadyStatus(ERR_SUCCESS), testReadyCalls(0) {}
	virtual ~FakeComm() {}

	int eraseRet;
	int writeRet;
	int blockRet;
	bool flashIdIsEmmc;
	UINT flashInfoFlashSize;
	USHORT flashInfoBlockSize;
	int testReadyStatus;
	int testReadyCalls;
	std::vector<WriteCall> eraseCalls;
	std::vector<WriteCall> writeCalls;
	std::vector<BlockCall> blockCalls;
	std::vector<BYTE> testReadySubCodes;
	std::vector<unsigned char> lastWriteData;

	virtual int RKU_EraseLBA(DWORD dwPos, DWORD dwCount)
	{
		WriteCall c = { dwPos, dwCount };
		eraseCalls.push_back(c);
		return eraseRet;
	}
	virtual int RKU_WriteLBA(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer, BYTE bySubCode = RWMETHOD_IMAGE)
	{
		(void)bySubCode;
		WriteCall c = { dwPos, dwCount };
		writeCalls.push_back(c);
		lastWriteData.assign(lpBuffer, lpBuffer + (size_t)dwCount * SECTOR_SIZE);
		return writeRet;
	}

	virtual int RKU_EraseBlock(BYTE cs, DWORD pos, DWORD count, BYTE type)
	{
		BlockCall c = { cs, pos, count, type };
		blockCalls.push_back(c);
		return blockRet;
	}
	virtual int RKU_ReadChipInfo(BYTE *) { return ERR_SUCCESS; }
	virtual int RKU_ReadFlashID(BYTE *lpBuffer)
	{
		memset(lpBuffer, 0, 5);
		if (flashIdIsEmmc)
			memcpy(lpBuffer, "EMMC", 4);
		return ERR_SUCCESS;
	}
	virtual int RKU_ReadCapability(BYTE *lpBuffer) { memset(lpBuffer, 0, 8); return ERR_SUCCESS; }
	virtual int RKU_ReadFlashInfo(BYTE *lpBuffer, UINT *)
	{
		memset(lpBuffer, 0, 512);

		*(UINT *)lpBuffer = flashInfoFlashSize;
		*(USHORT *)(lpBuffer + 4) = flashInfoBlockSize;
		return ERR_SUCCESS;
	}
	virtual int RKU_ReadLBA(DWORD, DWORD, BYTE *, BYTE = RWMETHOD_IMAGE) { return ERR_SUCCESS; }
	virtual int RKU_ResetDevice(BYTE = RST_NONE_SUBCODE) { return ERR_SUCCESS; }
	virtual int RKU_TestDeviceReady(DWORD *t, DWORD *c, BYTE sub = TU_NONE_SUBCODE)
	{
		(void)t; (void)c;
		testReadyCalls++;
		testReadySubCodes.push_back(sub);
		return testReadyStatus;
	}
	virtual int RKU_WriteSector(DWORD, DWORD, BYTE *) { return ERR_SUCCESS; }
	virtual int RKU_DeviceRequest(DWORD, BYTE *, DWORD) { return ERR_SUCCESS; }
	virtual bool Reset_Usb_Config(STRUCT_RKDEVICE_DESC) { return false; }
	virtual bool Reset_Usb_Device() { return false; }
	virtual int RKU_SwitchStorage(BYTE) { return ERR_SUCCESS; }
	virtual int RKU_ReadStorageList(BYTE *) { return ERR_SUCCESS; }
	virtual int RKU_ReadOtpChip(BYTE *) { return ERR_SUCCESS; }
	virtual int RKU_ReadComData(BYTE *, DWORD) { return ERR_SUCCESS; }
	virtual int RKU_ReadSector(DWORD, DWORD, BYTE *) { return ERR_SUCCESS; }
	virtual int RKU_TestBadBlock(BYTE, DWORD, DWORD, BYTE *) { return ERR_SUCCESS; }
	virtual int RKU_ReadNewEfuse(USHORT, USHORT, BYTE *, BYTE) { return ERR_SUCCESS; }
	virtual int RKU_WriteNewEfuse(USHORT, USHORT, BYTE *, BYTE) { return ERR_SUCCESS; }
	virtual int RKU_WriteSDRam(DWORD, BYTE *) { return ERR_SUCCESS; }
	virtual int RKU_RunSDRam() { return ERR_SUCCESS; }
	virtual int RKU_ResetPipe(BYTE) { return ERR_SUCCESS; }
	virtual int RKU_MscToRockusb() { return ERR_SUCCESS; }
	virtual int RKU_UvcToRockusb() { return ERR_SUCCESS; }

private:
	virtual bool RKU_Write(BYTE *, DWORD) { return false; }
	virtual bool RKU_Read(BYTE *, DWORD) { return false; }
};

static STRUCT_RKDEVICE_DESC make_desc()
{
	STRUCT_RKDEVICE_DESC d;
	memset(&d, 0, sizeof(d));
	d.usVid = 0x2207;
	d.usPid = 0x350b;
	d.emUsbType = RKUSB_LOADER;
	d.emDeviceType = RK32_DEVICE;
	return d;
}

struct Probe : public CRKAndroidDevice {
	Probe(STRUCT_RKDEVICE_DESC &x) : CRKAndroidDevice(x) {}
	void SetEraseUnit(UINT v) { m_uiEraseSectorPerUnit = v; }
};

static void test_sparse_region()
{
	STRUCT_RKDEVICE_DESC d = make_desc();
	Probe dev(d);
	FakeComm *comm = new FakeComm;
	dev.SetObject(NULL, comm, NULL);

	CHECK(dev.EraseSparseRegion(0, 0, 0x1000));
	CHECK(comm->eraseCalls.empty());

	comm->eraseCalls.clear();
	CHECK(dev.EraseSparseRegion(0x100, 0x3000, 0x1000));
	CHECK(comm->eraseCalls.size() == 3);
	CHECK(comm->eraseCalls[0].pos == 0x100 && comm->eraseCalls[0].count == 0x1000);
	CHECK(comm->eraseCalls[1].pos == 0x1100 && comm->eraseCalls[1].count == 0x1000);
	CHECK(comm->eraseCalls[2].pos == 0x2100 && comm->eraseCalls[2].count == 0x1000);

	comm->eraseCalls.clear();
	CHECK(dev.EraseSparseRegion(0, 0x1800, 0x1000));
	CHECK(comm->eraseCalls.size() == 2);
	CHECK(comm->eraseCalls[1].count == 0x800);

	comm->eraseCalls.clear();
	comm->eraseRet = ERR_FOUND_BAD_BLOCK;
	CHECK(dev.EraseSparseRegion(0, 0x1000, 0x1000));
	comm->eraseRet = ERR_FAILED;
	CHECK(!dev.EraseSparseRegion(0, 0x1000, 0x1000));
	CHECK(dev.GetError() == 0x65);
}

static void test_direct_lba_block()
{
	STRUCT_RKDEVICE_DESC d = make_desc();
	Probe *pdev = new Probe(d);
	FakeComm *comm = new FakeComm;
	pdev->SetObject(NULL, comm, NULL);

	CHECK(pdev->EraseDirectLBABlock(0, 0));
	CHECK(comm->writeCalls.empty());

	pdev->SetEraseUnit(0x40);
	comm->writeCalls.clear();
	CHECK(pdev->EraseDirectLBABlock(0, 3));

	CHECK(comm->writeCalls.size() == 6);
	CHECK(comm->writeCalls[0].pos == 0x00 && comm->writeCalls[0].count == 0x20);
	CHECK(comm->writeCalls[1].pos == 0x20 && comm->writeCalls[1].count == 0x20);
	CHECK(comm->writeCalls[2].pos == 0x40 && comm->writeCalls[2].count == 0x20);
	CHECK(comm->writeCalls[5].pos == 0xa0 && comm->writeCalls[5].count == 0x20);

	CHECK(!comm->lastWriteData.empty());
	bool allFf = true;
	for (size_t i = 0; i < comm->lastWriteData.size(); i++)
		if (comm->lastWriteData[i] != 0xff) { allFf = false; break; }
	CHECK(allFf);

	comm->writeRet = ERR_FAILED;
	CHECK(!pdev->EraseDirectLBABlock(0, 1));
	CHECK(pdev->GetError() == 0x65);
	delete pdev;
}

static void test_ubi_region()
{

	{
		STRUCT_RKDEVICE_DESC d = make_desc();
		Probe dev(d);
		FakeComm *comm = new FakeComm;
		comm->flashIdIsEmmc = true;
		dev.SetObject(NULL, comm, NULL);
		CHECK(dev.EraseUbiRegion(0x1000, 0x2000));
		CHECK(comm->blockCalls.empty());
	}

	{
		STRUCT_RKDEVICE_DESC d = make_desc();
		Probe dev(d);
		FakeComm *comm = new FakeComm;
		comm->flashInfoFlashSize = 32 * 1024 * 1024;
		comm->flashInfoBlockSize = 0x400;
		dev.SetObject(NULL, comm, NULL);

		CHECK(dev.EraseUbiRegion(0x800, 0x1800));
		CHECK(comm->blockCalls.size() == 1);
		CHECK(comm->blockCalls[0].pos == 2 && comm->blockCalls[0].count == 6);
		CHECK(comm->blockCalls[0].type == ERASE_FORCE);

		comm->blockCalls.clear();
		CHECK(dev.EraseUbiRegion(0, 0x401));
		CHECK(comm->blockCalls.size() == 1 && comm->blockCalls[0].count == 2);
	}

	{
		STRUCT_RKDEVICE_DESC d = make_desc();
		Probe dev(d);
		FakeComm *comm = new FakeComm;
		comm->blockRet = ERR_FAILED;
		comm->flashInfoBlockSize = 0x400;
		dev.SetObject(NULL, comm, NULL);
		CHECK(!dev.EraseUbiRegion(0, 0x400));
		CHECK(dev.GetError() == 0x65);
	}
}

static void test_test_unit_poll()
{

	STRUCT_RKDEVICE_DESC d = make_desc();
	Probe dev(d);
	FakeComm *comm = new FakeComm;
	comm->testReadyStatus = ERR_SUCCESS;
	dev.SetObject(NULL, comm, NULL);
	CHECK(dev.EraseSystemPart(0x1000));
	CHECK(comm->testReadyCalls == 1);
	CHECK(comm->testReadySubCodes[0] == TU_ERASESYSTEM_SUBCODE);

	comm->testReadyCalls = 0;
	comm->testReadySubCodes.clear();
	comm->testReadyStatus = ERR_FAILED;
	CHECK(!dev.EraseUserDataPart(0x1000));
	CHECK(comm->testReadyCalls == 3);
	CHECK(comm->testReadySubCodes[0] == TU_ERASEUSERDATA_SUBCODE);
}

int main()
{
	test_sparse_region();
	test_direct_lba_block();
	test_ubi_region();
	test_test_unit_poll();

	if (g_fail == 0) {
		printf("test_erase: PASS\n");
		return 0;
	}
	printf("test_erase: %d FAILURES\n", g_fail);
	return 1;
}
