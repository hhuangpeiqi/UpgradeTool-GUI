#pragma once
#include <cstdio>

// Each suite owns a local or member failure counter and returns it to its runner.
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)
