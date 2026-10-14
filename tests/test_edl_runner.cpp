#include "gui/EdlRunner.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
class CRKLog;
CRKLog *g_pLogObject = nullptr;
using namespace gui;
static int failures=0;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(0)
template<class F> bool await(F predicate) {
    QElapsedTimer timer; timer.start();
    while(!predicate() && timer.elapsed()<4000) { QCoreApplication::processEvents(); QThread::msleep(2); }
    return predicate();
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (argc > 1) {
        const auto args = app.arguments();
        if (args[1] == "devices") {
            std::printf("List of devices attached\nboard\t%s\n", qgetenv("EDL_TEST_UNAUTHORIZED").isEmpty() ? "device" : "unauthorized");
            return 0;
        }
        if (args.mid(1) == QStringList({"-s","board","reboot","edl"})) return qgetenv("EDL_TEST_DISCONNECT").isEmpty() ? 0 : 1;
        return 42;
    }
    // Exercise the real subprocess adapter using this executable, never host ADB.
    const auto hooks = DefaultEdlHooks();
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    QString commandError;
    CHECK(hooks.reboot(app.applicationFilePath(), "board", cancellation, commandError));
    qputenv("EDL_TEST_DISCONNECT", "1");
    CHECK(hooks.reboot(app.applicationFilePath(), "board", cancellation, commandError));
    CHECK(!commandError.isEmpty()); qunsetenv("EDL_TEST_DISCONNECT");
    qputenv("EDL_TEST_UNAUTHORIZED", "1");
    CHECK(!hooks.reboot(app.applicationFilePath(), "board", cancellation, commandError));
    qunsetenv("EDL_TEST_UNAUTHORIZED");
    CHECK(!hooks.reboot("/nonexistent/adb", "board", cancellation, commandError));
    cancellation->store(true);
    CHECK(!hooks.reboot(app.applicationFilePath(), "board", cancellation, commandError));

    auto wire=EdlRunner::encode("123456789"); CHECK(wire.toHex()=="3132333435363738396e907e");
    QByteArray payload=QByteArray::fromHex("007d7e00"),decoded;
    wire=EdlRunner::encode(payload); CHECK(wire.contains(QByteArray::fromHex("7d5d7d5e")));
    auto half=wire.left(2); CHECK(!EdlRunner::decode(half,decoded)); half+=wire.mid(2);
    CHECK(EdlRunner::decode(half,decoded) && decoded==payload);
    auto bad=EdlRunner::encode(payload); bad[0]=char(1); bad+=EdlRunner::encode(payload);
    CHECK(EdlRunner::decode(bad,decoded) && decoded==payload);
    CHECK(!EdlRunner::validVersion(QByteArray(1,0)));
    CHECK(!EdlRunner::validVersion(QByteArray(55,char(0x13))));
    CHECK(EdlRunner::validVersion(QByteArray(55,0)));
    EdlEndpoint native, usb;
    native.location = "native:port"; native.bus = 1; native.ports = {2,3};
    usb.location = "usb:1-2.3"; usb.bus = 1; usb.ports = {2,3};
    CHECK(EdlRunner::sameLocation(native, usb));
    usb.ports = {2,4}; CHECK(!EdlRunner::sameLocation(native, usb));
    native = {}; usb = {}; CHECK(!EdlRunner::sameLocation(native, usb));
    for(int scenario=0;scenario<13;++scenario) {
        EdlEndpoint diag; diag.id="diag"; diag.label="DIAG"; diag.location="usb:1-2"; diag.suggested=true;
        diag.vid=scenario%2 ? 0x2c7c : 0x05c6; diag.pid=scenario%2 ? 0x0125 : 0x9091; diag.interfaceNumber=3;
        EdlEndpoint adb=diag; adb.id="adb:board"; adb.kind=EdlEndpoint::Adb; adb.serial="board";
        EdlEndpoint edl=diag; edl.pid=0x9008; edl.edl=true; edl.path="COM12";
        QVector<EdlEndpoint> endpoints{diag,adb};
        if(scenario==6) { endpoints[0].suggested=false; } // Explicit unknown interface.
        if(scenario==7) { auto other=diag; other.id="diag2"; endpoints.append(other); }
        if(scenario==8) { endpoints[0].location.clear(); endpoints.resize(1); edl.location="usb:9-9"; }
        int probes=0,switches=0,reboots=0; std::atomic_bool sent(false);
        EdlHooks hooks;
        hooks.discover=[&](const QString &,const EdlCancel &,QString &) { return endpoints; };
        hooks.exchange=[&](const EdlEndpoint &selected,const QByteArray &request,const EdlCancel &,QString &error,bool &written) {
            written=true;
            CHECK(selected.id=="diag");
            if(request==QByteArray(1,0)) { ++probes; if(scenario==1 || scenario==2) { error="Not DIAG"; return QByteArray(1,char(0x13)); } return QByteArray(55,0); }
            CHECK(request==QByteArray::fromHex("4b650100")); ++switches; sent=true;
            if(scenario==12) { written=false; error="permission denied"; return QByteArray(); }
            if(scenario==4) return QByteArray(1,char(0x13));
            if(scenario==10) { error="disconnected"; return QByteArray(); }
            return request;
        };
        hooks.reboot=[&](const QString &,const QString &serial,const EdlCancel &,QString &error) { CHECK(serial=="board"); ++reboots; sent=true; if(scenario==11) { error="unauthorized"; return false; } return true; };
        hooks.scan=[&] { auto found=edl; if(scenario==3 || scenario==5 || scenario==11) found.location="usb:9-9"; return sent.load() ? QVector<EdlEndpoint>{found} : QVector<EdlEndpoint>{}; };
        EdlRunner runner(hooks,150); bool done=false,ok=false;
        QObject::connect(&runner,&EdlRunner::finished,[&](bool success,const QString &,const QString &,int,const QVector<int> &) { done=true; ok=success; });
        runner.refresh("fake-adb"); CHECK(await([&]{return !runner.discovering();}));
        CHECK(!runner.targets().isEmpty());
        const int method=(scenario==2 || scenario==6 || scenario==8) ? 1 : (scenario==9 || scenario==11) ? 2 : 0;
        runner.start(scenario==8 ? "diag" : "usb:1-2",method,scenario==6 ? "diag" : "","fake-adb");
        if(scenario==7) { CHECK(!runner.running()); CHECK(probes==0 && switches==0); continue; }
        if(scenario==5) { CHECK(await([&]{return sent.load();})); runner.cancel(); }
        CHECK(await([&]{return done;}));
        CHECK(ok==(scenario==0 || scenario==1 || scenario==6 || scenario==9 || scenario==10));
        if(scenario==1) CHECK(reboots==1 && switches==0);
        if(scenario==2) CHECK(reboots==0 && switches==0);
        if(scenario==6) CHECK(probes==1 && switches==1);
        if(scenario==9) CHECK(probes==0 && reboots==1);
    }
    std::printf("EDL runner: %d failures\n",failures);
    return failures ? 1 : 0;
}
