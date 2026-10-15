#ifndef GUI_TASK_CONTROLLER_H
#define GUI_TASK_CONTROLLER_H
#include <QObject>
#include <QElapsedTimer>
#include "application/TaskRunner.h"
namespace gui {
class TaskController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int progressValue READ progressValue NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString progressSpeed READ progressSpeed NOTIFY progressChanged)
public:
    explicit TaskController(QObject *parent=nullptr) : QObject(parent) {}
    bool busy() const { return m_busy; }
    bool cancelRequested() const { return m_cancelRequested; }
    int progressValue() const { return m_progressValue; }
    QString progressText() const { return m_progressText; }
    QString progressSpeed() const { return m_progressSpeed; }
    const core::TaskJob &job() const { return m_job; }
    core::Operation runningOperation() const { return m_job.steps.empty()?core::Operation::Count:m_job.steps.front().operation(); }
    void start(core::TaskJob job);
    void cancel();
    void finish();
    void setBusy(bool);
    void setProgress(int,const QString &);
    void setSpeed(const QString &speed) { m_progressSpeed=speed; emit progressChanged(); }
    void resetProgress();
public slots:
    void onProgress(const QString &,long long,long long,int);
    void onStageProgress(const QString &,long long,long long,int);
signals:
    void busyChanged();
    void progressChanged();
private:
    core::TaskJob m_job;
    bool m_busy=false,m_cancelRequested=false;
    int m_progressValue=0;
    QString m_progressText,m_progressSpeed,m_progressStep;
    QElapsedTimer m_progressTimer;
    long long m_progressBase=0;
};
}
#endif
