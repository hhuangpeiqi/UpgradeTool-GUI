#include "tests/support/qt/FakeDiagSession.h"
#include "gui/EdlRunner.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
namespace platform { class FileLog; }
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
            const auto status = qgetenv("EDL_TEST_UNAUTHORIZED");
            std::printf("List of devices attached\nboard\t%s\n", status.isEmpty() ? "device" : status == "offline" ? "offline" : "unauthorized");
            return 0;
        }
        if (args.mid(1) == QStringList({"-s","board","reboot","edl"})) return qgetenv("EDL_TEST_DISCONNECT").isEmpty() ? 0 : 1;
        return 42;
    }
    // Exercise the real subprocess adapter using this executable, never host ADB.
    const auto hooks = DefaultEdlHooks();
    const auto cancellation = std::make_shared<std::stop_source>();
    QString commandError;
    CHECK(hooks.reboot(app.applicationFilePath(), "board", cancellation, commandError));
    qputenv("EDL_TEST_DISCONNECT", "1");
    CHECK(hooks.reboot(app.applicationFilePath(), "board", cancellation, commandError));
    CHECK(!commandError.isEmpty()); qunsetenv("EDL_TEST_DISCONNECT");
    qputenv("EDL_TEST_UNAUTHORIZED", "1");
    CHECK(!hooks.reboot(app.applicationFilePath(), "board", cancellation, commandError));
    for (const auto &status : {"unauthorized", "offline"}) {
        qputenv("EDL_TEST_UNAUTHORIZED", status);
        const auto endpoints = hooks.discover(app.applicationFilePath(), cancellation, commandError);
        for (const auto &endpoint : endpoints) CHECK(endpoint.kind != EdlEndpoint::Adb);
    }
    qunsetenv("EDL_TEST_UNAUTHORIZED");
    bool authorized = false;
    for (const auto &endpoint : hooks.discover(app.applicationFilePath(), cancellation, commandError))
        authorized |= endpoint.kind == EdlEndpoint::Adb && endpoint.serial == "board";
    CHECK(authorized);
    CHECK(!hooks.reboot("/nonexistent/adb", "board", cancellation, commandError));
    cancellation->request_stop();
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
    for(int scenario=0;scenario<23;++scenario) {
        EdlEndpoint diag; diag.id="diag"; diag.label="DIAG"; diag.location="usb:1-2"; diag.suggested=true;
        diag.vid=scenario%2 ? 0x2c7c : 0x05c6; diag.pid=scenario%2 ? 0x0125 : 0x9091; diag.interfaceNumber=3;
        EdlEndpoint adb=diag; adb.id="adb:board"; adb.kind=EdlEndpoint::Adb; adb.serial="board";
        EdlEndpoint edl=diag; edl.pid=0x9008; edl.downloadInterface=true; edl.vid=0x05c6; edl.pid=0x9008; edl.path="COM12";
        QVector<EdlEndpoint> endpoints{diag,adb};
        if(scenario==6 || scenario==16) { endpoints[0].suggested=false; } // Explicit unknown interface.
        if(scenario==7) { auto other=diag; other.id="diag2"; endpoints.append(other); }
        if(scenario==8) { endpoints[0].location.clear(); endpoints.resize(1); edl.location="usb:9-9"; }
        int probes=0,switches=0,reboots=0,opens=0; std::atomic_bool sent(false);
        EdlHooks hooks;
        int discoveries = 0;
        hooks.discover=[&](const QString &,const EdlCancel &,QString &) {
            ++discoveries;
            auto fresh = endpoints;
            if (discoveries > 1 && scenario == 13) fresh.clear();
            if (discoveries > 1 && scenario == 14) fresh[0].serial = "replacement";
            return fresh;
        };
        auto openDiag = FakeDiagFactory([&](const EdlEndpoint &selected,const QByteArray &request,const EdlCancel &,QString &error,bool &written) {
            written=true;
            CHECK(selected.id=="diag");
            if(request==QByteArray(1,0)) { ++probes; if(scenario==1 || scenario==2 || scenario==16) { error="Not DIAG"; return QByteArray(1,char(0x13)); } return QByteArray(55,0); }
            CHECK(request==QByteArray::fromHex("4b650100")); ++switches; sent=true;
            if(scenario==12) { written=false; error="permission denied"; return QByteArray(); }
            if(scenario==4) return QByteArray(1,char(0x13));
            if(scenario==10) { error="disconnected"; return QByteArray(); }
            return request;
        });
        hooks.openDiag=[&](const EdlEndpoint &e,const EdlCancel &cancel,QString &error) {
            ++opens; return openDiag(e,cancel,error);
        };
        hooks.reboot=[&](const QString &,const QString &serial,const EdlCancel &,QString &error) { CHECK(serial=="board"); ++reboots; sent=true; if(scenario==11) { error="unauthorized"; return false; } return true; };
        hooks.scan=[&] {
            auto found=edl;
            if(scenario==3 || scenario==5 || scenario==11) found.location="usb:9-9";
            if(scenario==17) found.pid=0x9091;
            if(scenario==18) found.vid=0x1234;
            if(scenario==19) found.downloadInterface=false;
            if(scenario==20) found.pid=0x900e;
            if(!sent.load()) return QVector<EdlEndpoint>{};
            if(scenario==21) { auto second=found; second.id="second"; second.interfaceNumber=4; return QVector<EdlEndpoint>{found,second}; }
            if(scenario==22) { found.location.clear(); found.ports.clear(); }
            return QVector<EdlEndpoint>{found};
        };
        EdlRunner runner(hooks,150); bool done=false,ok=false;
        QObject::connect(&runner,&EdlRunner::finished,[&](bool success,const QString &,const EdlEndpoint &) { done=true; ok=success; });
        runner.refresh("fake-adb"); CHECK(await([&]{return !runner.discovering();}));
        CHECK(!runner.targets().isEmpty());
        const int method=(scenario==9 || scenario==11) ? EdlRunner::Adb : EdlRunner::Diag;
        runner.setMethod(method);
        runner.start(scenario==8 ? "diag" : "usb:1-2",method,(scenario==6 || scenario==16) ? "diag" : "","fake-adb");
        if(scenario==7) { CHECK(!runner.running()); CHECK(probes==0 && switches==0); continue; }
        if(scenario==15) runner.start("usb:1-2", method, "", "fake-adb");
        if(scenario==5) { CHECK(await([&]{return sent.load();})); runner.cancel(); }
        CHECK(await([&]{return done;}));
        CHECK(ok==(scenario==0 || scenario==6 || scenario==9 || scenario==10 || scenario==15));
        if(scenario==1) CHECK(reboots==0 && switches==0);
        if(scenario==2) CHECK(reboots==0 && switches==0);
        if(scenario==6) CHECK(probes==1 && switches==1);
        if(scenario==16) CHECK(probes==1 && switches==0 && reboots==0);
        if(scenario==9) CHECK(probes==0 && reboots==1);
        if(scenario==13 || scenario==14) CHECK(probes==0 && switches==0 && reboots==0);
        if(scenario==15) CHECK(switches==1 && reboots==0);
        CHECK(!runner.ready());
        CHECK(runner.state()==(scenario==5 ? EdlRunner::Cancelled : ok ? EdlRunner::Completed : EdlRunner::Failed));
        CHECK(opens <= 1);
        if(switches) CHECK(opens==1); // Verification and switch share one session.
    }
    {
        // Snapshot readiness survives background refresh. An older discovery
        // finishing after a switch must never restore its pre-reboot targets.
        EdlEndpoint diag;
        diag.id="diag"; diag.location="usb:1-2"; diag.suggested=true;
        auto edl=diag; edl.downloadInterface=true; edl.vid=0x05c6; edl.pid=0x9008;
        std::atomic<int> discoveries(0), probes(0), switches(0);
        std::atomic_bool releaseBackground(false), backgroundEntered(false);
        EdlHooks injected;
        injected.discover=[&](const QString &,const EdlCancel &,QString &) {
            const int call=++discoveries;
            if(call==2) {
                backgroundEntered=true;
                while(!releaseBackground.load()) QThread::msleep(2);
            }
            return QVector<EdlEndpoint>{diag};
        };
        injected.openDiag = FakeDiagFactory([&](const EdlEndpoint &,const QByteArray &request,const EdlCancel &,QString &,bool &sent) {
            sent=true;
            if(request==QByteArray(1,0)) { ++probes; return QByteArray(55,0); }
            ++switches; return request;
        });
        injected.scan=[&] { return QVector<EdlEndpoint>{edl}; };
        injected.reboot=[](const QString &,const QString &,const EdlCancel &,QString &) { CHECK(false); return false; };
        EdlRunner runner(injected,100);
        int selections=0, errors=0; bool done=false;
        QObject::connect(&runner,&EdlRunner::selectionRequested,[&]{++selections;});
        QObject::connect(&runner,&EdlRunner::errorOccurred,[&](const QString &){++errors;});
        QObject::connect(&runner,&EdlRunner::finished,[&](bool ok,const QString &,const EdlEndpoint &){CHECK(ok);done=true;});
        CHECK(runner.method()==EdlRunner::Diag && !runner.ready());
        runner.requestSwitch("adb"); CHECK(errors==1 && !runner.running());
        runner.refresh("adb"); CHECK(await([&]{return !runner.discovering();}));
        CHECK(runner.ready());
        runner.refresh("adb"); CHECK(await([&]{return backgroundEntered.load();}));
        CHECK(runner.discovering() && runner.ready());
        runner.refresh("adb"); CHECK(discoveries==2); // no duplicate refresh
        runner.requestSwitch("adb"); CHECK(runner.running());
        runner.setMethod(EdlRunner::Adb); CHECK(runner.method()==EdlRunner::Diag);
        runner.requestSwitch("adb"); // no duplicate reboot
        CHECK(await([&]{return done;}));
        CHECK(probes==1 && switches==1 && selections==0 && !runner.ready());
        releaseBackground=true;
        CHECK(await([&]{return !runner.discovering();}));
        CHECK(runner.targets().isEmpty() && !runner.ready());
    }
    for(int scenario=0;scenario<3;++scenario) {
        EdlEndpoint diag; diag.id="diag"; diag.location="usb:1-2"; diag.vid=0x05c6; diag.pid=0x9091; diag.suggested=true;
        auto edl=diag; edl.pid=0x9008; edl.downloadInterface=true;
        bool alive=false,done=false; int opens=0,closes=0,commands=0;
        EdlHooks injected;
        injected.discover=[&](const QString &,const EdlCancel &,QString &) { return QVector<EdlEndpoint>{diag}; };
        injected.openDiag=[&](const EdlEndpoint &e,const EdlCancel &cancel,QString &error) {
            ++opens; alive=true;
            std::shared_ptr<int> lifetime(new int(0),[&](int *p){delete p; alive=false; ++closes;});
            auto factory=FakeDiagFactory([&,lifetime](const EdlEndpoint &,const QByteArray &request,const EdlCancel &,QString &,bool &sent) {
                CHECK(alive); ++commands; sent=true;
                if(request==QByteArray(1,0)) return scenario==1 ? QByteArray(1,char(0x13)) : QByteArray(55,0);
                return scenario==2 ? QByteArray(1,char(0x13)) : request;
            });
            return factory(e,cancel,error);
        };
        injected.scan=[&] { CHECK(!alive); return QVector<EdlEndpoint>{edl}; };
        EdlRunner runner(injected,100);
        QObject::connect(&runner,&EdlRunner::finished,[&](bool ok,const QString &,const EdlEndpoint &) {
            CHECK(ok==(scenario==0)); CHECK(!alive); done=true;
        });
        runner.refresh(""); CHECK(await([&]{return !runner.discovering();}));
        runner.requestSwitch(""); CHECK(await([&]{return done;}));
        CHECK(opens==1 && closes==1 && commands==(scenario==1 ? 1 : 2));
    }
    {
        // No ordinary host UART or raw USB ADB interface may become DIAG.
        QVector<EdlEndpoint> endpoints;
        EdlEndpoint host; host.id="serial:/dev/ttyS0"; endpoints.append(host);
        EdlEndpoint adb; adb.id="adb:board"; adb.kind=EdlEndpoint::Adb; adb.serial="board";
        endpoints.append(adb);
        EdlEndpoint alreadyEdl; alreadyEdl.id="edl"; alreadyEdl.downloadInterface=true; alreadyEdl.vid=0x05c6; alreadyEdl.pid=0x9008; alreadyEdl.suggested=true;
        endpoints.append(alreadyEdl);
        EdlHooks injected;
        injected.discover=[&](const QString &,const EdlCancel &,QString &) {return endpoints;};
        EdlRunner runner(injected,100);
        runner.refresh("adb"); CHECK(await([&]{return !runner.discovering();}));
        CHECK(!runner.ready() && runner.targets().isEmpty());
        runner.setMethod(EdlRunner::Adb); CHECK(runner.ready() && runner.targets().size()==1);
        CHECK(runner.canStart("adb:board", ""));
        runner.setMethod(0); CHECK(runner.method()==EdlRunner::Adb);
        // Offline/unauthorized devices are omitted by the discovery adapter.
        endpoints.removeAt(1);
        runner.refresh("adb"); CHECK(await([&]{return !runner.discovering();}));
        CHECK(!runner.ready());
    }
    std::printf("EDL runner: %d failures\n",failures);
    return failures ? 1 : 0;
}
