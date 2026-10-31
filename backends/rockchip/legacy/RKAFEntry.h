#ifndef RKAF_ENTRY_HEADER
#define RKAF_ENTRY_HEADER
#include <cstdint>
// RKAF records are 0x70 bytes. Offset (+0x60) is relative to the RKAF base;
// size (+0x6c) is in bytes. Each has an optional unaligned high word marked 'H'.
// Read explicitly as little endian on every host.
namespace rkaf {
inline uint32_t Read32(const unsigned char *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint64_t DataOffset(const unsigned char *p) {
    return uint64_t(Read32(p + 0x60)) | (p[0x52] == 'H' ? uint64_t(Read32(p + 0x53)) << 32 : 0);
}
inline uint64_t DataSize(const unsigned char *p) {
    return uint64_t(Read32(p + 0x6c)) | (p[0x57] == 'H' ? uint64_t(Read32(p + 0x58)) << 32 : 0);
}
}
#endif
