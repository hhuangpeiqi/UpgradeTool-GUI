#include "platform/Process.h"
#include "platform/SerialPort.h"
#include "platform/Text.h"
#include "platform/File.h"
#include "tests/support/TestFiles.h"
#include <chrono>
#include <cstdio>
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
#endif
static int failures=0;
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); ++failures; } } while(0)
static bool Wait(platform::IProcess &process,int milliseconds=3000) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    do { process.Poll(); if(!process.Running()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); } while(std::chrono::steady_clock::now()<deadline);
    return false;
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
        if(mode=="--sleep") { std::this_thread::sleep_for(std::chrono::seconds(30)); return 0; }
        if(mode=="--flood") { for(int i=0;i<1024;++i) { std::string text(1024,'x'); std::fwrite(text.data(),1,text.size(),stdout); } return 0; }
    }
    TestProcess(argv[0]);
    TestText();
    TestFile();
    TestSerialPort();
    return failures?1:0;
}
