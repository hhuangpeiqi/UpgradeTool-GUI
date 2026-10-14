#include "qdl_bridge.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
static int failures;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while (0)
struct State { std::atomic<bool> ready{false}, cancel{false}, done{false}, peerOk{true}; };
static QByteArray Packet(std::initializer_list<uint32_t> words) {
    QByteArray data;
    for (uint32_t word : words) for (int i=0; i<4; ++i) data.append(char(word >> (8*i)));
    return data;
}
static bool Send(int fd, const QByteArray &data) { return write(fd,data.constData(),data.size()) == data.size(); }
static QByteArray Receive(int fd, int size) {
    QByteArray data; QElapsedTimer timer; timer.start();
    while (data.size() < size && timer.elapsed() < 3000) {
        pollfd p{fd,POLLIN,0};
        if (poll(&p,1,100) <= 0) continue;
        char buf[137]; int n = read(fd,buf,qMin(int(sizeof(buf)),size-int(data.size())));
        if (n > 0) data.append(buf,n); else if (n == 0 || errno != EAGAIN) break;
    }
    return data;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir dir;
    QByteArray image(65536,0); for (int i=0;i<image.size();++i) image[i]=char(i);
    const std::string loader=dir.filePath("loader.mbn").toStdString();
    { QFile f(QString::fromStdString(loader)); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(image)==image.size()); }
    // 0 success with small chunks, 1 blocked write cancellation, 2 read timeout,
    // 3 peer disconnect, 4 partial write timeout, 5 cancelled idle read, 6 malformed HELLO.
    for (int scenario=0;scenario<7;++scenario) {
        int master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK); CHECK(master>=0); if(master<0) continue;
        CHECK(grantpt(master)==0); CHECK(unlockpt(master)==0); std::string node=ptsname(master);
        State state;
        qtq_request r; qtq_defaults(&r); r.command=QTQ_KICKSTART; r.node=node.c_str(); r.programmer=loader.c_str();
        r.chunk_size=scenario==0 ? 257 : 65536;
        qtq_callbacks hooks{}; hooks.user=&state;
        hooks.progress=[](void *p,const char *,uint64_t,uint64_t){static_cast<State*>(p)->ready=true;};
        hooks.cancelled=[](void *p){return static_cast<State*>(p)->cancel.load()?1:0;};
        std::thread peer([&] {
            while(!state.ready && !state.done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(state.done) return;
            if(scenario==2) return;
            if(scenario==6) { state.peerOk=Send(master,Packet({1,8})); return; }
            if(scenario==5) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); state.cancel=true; return; }
            if(scenario==3) { close(master); return; }
            auto greeting=Packet({1,48,2,1,4096,0,0,0,0,0,0,0});
            bool ok=Send(master,greeting.left(3));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ok=ok && Send(master,greeting.mid(3,10));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ok=ok && Send(master,greeting.mid(13));
            auto hello=Receive(master,48); ok=ok && hello.size()==48 && hello[0]==2;
            ok=ok && Send(master,Packet({3,20,13,0,uint32_t(image.size())}));
            if(scenario==1 || scenario==4) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if(scenario==1) state.cancel=true;
                state.peerOk=ok; return;
            }
            ok=ok && Receive(master,image.size())==image;
            ok=ok && Send(master,Packet({4,16,13,0}));
            ok=ok && Receive(master,8)==Packet({5,8});
            ok=ok && Send(master,Packet({6,12,1}));
            state.peerOk=ok;
        });
        QElapsedTimer elapsed; elapsed.start();
        int ret=qtq_execute(&r,&hooks); state.done=true; peer.join();
        CHECK(state.peerOk);
        if(scenario==0) CHECK(ret==0);
        else if(scenario==1 || scenario==5) CHECK(ret==-ECANCELED);
        else CHECK(ret<0);
        CHECK(elapsed.elapsed()<4000);
        if(scenario!=3) close(master);
    }
    return failures ? 1 : 0;
}
