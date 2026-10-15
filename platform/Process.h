#ifndef PLATFORM_PROCESS_H
#define PLATFORM_PROCESS_H
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace platform {
class IProcess {
public:
    virtual ~IProcess() = default;
    virtual void Start(const std::string &,const std::vector<std::string> &)=0;
    virtual void Poll()=0;
    virtual bool Running() const=0;
    virtual int ExitCode() const=0;
    virtual bool NormalExit() const=0;
    virtual std::string TakeOutput()=0;
    virtual void Kill()=0;
};
using ProcessFactory=std::function<std::unique_ptr<IProcess>()>;
std::unique_ptr<IProcess> MakeProcess();
}
#endif
