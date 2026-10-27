#ifndef PLATFORM_PLATFORM_PATH_H
#define PLATFORM_PLATFORM_PATH_H

#include <string>

namespace platform {

std::string GetExecutableDir();
std::string GetUserHomeDir();

std::string GetUserConfigDir();

std::string GetUserLogDir();

bool EnsureDir(const std::string &strPath);

bool EnsureDirRecursive(const std::string &strPath);

}

#endif
