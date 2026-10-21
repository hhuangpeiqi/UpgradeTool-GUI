#include "platform/Process.h"
#include "platform/SerialPort.h"
#include "platform/Text.h"
#include "platform/File.h"
#include "tests/support/TestFiles.h"
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif
static int failures=0;
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); ++failures; } } while(0)
static bool Wait(platform::IProcess &process,int milliseconds=3000) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    do { process.Poll(); if(!process.Running()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); } while(std::chrono::steady_clock::now()<deadline);
    return false;
}

static uintmax_t Heartbeats(const std::string &path) {
    std::error_code error;
    auto size=std::filesystem::file_size(platform::Path(path),error);
    return error ? 0 : size;
}

static void TestAdbServerLifetime(const char *program) {
    const auto executable=platform::Utf8(std::filesystem::absolute(program));
    for (int mode=0; mode<4; ++mode) {
        TemporaryDirectory dir;
        const auto heartbeat=dir.filePath("heartbeat");
        auto process=mode==3 ? platform::MakeProcess() : platform::MakeAdbProcess();
        process->Start(executable,{"--spawn-server",heartbeat,mode==0 ? "exit" : "wait"});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(Heartbeats(heartbeat)<2 && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(Heartbeats(heartbeat)>=2);
        if(mode==0) CHECK(Wait(*process));
        if(mode==1 || mode==3) { process->Kill(); CHECK(Wait(*process)); }
        process.reset(); // Includes cancellation by destructor and Windows job-handle closure.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto before=Heartbeats(heartbeat);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        CHECK((Heartbeats(heartbeat)>before)==(mode!=3));
        { std::ofstream stop(platform::Path(heartbeat+".stop")); stop << "stop"; }
        if(mode!=3) {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!std::filesystem::exists(platform::Path(heartbeat+".done")) && std::chrono::steady_clock::now()<end)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            CHECK(std::filesystem::exists(platform::Path(heartbeat+".done")));
        }
    }
}
static void TestProcess(const char *program) {
    auto process=platform::MakeProcess();
    const auto executable=platform::Utf8(std::filesystem::absolute(program));
    process->Start(executable,{"--echo","space and \"quote\"","固件路径","$(literal)","trail\\"});
    CHECK(Wait(*process)); CHECK(process->NormalExit() && process->ExitCode()==0);
    CHECK(process->TakeOutput()=="space and \"quote\"\n固件路径\n$(literal)\ntrail\\\n");
    process->Start(executable,{"--fail"}); CHECK(Wait(*process)); CHECK(process->NormalExit() && process->ExitCode()==7);
    process->Start(executable,{"--flood"}); CHECK(Wait(*process)); CHECK(process->TakeOutput().size()==1024*1024);
    process->Start(executable,{"--sleep"}); process->Kill(); CHECK(Wait(*process,1000));
    try { process->Start("/a/missing/upgrade-test-program",{}); CHECK(false); } catch(const std::exception &) {}
}

static void TestText() {
    CHECK(platform::FoldCase("Straße.IMG")=="strasse.img"); CHECK(platform::Upper("固件.img")=="固件.IMG");
}

static void TestFile() {
    TemporaryDirectory dir;
    const auto output=platform::Path(dir.filePath("固件.bin"));
    { platform::AtomicFile file(output); file.write("old",3); file.commit(); }
    { platform::AtomicFile file(output); file.write("new",3); }
    { platform::File file(output); auto b=file.read(3); CHECK(std::string(b.begin(),b.end())=="old"); }
    size_t entries=0; for(const auto &e:std::filesystem::directory_iterator(dir.path)) { (void)e; ++entries; } CHECK(entries==1);
    { platform::AtomicFile file(output); file.resize(uint64_t(5)*1024*1024*1024); file.seek(uint64_t(5)*1024*1024*1024-1); file.write("z",1); file.commit(); }
    { platform::File file(output); CHECK(file.size()==uint64_t(5)*1024*1024*1024); file.seek(file.size()-1); CHECK(file.read(1)[0]=='z'); }
}

