#include "tests/support/TestFiles.h"
#include "RKDisk.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static TemporaryDirectory temporary;

static int g_fail = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_fail++; \
	} \
} while (0)

static const std::string diskPath = temporary.filePath("rkdisk_test.bin");
static const char *kTemp = diskPath.c_str();
static const unsigned long kSize = 4UL * 1024 * 1024;

static void make_file()
{
	std::ofstream out(kTemp, std::ios::binary | std::ios::trunc);
	std::vector<char> buf(0x200);
	for (unsigned long off = 0; off < kSize; off += 0x200) {
		for (size_t i = 0; i < buf.size(); ++i)
			buf[i] = (char)((off / 0x200 + i) & 0xff);
		out.write(buf.data(), buf.size());
	}
}

static void test_open_size_read_write()
{
	STRUCT_DISK_BASE base;
	base.diskId = 800;
	base.name = kTemp;
	base.vendor.clear();

	CRKDisk disk(base);
	CHECK(disk.OpenDisk());
	CHECK(disk.GetDiskSize() == (unsigned long long)kSize);

	unsigned char rd[0x200];
	CHECK(disk.ReadDisk(0x200, 0x200, rd));
	for (size_t i = 0; i < sizeof(rd); ++i)
		CHECK(rd[i] == (unsigned char)((1 + i) & 0xff));

	unsigned char wr[0x200];
	for (size_t i = 0; i < sizeof(wr); ++i)
		wr[i] = (unsigned char)(i & 0xff);
	CHECK(disk.WriteDisk(0x400, 0x200, wr));
	CHECK(disk.ReadDisk(0x400, 0x200, rd));
	CHECK(memcmp(rd, wr, sizeof(wr)) == 0);

	CHECK(disk.OffsetDisk(0, SEEK_SET));
	CHECK(disk.ReadDisk(0, 0x200, rd));
	CHECK(rd[0] == 0x00);

	disk.CloseDisk();
	// Check bytes on disk independently, including LF, CR and Ctrl-Z.
	std::ifstream stored(kTemp, std::ios::binary);
	stored.seekg(0x400);
	stored.read(reinterpret_cast<char *>(rd), sizeof(rd));
	CHECK(stored.gcount() == sizeof(rd));
	CHECK(memcmp(rd, wr, sizeof(wr)) == 0);

	g_lastDiskErr = 0;
	CHECK(!disk.ReadDisk(0, 1, rd));
	CHECK(disk.GetLastDiskError() == DISK_ERR_NO_DISK);
}

static void test_open_failure()
{
	STRUCT_DISK_BASE base;
	base.diskId = 801;
	base.name = temporary.filePath("does_not_exist_rkdisk.bin");
	base.vendor.clear();

	CRKDisk disk(base);
	g_lastDiskErr = 0;
	CHECK(!disk.OpenDisk());
	CHECK(disk.GetLastDiskError() == ENOENT);

	bool removed = false;
	CHECK(disk.IsRemovedDisk(removed));
	CHECK(removed);
	bool removable = false;
	CHECK(disk.CheckDiskRemovable(0, removable));
	CHECK(removable);
}

static void test_disk_size_guard()
{

	STRUCT_DISK_BASE base;
	base.diskId = 802;
	base.name = kTemp;
	base.vendor.clear();

	CRKDisk disk(base);
	unsigned char buf[1];
	g_lastDiskErr = 0;
	CHECK(!disk.ReadDisk(0, 1, buf));
	CHECK(disk.GetLastDiskError() == DISK_ERR_NOT_OPEN);
	CHECK(disk.GetDiskSize() == 0);
}

int main()
{
	make_file();
	test_open_size_read_write();
	test_open_failure();
	test_disk_size_guard();
	remove(kTemp);

	if (g_fail == 0) {
		printf("test_rkdisk: PASS\n");
		return 0;
	}
	printf("test_rkdisk: %d FAILURES\n", g_fail);
	return 1;
}
