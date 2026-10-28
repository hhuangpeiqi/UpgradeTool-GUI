#ifndef GUI_ALLWINNER_CONTROLLER_H
#define GUI_ALLWINNER_CONTROLLER_H
#include <QObject>
#include <QVariantMap>
#include "gui/backend/KeyValueModel.h"
#include "gui/backend/Retranslatable.h"
#include "application/TaskRunner.h"
#include "application/Inspection.h"
namespace gui {
class AllwinnerController : public QObject, public Retranslatable {
    Q_OBJECT
    Q_PROPERTY(QVariantList states READ states NOTIFY changed)
    Q_PROPERTY(QString firmware READ firmware WRITE setFirmware NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(QVariantList partitions READ partitions NOTIFY changed)
    Q_PROPERTY(QVariantList files READ files NOTIFY changed)
    Q_PROPERTY(gui::KeyValueModel *results READ results CONSTANT)
public:
    explicit AllwinnerController(QObject *parent=nullptr);
    ~AllwinnerController() override;
    QVariantList states() const;
    QString firmware() const { return m_states[1].firmware; }
    bool valid() const { return m_states[1].valid; }
    QVariantList partitions() const { return m_states[1].partitions; }
    QVariantList files() const { return m_states[1].files; }
    KeyValueModel *results() const { return m_states[1].summary; }
    void setLocked(bool locked) { m_locked=locked; }
    void setActive(bool active) { m_active=active; }
    void setHasDevice(bool present) { m_hasDevice=present; }
    void cancelInspections();
    void retranslate() override;
    void applyResult(const core::OperationResult &);
    void applyFinished(const core::TaskSummary &,size_t targets);
    void applyInspection(int context,quint64 generation,const core::OperationResult &);
    Q_INVOKABLE void setFirmware(const QString &);
    Q_INVOKABLE void setContextFirmware(int context,const QString &path);
    Q_INVOKABLE void ensureContext(int context);
    Q_INVOKABLE void inspectFirmware(int context);
    Q_INVOKABLE void run(int operation,const QVariantMap &parameters=QVariantMap());
    Q_INVOKABLE QVariantMap settings() const;
    Q_INVOKABLE void saveSettings(const QVariantMap &);
signals:
    void changed();
    void executeRequested(const core::TaskJob &);
    void inspectRequested(const core::InspectionRequest &);
    void errorRequested(const QString &,bool);
private:
    struct State {
        QString firmware;
        TranslatableText hint;
        QVariantList partitions,files;
        bool valid=false,inspected=false,error=false;
        KeyValueModel *summary=nullptr,*results=nullptr;
        quint64 generation=0;
        core::Cancellation inspection;
    };
    State m_states[4];
    int m_context=1;
    bool m_locked=false,m_active=false,m_hasDevice=false;
    void SetHint(int,TranslatableText::Factory,bool);
    void ApplyInspectResult(int,const core::OperationResult &);
    static TranslatableText::Factory Hint(const core::OperationResult &);
};
}
#endif
