#ifndef PLATFORM_TEXT_H
#define PLATFORM_TEXT_H
#include <cstdint>
#include <string>
namespace platform {
std::string Trim(const std::string &);
std::string Upper(const std::string &);
std::string FoldCase(const std::string &);
std::string PrefixCharacters(const std::string &, size_t);
uint64_t Unsigned(const std::string &, int base = 10);
}
#endif
