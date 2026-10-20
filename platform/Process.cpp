#include "platform/Process.h"
#include "platform/File.h"
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <system_error>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
namespace platform {
namespace {
class NativeProcess final : public IProcess {
    const bool killTree;
    bool running=false,normal=false,killed=false;
    int exitCode=-1;
    std::string output;
#ifdef _WIN32
    HANDLE process=nullptr,readPipe=nullptr,job=nullptr;
    static std::wstring Quote(const std::wstring &argument) {
        std::wstring out=L"\""; size_t slashes=0;
        for(wchar_t c:argument) {
            if(c==L'\\') { ++slashes; continue; }
            if(c==L'"') { out.append(slashes*2+1,L'\\'); out+=c; }
            else { out.append(slashes,L'\\'); out+=c; }
            slashes=0;
        }
        out.append(slashes*2,L'\\'); out+=L'"'; return out;
    }
    void Close() {
        if(readPipe) CloseHandle(readPipe);
        if(process) CloseHandle(process);
        if(job) CloseHandle(job);
        readPipe=process=job=nullptr;
    }
#else
    pid_t pid=-1;
    int readPipe=-1;
    void Close() { if(readPipe>=0) close(readPipe); readPipe=-1; }
#endif
    void Append(const char *bytes,size_t count) {
        constexpr size_t limit=16*1024*1024;
        const auto room=limit-output.size(); output.append(bytes,std::min(room,count));
    }
public:
    explicit NativeProcess(bool tree=true) : killTree(tree) {}
    ~NativeProcess() override {
        if(running) { Kill(); while(running) { Poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
        Close();
    }
    void Start(const std::string &executable,const std::vector<std::string> &arguments) override {
        if(running) throw std::logic_error("Process is already running");
        Close(); output.clear(); exitCode=-1; normal=false; killed=false;
#ifdef _WIN32
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE}; HANDLE writePipe=nullptr;
        if(!CreatePipe(&readPipe,&writePipe,&security,0)) throw std::system_error(GetLastError(),std::system_category(),"CreatePipe");
        SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
        std::wstring command=Quote(Path(executable).native());
        for(const auto &arg:arguments) { command+=L' '; command+=Quote(Path(arg).native()); }
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdOutput=startup.hStdError=writePipe; startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION info{};
        const BOOL started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&info);
        const DWORD error=GetLastError(); CloseHandle(writePipe);
        if(!started) { Close(); throw std::system_error(error,std::system_category(),"Cannot start process"); }
        process=info.hProcess;
        job=killTree ? CreateJobObjectW(nullptr,nullptr) : nullptr;
        if(job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) || !AssignProcessToJobObject(job,process)) { CloseHandle(job); job=nullptr; }
        }
        ResumeThread(info.hThread); CloseHandle(info.hThread);
#else
        int pipes[2]; if(pipe(pipes)) throw std::system_error(errno,std::generic_category(),"pipe");
        fcntl(pipes[0],F_SETFD,FD_CLOEXEC); fcntl(pipes[1],F_SETFD,FD_CLOEXEC);
        fcntl(pipes[0],F_SETFL,O_NONBLOCK);
        posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,pipes[1],STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions,pipes[1],STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions,pipes[0]);
        posix_spawn_file_actions_addclose(&actions,pipes[1]);
        posix_spawnattr_t attributes; posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&attributes,0);
        std::vector<std::string> strings{executable}; strings.insert(strings.end(),arguments.begin(),arguments.end());
        std::vector<char *> argv; for(auto &s:strings) argv.push_back(s.data()); argv.push_back(nullptr);
        int error=posix_spawnp(&pid,executable.c_str(),&actions,&attributes,argv.data(),environ);
        posix_spawnattr_destroy(&attributes); posix_spawn_file_actions_destroy(&actions); close(pipes[1]);
        if(error) { close(pipes[0]); pid=-1; throw std::system_error(error,std::generic_category(),"Cannot start process"); }
        readPipe=pipes[0];
#endif
        running=true;
    }
    void Poll() override {
        if(!running) return;
        char bytes[8192];
#ifdef _WIN32
        DWORD available=0;
        while(PeekNamedPipe(readPipe,nullptr,0,nullptr,&available,nullptr) && available) {
            DWORD count=0;
            if(!ReadFile(readPipe,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr) || !count) break;
            Append(bytes,count);
        }
        if(WaitForSingleObject(process,0)==WAIT_OBJECT_0) {
            DWORD code=0; GetExitCodeProcess(process,&code); exitCode=static_cast<int>(code); normal=!killed && code<0x80000000; running=false;
            while(PeekNamedPipe(readPipe,nullptr,0,nullptr,&available,nullptr) && available) {
                DWORD count=0; if(!ReadFile(readPipe,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr) || !count) break;
                Append(bytes,count);
            }
        }
#else
        for(;;) {
            ssize_t count=read(readPipe,bytes,sizeof(bytes));
            if(count>0) Append(bytes,static_cast<size_t>(count));
            else if(count<0 && errno==EINTR) continue;
            else break;
        }
        int status=0; pid_t waited=waitpid(pid,&status,WNOHANG);
        if(waited==pid) {
            normal=!killed && WIFEXITED(status); exitCode=WIFEXITED(status)?WEXITSTATUS(status):-WTERMSIG(status);
            running=false; pid=-1;
            // Drain bytes written between the first read and waitpid.
            for(;;) { ssize_t count=read(readPipe,bytes,sizeof(bytes)); if(count>0) Append(bytes,size_t(count)); else if(count<0&&errno==EINTR) continue; else break; }
        } else if(waited<0 && errno!=EINTR) { running=false; pid=-1; exitCode=-1; }
#endif
    }
    bool Running() const override { return running; }
    int ExitCode() const override { return exitCode; }
    bool NormalExit() const override { return normal; }
    std::string TakeOutput() override { auto result=std::move(output); output.clear(); return result; }
    void Kill() override {
        if(!running) return;
        killed=true;
#ifdef _WIN32
        if(job) TerminateJobObject(job,1); else TerminateProcess(process,1);
#else
        kill(killTree ? -pid : pid,SIGKILL);
#endif
    }
};
}
std::unique_ptr<IProcess> MakeProcess() { return std::make_unique<NativeProcess>(); }
std::unique_ptr<IProcess> MakeAdbProcess() { return std::make_unique<NativeProcess>(false); }
}
