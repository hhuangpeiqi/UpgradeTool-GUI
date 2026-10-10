#ifndef ALLWINNER_FIRMWARE_H
#define ALLWINNER_FIRMWARE_H
#include "core/OperationResult.h"
#include <QFile>
#include <functional>
#include <map>
namespace core { namespace allwinner {
using Bytes = std::vector<uint8_t>;
uint16_t U16(const uint8_t *p);
uint32_t U32(const uint8_t *p);
uint64_t U64(const uint8_t *p);
void Put32(uint8_t *p, uint32_t v);
void Put64(uint8_t *p, uint64_t v);
uint32_t Crc32(const uint8_t *p, size_t n);
uint64_t Add(uint64_t a, uint64_t b);
uint64_t Mul(uint64_t a, uint64_t b);
std::string Text(const uint8_t *p, size_t n);
struct Entry { std::string name, mainType, subType; uint64_t offset = 0, size = 0, storedSize = 0; };
struct Partition { std::string name, file; uint64_t address = 0, sectors = 0; bool readonly = false; };
struct ConfigPartition { std::string name, file; uint64_t sectors = 0; };
struct SparseChunk { uint16_t type; uint64_t offset, outputOffset, size; uint32_t fill; };
class Firmware {
public:
    explicit Firmware(const std::string &path);
    uint32_t headerVersion = 0, version = 0, attr = 0;
    uint64_t size = 0;
    std::vector<Entry> entries;
    const Entry *Find(const std::string &nameOrSubtype, const std::string &mainType = "") const;
    const Entry &Require(const std::string &subtype, const std::string &mainType = "") const;
    Bytes Read(const Entry &entry, uint64_t offset = 0, uint64_t count = UINT64_MAX);
    std::vector<Partition> Partitions();
    std::vector<ConfigPartition> ConfigPartitions();
    FirmwareInfo Info();
    void Extract(const std::string &dir, const Cancellation &, const std::function<void(uint64_t,uint64_t)> &);
    static std::string Subtype(const std::string &file);
private:
    QFile file;
};
OperationResult InspectFirmware(const std::string &path);
std::vector<Partition> ParseMbr(const Bytes &data);
Bytes RawMbr(uint64_t address, uint64_t sectors, unsigned copies);
std::vector<SparseChunk> SparseLayout(Firmware &, const Entry &, uint64_t &expandedSize);
uint64_t ParseCapacity(const std::string &text);
void Convert(Firmware &, const OperationRequest &, const Cancellation &,
             const std::function<void(const std::string &, uint64_t,uint64_t)> &, OperationResult &);
} }
#endif
