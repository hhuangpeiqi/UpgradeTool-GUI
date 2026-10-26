#pragma once
#include <string>
namespace core::allwinner {
// Opens the official driver's interface without sending any device command.
int ProbeAllwinner(const std::string &instance);
}
