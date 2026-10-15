#include "application/ConfigService.h"
#include "platform/File.h"
#include "platform/Text.h"
#include <cstdlib>
#include <sstream>
#include <stdexcept>
namespace core {
ConfigSnapshot ParseConfig(const std::string &text) {
    ConfigSnapshot result; std::istringstream input(text);
    for(std::string line;std::getline(input,line);) {
        line=platform::Trim(line);
        if(line.empty() || line.starts_with('#')) continue;
        const auto equal=line.find('='); if(equal==std::string::npos) continue;
        auto key=platform::FoldCase(platform::Trim(line.substr(0,equal)));
        auto value=platform::Trim(line.substr(equal+1));
        // Retain the first-value precedence of the original parser.
        if(!key.empty() && !value.empty()) result.values.emplace(std::move(key),std::move(value));
    }
    return result;
}
ConfigSnapshot ReadConfig(const std::string &path) {
    if(path.empty() || !std::filesystem::exists(platform::Path(path))) return {};
    platform::File file(platform::Path(path));
    if(file.size()>16*1024*1024) throw std::runtime_error("Configuration file is too large");
    auto bytes=file.read(static_cast<size_t>(file.size()));
    return ParseConfig(std::string(bytes.begin(),bytes.end()));
}
}
