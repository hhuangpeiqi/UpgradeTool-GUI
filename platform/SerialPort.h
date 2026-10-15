#ifndef PLATFORM_SERIAL_PORT_H
#define PLATFORM_SERIAL_PORT_H
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>
namespace platform {
class IByteStream {
public:
    virtual ~IByteStream() = default;
    virtual void Clear()=0;
    virtual size_t Write(std::span<const uint8_t>,std::chrono::milliseconds,std::stop_token)=0;
    virtual std::vector<uint8_t> Read(std::chrono::milliseconds,std::stop_token)=0;
};
std::unique_ptr<IByteStream> OpenSerialPort(const std::string &path);
}
#endif
