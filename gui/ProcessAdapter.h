#ifndef GUI_PROCESS_ADAPTER_H
#define GUI_PROCESS_ADAPTER_H
#include <QObject>
#include <QTimer>
#include <QStringList>
#include "platform/Process.h"
namespace gui {
class ProcessAdapter : public QObject {
    Q_OBJECT
public:
    enum State { NotRunning,Running };
    explicit ProcessAdapter(QObject *parent=nullptr,platform::ProcessFactory factory=platform::MakeProcess);
    void start(const QString &,const QStringList &);
    State state() const { return process->Running()?Running:NotRunning; }
    void kill() { process->Kill(); }
    bool waitForFinished(int timeout);
    QString errorString() const { return error; }
    QByteArray readAll() { return QByteArray::fromStdString(process->TakeOutput()); }
signals:
    void finished(int,bool normal);
    void failed(const QString &);
private:
    std::unique_ptr<platform::IProcess> process;
    QTimer timer;
    QString error;
    bool pending=false;
};
}
#endif
