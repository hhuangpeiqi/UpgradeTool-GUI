#include "backends/qualcomm/PathCompat.h"
#include "backends/qualcomm/qdl_bridge.h"
#include "tests/support/TestFiles.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static int failures;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static void Write(const std::string &path, const std::string &bytes) {
    platform::AtomicFile file(platform::Path(path));
    file.write(bytes.data(), bytes.size()); file.commit();
}
static std::string Read(const std::string &path) {
    platform::File file(platform::Path(path));
    const auto bytes = file.read(static_cast<size_t>(file.size()));
    return {bytes.begin(), bytes.end()};
}
struct Capture { std::string logs; };
static void Log(void *user, int, const char *text) { static_cast<Capture *>(user)->logs += text; }
static const std::string program =
    "<data><program SECTOR_SIZE_IN_BYTES=\"512\" filename=\"镜像/boot.bin\" label=\"boot\" "
    "sparse=\"false\" file_sector_offset=\"0\" num_partition_sectors=\"2\" "
    "physical_partition_number=\"0\" start_sector=\"34\"/></data>";

int main(int argc, char **argv) {
    TemporaryDirectory temporary;
    Directory firmware(temporary.filePath("中文目录 空格"));
    std::filesystem::create_directories(firmware.path/platform::Path("镜像"));
    auto path = [&](const std::string &name) { return firmware.filePath(name); };
    const auto loader = path("引导.mbn"), xml = path("刷机.xml"), patch = path("补丁.xml");
    const auto image = path("镜像/BOOT.bin");
    Write(loader, std::string(512, 'L')); Write(image, std::string(1024, 'B'));
    Write(xml, program);
    Write(patch, "<patches><patch SECTOR_SIZE_IN_BYTES=\"512\" byte_offset=\"0\" filename=\"DISK\" "
                 "physical_partition_number=\"0\" size_in_bytes=\"4\" start_sector=\"34\" "
                 "value=\"1\" what=\"test\"/></patches>");
    const char *files[] = {xml.c_str(), patch.c_str()};
    qtq_request request; qtq_defaults(&request);
    request.programmer = loader.c_str(); request.xml = files; request.xml_count = 2; request.dry_run = 1;
    Capture capture; qtq_callbacks hooks{}; hooks.user = &capture; hooks.log = Log;
    CHECK(qtq_execute(&request, &hooks) == 0);
    CHECK(capture.logs.find("flashed \"boot\" successfully") != std::string::npos);
    // Relative directories as well as the filename can differ in ASCII case.
    std::filesystem::create_directory(firmware.path/"Images");
    Write(path("Images/System.BIN"), "image");
    char *resolved = nullptr;
    CHECK(qtq_resolve_image("images/system.bin", xml.c_str(), nullptr, nullptr, &resolved) == 1);
    CHECK(resolved && platform::SameFile(platform::Path(resolved), platform::Path(path("Images/System.BIN"))));
    std::free(resolved); resolved = nullptr;
    CHECK(qtq_resolve_image(image.c_str(), xml.c_str(), nullptr, nullptr, &resolved) == 1);
    CHECK(resolved && platform::SameFile(platform::Path(resolved), platform::Path(image)));
    std::free(resolved); resolved = nullptr;

    // Native matches precede case fallback. On case-insensitive filesystems,
    // the include-dir BOOT.bin is already a native match for boot.bin.
    Directory include(path("额外目录")); std::filesystem::create_directory(include.path);
    Write(include.filePath("BOOT.bin"), "include"); Write(path("boot.bin"), "exact");
    const auto includeDir = platform::Utf8(include.path);
    CHECK(qtq_resolve_image("boot.bin", xml.c_str(), nullptr, includeDir.c_str(), &resolved) == 1);
    const bool nativeIncludeMatch = std::filesystem::exists(platform::Path(include.filePath("boot.bin")));
    const auto expected = nativeIncludeMatch ? include.filePath("BOOT.bin") : path("boot.bin");
    CHECK(resolved && platform::SameFile(platform::Path(resolved), platform::Path(expected)));
    std::free(resolved); resolved = nullptr;
    // A relative CWD filename is also part of exact lookup, after the XML dir.
    const auto absoluteImage = platform::Path(image);
    const auto previousDirectory = std::filesystem::current_path();
    std::filesystem::current_path(firmware.path);
    CHECK(qtq_resolve_image("镜像/BOOT.bin", "other/definition.xml", nullptr, nullptr, &resolved) == 1);
    CHECK(resolved && platform::SameFile(platform::Path(resolved), absoluteImage));
    std::free(resolved); resolved = nullptr;
    std::filesystem::current_path(previousDirectory);

    // Exercise UTF-8 paths through each filesystem adapter.
    const auto outputDir = path("输出目录"), output = path("输出目录/备份.bin"), renamed = path("输出目录/重命名.bin");
    CHECK(qtq_path_mkdir(outputDir.c_str()) == 0);
    CHECK(qtq_path_is_directory(outputDir.c_str()) == 1);
    FILE *file = qtq_path_fopen(output.c_str(), "wb"); CHECK(file);
    if (file) { CHECK(std::fwrite("backup", 1, 6, file) == 6); CHECK(std::fclose(file) == 0); }
    CHECK(qtq_path_access(output.c_str(), 0) == 0);
    CHECK(Read(output) == "backup");
    const int fd = qtq_path_open(output.c_str(), O_RDONLY, 0); CHECK(fd >= 0);
    if (fd >= 0) {
#ifdef _WIN32
        _close(fd);
#else
        close(fd);
#endif
    }
    CHECK(qtq_path_rename(output.c_str(), renamed.c_str()) == 0);
    CHECK(qtq_path_unlink(renamed.c_str()) == 0);
    CHECK(!std::filesystem::exists(platform::Path(renamed)));
    CHECK(qtq_path_open(nullptr, O_RDONLY, 0) == -1 && errno == EINVAL);
#ifdef _WIN32
    CHECK(qtq_path_open("\xff", O_RDONLY, 0) == -1 && errno == EINVAL);
#endif
    const auto archive = path("引导存档.bin");
    request.command = QTQ_CREATE_ARCHIVE; request.output = archive.c_str();
    CHECK(qtq_execute(&request, &hooks) == 0);
    request.command = QTQ_FLASH; request.programmer = archive.c_str();
    CHECK(qtq_execute(&request, &hooks) == 0); request.programmer = loader.c_str();
    const auto digests = path("摘要目录"); CHECK(qtq_path_mkdir(digests.c_str()) == 0);
    request.digest_dir = digests.c_str(); CHECK(qtq_execute(&request, &hooks) == 0);
    CHECK(std::filesystem::file_size(platform::Path(digests)/"DigestsToSign.bin") > 0);
    request.digest_dir = nullptr;
    const auto contents = path("固件清单.xml"), zipped = path("输出目录/固件包.zip");
    Write(contents, "<contents><builds_flat><build><windows_root_path>./</windows_root_path>"
                    "<linux_root_path>./</linux_root_path><device_programmer storage_type=\"ufs\">"
                    "<file_name>引导.mbn</file_name><file_path>./</file_path></device_programmer>"
                    "<partition_file storage_type=\"ufs\"><file_name>刷机.xml</file_name><file_path>./</file_path>"
                    "</partition_file><partition_patch_file storage_type=\"ufs\"><file_name>补丁.xml</file_name>"
                    "<file_path>./</file_path></partition_patch_file></build></builds_flat></contents>");
    request.command = QTQ_CREATE_ZIP; request.package = contents.c_str(); request.output = zipped.c_str();
    capture.logs.clear();
    const int zipResult = qtq_execute(&request, &hooks);
    if (zipResult) std::fprintf(stderr, "%s", capture.logs.c_str());
    CHECK(zipResult == 0);
    request.command = QTQ_PACKAGE; request.package = zipped.c_str();
    CHECK(qtq_execute(&request, &hooks) == 0);
    request.command = QTQ_FLASH; request.package = nullptr;

    // Never let allow_missing hide ambiguity on a case-sensitive filesystem.
    const auto second = path("镜像/Boot.bin");
    Write(second, std::string(1024, 'C'));
    if (!platform::SameFile(platform::Path(image), platform::Path(second))) {
        capture.logs.clear(); request.allow_missing = 1;
        CHECK(qtq_execute(&request, &hooks) != 0);
        CHECK(capture.logs.find("Ambiguous image") != std::string::npos);
        CHECK(capture.logs.find("BOOT.bin") != std::string::npos && capture.logs.find("Boot.bin") != std::string::npos);
        // An exact filename still wins when other case variants exist.
        Write(path("镜像/boot.bin"), std::string(1024, 'E'));
        CHECK(qtq_execute(&request, &hooks) == 0);
        std::filesystem::remove(platform::Path(path("镜像/boot.bin")));
    }
    std::filesystem::remove(platform::Path(second));
    std::filesystem::remove(platform::Path(image));
    capture.logs.clear(); request.allow_missing = 0;
    CHECK(qtq_execute(&request, &hooks) != 0);
    CHECK(capture.logs.find(xml) != std::string::npos && capture.logs.find("Searched:") != std::string::npos);
    request.allow_missing = 1; CHECK(qtq_execute(&request, &hooks) == 0);

    // Optional regression against an existing firmware directory; always offline.
    if (argc > 1) {
        Directory real(argv[1]);
        const auto realLoader = real.filePath("prog_emmc_firehose_8953_ddr.mbn");
        const auto realXml = real.filePath("rawprogram_unsparse.xml"), realPatch = real.filePath("patch0.xml");
        const char *realFiles[] = {realXml.c_str(), realPatch.c_str()};
        qtq_defaults(&request); request.dry_run = 1; request.programmer = realLoader.c_str();
        request.xml = realFiles; request.xml_count = 2; capture.logs.clear();
        CHECK(qtq_execute(&request, &hooks) == 0);
        CHECK(capture.logs.find("NON-HLOS.bin") != std::string::npos);
        std::printf("Real firmware dry-run completed\n");
    }
    return failures ? 1 : 0;
}
