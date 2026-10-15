#include "platform/SerialPort.h"
#include "platform/File.h"
#include <algorithm>
#include <cerrno>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif
namespace platform {
namespace {
[[noreturn]] void Fail(const char *what) {
#ifdef _WIN32
    throw std::system_error(GetLastError(),std::system_category(),what);
#else
    throw std::system_error(errno,std::generic_category(),what);
#endif
}
class Serial final : public IByteStream {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
#else
    int handle=-1;
#endif
public:
    explicit Serial(const std::string &path) {
#ifdef _WIN32
        auto native=Path(path).native(); if(!native.starts_with(L"\\\\.\\")) native=L"\\\\.\\"+native;
        handle=CreateFileW(native.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(handle==INVALID_HANDLE_VALUE) Fail("Cannot open diagnostic port");
        DCB state{}; state.DCBlength=sizeof(state);
        if(!GetCommState(handle,&state)) { CloseHandle(handle); Fail("Cannot read serial configuration"); }
        state.BaudRate=CBR_115200; state.ByteSize=8; state.Parity=NOPARITY; state.StopBits=ONESTOPBIT;
        state.fBinary=TRUE; state.fParity=FALSE; state.fOutxCtsFlow=FALSE; state.fOutxDsrFlow=FALSE;
        state.fDtrControl=DTR_CONTROL_DISABLE; state.fRtsControl=RTS_CONTROL_DISABLE; state.fOutX=state.fInX=FALSE;
        COMMTIMEOUTS limits{}; limits.ReadIntervalTimeout=MAXDWORD; limits.ReadTotalTimeoutConstant=50; limits.WriteTotalTimeoutConstant=50;
        if(!SetCommState(handle,&state) || !SetCommTimeouts(handle,&limits)) { CloseHandle(handle); Fail("Cannot configure diagnostic port"); }
#else
        handle=open(Path(path).c_str(),O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
        if(handle<0) Fail("Cannot open diagnostic port");
        termios state{};
        if(tcgetattr(handle,&state)) { close(handle); Fail("Cannot read serial configuration"); }
        cfmakeraw(&state); cfsetispeed(&state,B115200); cfsetospeed(&state,B115200);
        state.c_cflag|=CLOCAL|CREAD; state.c_cflag&=~(PARENB|CSTOPB|CRTSCTS); state.c_cflag=(state.c_cflag&~CSIZE)|CS8;
        if(tcsetattr(handle,TCSANOW,&state)) { close(handle); Fail("Cannot configure diagnostic port"); }
#endif
    }
    ~Serial() override {
#ifdef _WIN32
        CloseHandle(handle);
#else
        close(handle);
#endif
    }
    void Clear() override {
#ifdef _WIN32
        if(!PurgeComm(handle,PURGE_RXCLEAR|PURGE_TXCLEAR)) Fail("Cannot clear diagnostic port");
#else
        if(tcflush(handle,TCIOFLUSH)) Fail("Cannot clear diagnostic port");
#endif
    }
    size_t Write(std::span<const uint8_t> bytes,std::chrono::milliseconds timeout,std::stop_token stop) override {
        size_t written=0; const auto deadline=std::chrono::steady_clock::now()+timeout;
        while(written<bytes.size() && !stop.stop_requested() && std::chrono::steady_clock::now()<deadline) {
#ifdef _WIN32
            DWORD count=0;
            if(!WriteFile(handle,bytes.data()+written,static_cast<DWORD>(std::min<size_t>(8192,bytes.size()-written)),&count,nullptr)) Fail("Diagnostic write failed");
            written+=count;
#else
            pollfd ready{handle,POLLOUT,0}; int rc=poll(&ready,1,50);
            if(rc<0 && errno==EINTR) continue;
            if(rc<0) Fail("Diagnostic poll failed");
            if(!rc) continue;
            auto count=write(handle,bytes.data()+written,bytes.size()-written);
            if(count>0) written+=static_cast<size_t>(count);
            else if(count<0 && errno!=EAGAIN && errno!=EINTR) Fail("Diagnostic write failed");
#endif
        }
        return written;
    }
    std::vector<uint8_t> Read(std::chrono::milliseconds timeout,std::stop_token stop) override {
        if(stop.stop_requested()) return {};
        std::vector<uint8_t> bytes(8192);
#ifdef _WIN32
        (void)timeout; DWORD count=0;
        if(!ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)) Fail("Diagnostic read failed");
#else
        pollfd ready{handle,POLLIN,0}; int rc=poll(&ready,1,static_cast<int>(std::clamp<int64_t>(timeout.count(),0,50)));
        if(rc<0 && errno==EINTR) return {};
        if(rc<0) Fail("Diagnostic poll failed");
        if(!rc) return {};
        auto count=read(handle,bytes.data(),bytes.size());
        if(count<0 && (errno==EAGAIN || errno==EINTR)) return {};
        if(count<0) Fail("Diagnostic read failed");
        if(!count && (ready.revents&(POLLHUP|POLLERR))) throw std::runtime_error("Diagnostic port disconnected");
#endif
        bytes.resize(static_cast<size_t>(count)); return bytes;
    }
};
}
std::unique_ptr<IByteStream> OpenSerialPort(const std::string &path) { return std::make_unique<Serial>(path); }
}
