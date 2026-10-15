#include "domain/Config.h"
#include "platform/Text.h"
#include <cstdlib>
#include <exception>
namespace core {
std::string ConfigSnapshot::String(const std::string &key) const {
    auto i=values.find(platform::FoldCase(key)); return i==values.end()?std::string{}:i->second;
}
bool ConfigSnapshot::Flag(const std::string &key) const {
    auto value=platform::FoldCase(String(key));
    return value=="true" || value=="yes" || std::atoi(value.c_str())!=0;
}
std::optional<uint32_t> ConfigSnapshot::Number(const std::string &key) const {
    const auto text=String(key); if(text.empty()) return {};
    try {
        uint64_t value=platform::Unsigned(text,text.starts_with("0x")?16:10);
        if(value>UINT32_MAX) return {};
        return static_cast<uint32_t>(value);
    } catch(const std::exception &) { return {}; }
}
}
