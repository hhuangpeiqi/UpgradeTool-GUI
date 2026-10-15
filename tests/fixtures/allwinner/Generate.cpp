#include "FirmwareBuilder.h"
#include <cstdio>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    try {
        Directory output(argv[1]);
        std::filesystem::create_directories(output.path);
        using namespace test_support::allwinner;
        Write(output.filePath("normal.img"), Package(Entries()));
        Write(output.filePath("sparse.img"), Package(Entries(true)));
        Write(output.filePath("advanced.img"), Package(AdvancedEntries()));
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Fixture generation failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