static void TestOpenFile() {
    TemporaryDirectory dir;
    const auto folder = dir.path / platform::Path("\u6d4b\u8bd5 folder");
    CHECK(std::filesystem::create_directory(folder));
    const auto path = platform::Utf8(folder / platform::Path("\u56fa\u4ef6.bin"));
    { std::ofstream file(platform::Path(path), std::ios::binary); file << "old"; }
    auto file = platform::OpenFile(path.c_str(), "rb");
    CHECK(file != nullptr);
    if (file) { char bytes[3]; CHECK(std::fread(bytes, 1, 3, file) == 3); CHECK(std::memcmp(bytes, "old", 3) == 0); std::fclose(file); }
    file = platform::OpenFile(path.c_str(), "wb+");
    CHECK(file != nullptr);
    if (file) { CHECK(std::fwrite("new", 1, 3, file) == 3); CHECK(std::fclose(file) == 0); }
    { std::ifstream input(platform::Path(path), std::ios::binary); std::string bytes; input >> bytes; CHECK(bytes == "new"); }
    errno = 0;
    CHECK(platform::OpenFile(dir.filePath("missing").c_str(), "rb") == nullptr && errno == ENOENT);
    CHECK(platform::OpenFile(nullptr, "rb") == nullptr && errno == EINVAL);
    CHECK(platform::OpenFile(path.c_str(), nullptr) == nullptr && errno == EINVAL);
#ifdef _WIN32
    CHECK(platform::OpenFile("\xff", "rb") == nullptr && errno == EINVAL);
#endif
}

static void TestSerialPort() {
#ifndef _WIN32
    int master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK); CHECK(master>=0);
    if(master>=0) {
        CHECK(grantpt(master)==0 && unlockpt(master)==0);
        auto serial=platform::OpenSerialPort(ptsname(master)); serial->Clear();
        const uint8_t payload[]={0x7e,0,0xff,0x7d};
        CHECK(serial->Write(payload,std::chrono::milliseconds(100),{})==sizeof(payload));
        uint8_t received[4]={}; CHECK(read(master,received,sizeof(received))==sizeof(received));
        CHECK(std::equal(std::begin(payload),std::end(payload),std::begin(received)));
        CHECK(write(master,payload,sizeof(payload))==sizeof(payload));
        auto reply=serial->Read(std::chrono::milliseconds(50),{}); CHECK(reply==std::vector<uint8_t>(std::begin(payload),std::end(payload)));
        std::stop_source cancelled; cancelled.request_stop();
        CHECK(serial->Read(std::chrono::milliseconds(50),cancelled.get_token()).empty());
        close(master);
    }
#endif
}

int main(int argc,char **argv) {
    if(argc>1) {
        const std::string mode=argv[1];
        if(mode=="--echo") {
#ifdef _WIN32
            // The helper echoes UTF-8 bytes, independent of the CRT's ANSI argv
            // conversion and text-mode CRLF translation on Windows.
            _setmode(_fileno(stdout), _O_BINARY);
            int count=0;
            auto **arguments=CommandLineToArgvW(GetCommandLineW(), &count);
            if (!arguments) return 1;
            for(int i=2;i<count;++i) std::printf("%s\n",platform::Utf8(std::filesystem::path(arguments[i])).c_str());
            LocalFree(arguments);
#else
            for(int i=2;i<argc;++i) std::printf("%s\n",argv[i]);
#endif
            return 0;
        }
        if(mode=="--fail") return 7;
        if(mode=="--server-heartbeat" && argc==3) {
            const auto heartbeat=platform::Path(argv[2]);
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!std::filesystem::exists(platform::Path(std::string(argv[2])+".stop")) && std::chrono::steady_clock::now()<end) {
                { std::ofstream file(heartbeat,std::ios::app); file << '.'; }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::ofstream done(platform::Path(std::string(argv[2])+".done"));
            return 0;
        }
        if(mode=="--spawn-server" && argc==4) {
#ifdef _WIN32
            const auto executable=std::filesystem::absolute(argv[0]).wstring();
            auto command=L"\""+executable+L"\" --server-heartbeat \""+platform::Path(argv[2]).wstring()+L"\"";
            STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION child{};
            if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)) return 2;
            CloseHandle(child.hProcess); CloseHandle(child.hThread);
#else
            const auto child=fork();
            if(child<0) return 2;
            if(child==0) { execl(argv[0],argv[0],"--server-heartbeat",argv[2],nullptr); _exit(2); }
#endif
            if(std::string(argv[3])=="wait") std::this_thread::sleep_for(std::chrono::seconds(30));
            return 0;
        }
        if(mode=="--sleep") { std::this_thread::sleep_for(std::chrono::seconds(30)); return 0; }
        if(mode=="--flood") { for(int i=0;i<1024;++i) { std::string text(1024,'x'); std::fwrite(text.data(),1,text.size(),stdout); } return 0; }
    }
    TestProcess(argv[0]);
    TestAdbServerLifetime(argv[0]);
    TestText();
    TestFile();
    TestOpenFile();
    TestSerialPort();
    return failures?1:0;
}
