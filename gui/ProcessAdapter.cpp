#include "gui/ProcessAdapter.h"
#include <chrono>
#include <thread>
namespace gui {
ProcessAdapter::ProcessAdapter(QObject *parent,platform::ProcessFactory factory) : QObject(parent),process(factory()) {
    timer.setInterval(10);
    connect(&timer,&QTimer::timeout,this,[this] {
        process->Poll();
        if(pending && !process->Running()) { pending=false; timer.stop(); emit finished(process->ExitCode(),process->NormalExit()); }
    });
}
void ProcessAdapter::start(const QString &executable,const QStringList &arguments) {
    std::vector<std::string> args; for(const auto &a:arguments) args.push_back(a.toStdString());
    const auto currentGeneration = ++generation;
    timer.stop();
    error.clear(); pending=false;
    try { process->Start(executable.toStdString(),args); pending=true; timer.start(); }
    catch(const std::exception &e) {
        error=QString::fromUtf8(e.what());
        QTimer::singleShot(0,this,[this,currentGeneration,message=error] {
            if (currentGeneration == generation) emit failed(message);
        });
    }
}
bool ProcessAdapter::waitForFinished(int timeout) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    do { process->Poll(); if(!process->Running()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); } while(std::chrono::steady_clock::now()<deadline);
    return !process->Running();
}
}
