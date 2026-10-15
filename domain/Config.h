#ifndef DOMAIN_CONFIG_H
#define DOMAIN_CONFIG_H
#include <cstdint>
#include <map>
#include <optional>
#include <string>
namespace core {
struct ConfigSnapshot {
    std::map<std::string,std::string> values;
    std::string String(const std::string &key) const;
    bool Flag(const std::string &key) const;
    std::optional<uint32_t> Number(const std::string &key) const;
};
}
#endif
