#ifndef GUI_QUALCOMM_CONTROLLER_H
#define GUI_QUALCOMM_CONTROLLER_H
#include <QObject>
#include "gui/InspectionQueue.h"
#include "domain/Message.h"
#include <QVariantMap>
#include <QStringList>
#include <QMap>
#include "domain/OperationResult.h"
#include "gui/backend/KeyValueModel.h"
#include "gui/backend/Retranslatable.h"
namespace gui {
class QualcommController : public QObject, public Retranslatable {
    Q_OBJECT
    Q_PROPERTY(bool inspecting READ inspecting NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QStringList xmlFiles READ xmlFiles NOTIFY changed)
    Q_PROPERTY(QVariantList preview READ preview NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(gui::KeyValueModel *results READ results CONSTANT)
public:
    explicit QualcommController(QObject *parent = nullptr, InspectionQueue *inspections = nullptr);
    void cancelInspections();
    ~QualcommController() override;
    bool inspecting() const { return m_inspecting; }
    QVariantMap settings() const { return settingsFor("flash"); }
    QStringList xmlFiles() const { return xmlFilesFor("flash"); }
    QVariantList preview() const { return previewFor("flash"); }
    QString error() const { return xmlErrorFor("flash"); }
    KeyValueModel *results() { return &m_results; }
    Q_INVOKABLE gui::KeyValueModel *resultsFor(const QString &context) const { return m_contextResults.value(context, nullptr); }
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE QVariantMap settingsFor(const QString &context) const;
    Q_INVOKABLE void setContextValue(const QString &context, const QString &key, const QVariant &value);
    Q_INVOKABLE QStringList xmlFilesFor(const QString &context) const;
    Q_INVOKABLE QVariantList xmlEntriesFor(const QString &context) const { return m_xmlEntries.value(context); }
    Q_INVOKABLE QVariantList previewFor(const QString &context) const { return m_previews.value(context); }
    Q_INVOKABLE QString xmlErrorFor(const QString &context) const { return m_errors.value(context); }
    Q_INVOKABLE QString operationErrorFor(const QString &context) const { return m_operationErrors.value(context); }
    Q_INVOKABLE QString validateXmlSelection(const QStringList &paths, bool patches) const;
    Q_INVOKABLE bool replaceFlashXml(const QStringList &programs, const QStringList &patches);
    Q_INVOKABLE QString xmlFolder(const QString &context = "flash") const;
    Q_INVOKABLE void addXml(const QString &path, const QString &context = "flash");
    Q_INVOKABLE void removeXml(int index, const QString &context = "flash");
    Q_INVOKABLE void moveXml(int index, int offset, const QString &context = "flash");
    Q_INVOKABLE bool canRun(int operation, const QVariantMap &overrides = QVariantMap()) const;
    Q_INVOKABLE bool needsDevice(int operation) const;
    Q_INVOKABLE QString validationError(int operation, const QVariantMap &overrides = QVariantMap()) const;
    Q_INVOKABLE void loadFirmware(const QString &path);
    Q_INVOKABLE void run(int operation, const QVariantMap &overrides = QVariantMap());
    void retranslate() override;
    void applyResult(const core::OperationResult &result);
    void setLocked(bool locked) { m_locked = locked; }
signals:
    void changed();
    void executeRequested(const core::OperationRequest &request);
    void errorRequested(const QString &error);
private:
    void inspect();
    QString buildRequest(int operation, const QVariantMap &overrides, core::OperationRequest &request) const;
    QVariantMap m_common;
    QMap<QString, QVariantMap> m_profiles;
    QMap<QString, QVariantList> m_previews;
    QMap<QString, QVariantList> m_xmlEntries;
    QMap<QString, QString> m_errors, m_operationErrors;
    QMap<QString,core::Message> m_errorMessages;
    QMap<QString,core::Message> m_operationMessages;
    KeyValueModel m_results;
    QMap<QString, KeyValueModel *> m_contextResults;
    bool m_locked = false;
    bool m_inspecting=false;
    uint64_t m_inspectionGeneration=0;
    core::Cancellation m_inspection;
    InspectionQueue *m_inspections;
    bool m_ownsInspections;
};
}
#endif
