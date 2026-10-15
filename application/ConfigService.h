#ifndef APPLICATION_CONFIG_SERVICE_H
#define APPLICATION_CONFIG_SERVICE_H
#include "domain/Config.h"
namespace core {
ConfigSnapshot ReadConfig(const std::string &path);
ConfigSnapshot ParseConfig(const std::string &text);
}
#endif
