#ifndef BACKENDS_BACKEND_FACTORY_H
#define BACKENDS_BACKEND_FACTORY_H
#include "domain/Backend.h"
namespace platform { class LogService; }
namespace core {
std::vector<std::unique_ptr<IDeviceBackend>> CreateBackends(platform::LogService *log=nullptr);
}
#endif
