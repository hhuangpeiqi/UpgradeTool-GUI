#ifndef GUI_ROCKCHIP_CONTROLLER_H
#define GUI_ROCKCHIP_CONTROLLER_H
#include <QObject>
#include "gui/backend/ImageEntryModel.h"
#include "gui/backend/KeyValueModel.h"
#include "gui/backend/Retranslatable.h"
#include "application/TaskRunner.h"
#include "application/Inspection.h"
namespace gui {
class RockchipController : public QObject, public Retranslatable {
    Q_OBJECT
	Q_PROPERTY(gui::ImageEntryModel *imageEntries READ imageEntries CONSTANT)
	Q_PROPERTY(gui::KeyValueModel *firmwareEntries READ firmwareEntries CONSTANT)
	Q_PROPERTY(gui::KeyValueModel *results READ results CONSTANT)
	Q_PROPERTY(QString imageDir READ imageDir NOTIFY imageStateChanged)
	Q_PROPERTY(QString imageHint READ imageHint NOTIFY imageStateChanged)
	Q_PROPERTY(bool imageHintError READ imageHintError NOTIFY imageStateChanged)
	Q_PROPERTY(bool imageQueueActive READ imageQueueActive NOTIFY imageStateChanged)
	Q_PROPERTY(QString firmwarePath READ firmwarePath NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareHint READ firmwareHint NOTIFY firmwareStateChanged)
	Q_PROPERTY(bool firmwareHintError READ firmwareHintError NOTIFY firmwareStateChanged)
	Q_PROPERTY(bool firmwareValid READ firmwareValid NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareLoaderVersion READ firmwareLoaderVersion NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareChip READ firmwareChip NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareBuildTime READ firmwareBuildTime NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString firmwareSign READ firmwareSign NOTIFY firmwareStateChanged)
	Q_PROPERTY(QString advancedHint READ advancedHint NOTIFY advancedStateChanged)
	Q_PROPERTY(bool advancedHintError READ advancedHintError NOTIFY advancedStateChanged)
	Q_PROPERTY(QStringList storages READ storages CONSTANT)
public:
    explicit RockchipController(QObject *parent=nullptr);
    ~RockchipController() override;
    void restore();
    void setLocked(bool locked) { m_locked=locked; }
    void setActive(bool active) { m_active=active; }
    void setCancelled(bool cancelled) { m_bCancelRequested=cancelled; }
    void cancelInspections();
    void retranslate() override;
    void applyResult(const core::OperationResult &);
    Q_INVOKABLE void inspectAdvancedFirmware(const QString &path);
    void applyInspection(int context,quint64 generation,const core::OperationResult &);
    void applyFinished(const core::TaskSummary &,const core::TaskJob &);
    void applyStep(TranslatableText::Factory text) { if(m_bQueueActive) SetImageHint(std::move(text),false); }
	gui::ImageEntryModel *imageEntries() const { return m_pImageEntries; }
	gui::KeyValueModel *firmwareEntries() const { return m_pFirmwareEntries; }
	gui::KeyValueModel *results() const { return m_pResults; }
	QString imageDir() const { return m_strImageDir; }
	QString imageHint() const { return m_imageHint.value(); }
	bool imageHintError() const { return m_bImageHintError; }
	bool imageQueueActive() const { return m_bQueueActive; }
	QString firmwarePath() const { return m_strFirmware; }
	QString firmwareHint() const { return m_firmwareHint.value(); }
	bool firmwareHintError() const { return m_bFirmwareHintError; }
	bool firmwareValid() const { return m_bFirmwareValid; }
	QString firmwareVersion() const { return m_strFwVersion; }
	QString firmwareLoaderVersion() const { return m_strFwLoader; }
	QString firmwareChip() const { return m_strFwChip; }
	QString firmwareBuildTime() const { return m_strFwBuild; }
	QString firmwareSign() const { return m_strFwSign; }
	QString advancedHint() const { return m_advancedHint.value(); }
	bool advancedHintError() const { return m_bAdvancedHintError; }
	QStringList storages() const;
    Q_INVOKABLE void runOperation(int operation,const QVariantMap &parameters=QVariantMap());
    Q_INVOKABLE void setImageDir(const QString &);
    Q_INVOKABLE void clearImage();
    Q_INVOKABLE void executeImageDownload(bool reboot);
    Q_INVOKABLE void setFirmware(const QString &);
    Q_INVOKABLE void clearFirmware();
    Q_INVOKABLE void executeFirmwareUpgrade(bool reboot);
    Q_INVOKABLE void extractFirmware(const QString &);
    Q_INVOKABLE void showFirmwareInfo();
    Q_INVOKABLE void exportComLog(const QString &);
    Q_INVOKABLE void exportImage(const QString &,const QString &,const QString &);
    Q_INVOKABLE void createGpt(const QString &,const QString &);

signals:
    void imageStateChanged();
    void firmwareStateChanged();
    void advancedStateChanged();
    void executeRequested(const core::TaskJob &);
    void inspectRequested(const core::InspectionRequest &);
    void errorRequested(const QString &,bool);
private:
    void SetImageHint(TranslatableText::Factory,bool);
    void SetFirmwareHint(TranslatableText::Factory,bool);
    void SetAdvancedHint(TranslatableText::Factory,bool);
    ImageEntryModel *m_pImageEntries;
    KeyValueModel *m_pFirmwareEntries,*m_pResults;
    Translatable<QVariantList> m_firmwareInfo;
    bool m_locked=false,m_active=true,m_bCancelRequested=false;
    quint64 m_folderGeneration=0,m_firmwareGeneration=0,m_advancedFirmwareGeneration=0;
    core::Cancellation m_folderInspection,m_firmwareInspection,m_advancedFirmwareInspection;
	QString m_strImageDir;
	TranslatableText m_imageHint;
	bool m_bImageHintError=false;
	bool m_bQueueActive=false;

	QString m_strFirmware;
	TranslatableText m_firmwareHint;
	bool m_bFirmwareHintError=false;
	bool m_bFirmwareValid=false;
	QString m_strFwVersion;
	QString m_strFwLoader;
	QString m_strFwChip;
	QString m_strFwBuild;
	QString m_strFwSign;
	bool m_bFirmwareReboot=true;

	TranslatableText m_advancedHint;
	bool m_bAdvancedHintError=false;

};
}
#endif
