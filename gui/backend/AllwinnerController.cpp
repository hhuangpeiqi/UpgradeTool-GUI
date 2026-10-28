#include "gui/backend/AllwinnerController.h"
#include "gui/OperationSpec.h"
#include <QSettings>
#include <QCoreApplication>
namespace gui {
namespace { QString AppTr(const char *text) { return QCoreApplication::translate("gui::AppController",text); } }
AllwinnerController::AllwinnerController(QObject *parent) : QObject(parent) {
    const auto saved=settings();
    for(int i=0;i<4;++i) {
        auto &state=m_states[i]; state.summary=new KeyValueModel(this); state.results=new KeyValueModel(this);
        if(i<3) state.firmware=saved.value(QString("page%1/firmware").arg(i),saved.value("firmware")).toString();
    }
}
AllwinnerController::~AllwinnerController() { cancelInspections(); }
void AllwinnerController::cancelInspections() { for(auto &state:m_states) if(state.inspection) state.inspection->request_stop(); }
void AllwinnerController::retranslate() { for(auto &state:m_states) state.hint.retranslate(); emit changed(); }
void AllwinnerController::applyInspection(int context,quint64 generation,const core::OperationResult &result) {
    if(context<0 || context>2 || generation!=m_states[context].generation) return;
    ApplyInspectResult(context,result);
}
void AllwinnerController::applyResult(const core::OperationResult &result) {
    if(result.operation==core::Operation::AllwinnerInspect) { ApplyInspectResult(m_context,result); return; }
    auto &state=m_states[m_context]; QVariantList items;
    for(const auto &row:result.items) items.append(QVariantMap{{"label",QCoreApplication::translate("Allwinner",row.label.c_str())},{"value",QCoreApplication::translate("Allwinner",row.value.c_str())}});
    state.results->SetItems(items);
    state.error=!result.ok() && result.status!=core::OperationStatus::Cancelled;
    SetHint(m_context,Hint(result),state.error); emit changed();
}
void AllwinnerController::applyFinished(const core::TaskSummary &summary,size_t targets) {
    auto &state=m_states[m_context];
    const bool cancelled=summary.status==core::OperationStatus::Cancelled;
    state.error=summary.status==core::OperationStatus::Failed;
    if(cancelled) state.hint.Set([] { return AppTr("Cancelled"); });
    if(targets>1) state.hint.Set([summary,cancelled] {
        return AppTr("Batch finished: %1 succeeded, %2 failed").arg(summary.succeeded).arg(summary.failed)
            +(cancelled?AppTr(", remaining operations cancelled"):QString());
    });
    emit changed();
}
void AllwinnerController::SetHint(int context, TranslatableText::Factory factory, bool bError)
{
	if (context < 0 || context >= 4)
		return;
	State &state = m_states[context];
	state.hint.Set(std::move(factory));
	state.error = bError;
	emit changed();
}

void AllwinnerController::ApplyInspectResult(int context, const core::OperationResult &result)
{
    auto &state = m_states[context];
    QVariantList items;
    for(const auto &row:result.items) items.append(QVariantMap{{"label",QCoreApplication::translate("Allwinner",row.label.c_str())},{"value",QCoreApplication::translate("Allwinner",row.value.c_str())}});
    state.files.clear(); state.partitions.clear(); state.valid = result.ok();
    for(const auto &e:result.firmware.entries) state.files.append(QVariantMap{{"name",QString::fromStdString(e.fileName)},{"type",QString::fromStdString(e.type)},{"size",QString::number(e.size)}});
    for(const auto &p:result.partitions) {
        bool writable=false;
        for(const auto &e:result.firmware.entries) if(e.hasPart && e.partName==p.name) writable=true;
        state.partitions.append(QVariantMap{{"name",QString::fromStdString(p.name)},{"begin",QString::number(p.begin)},{"count",QString::number(p.count)},{"writable",writable}});
    }
    state.summary->SetItems(items);
    state.error = !result.ok() && result.status != core::OperationStatus::Cancelled;
    SetHint(context, Hint(result), state.error);
    emit changed();
}

TranslatableText::Factory AllwinnerController::Hint(const core::OperationResult &result)
{
	const bool awOk = result.ok();
	const bool awCancelled = result.status == core::OperationStatus::Cancelled;
	const core::Operation awOp = result.operation;
	const QString awDiagnostic = QString::fromStdString(result.diagnostic);
	return [awOk, awCancelled, awOp, awDiagnostic] {
		if (awCancelled)
			return AppTr("Cancelled");
		return awOk ? OperationName(awOp) + AppTr("done") : awDiagnostic;
	};
}

QVariantMap AllwinnerController::settings() const
{
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"upgrade_tool","upgrade_tool");
    settings.beginGroup("allwinner"); QVariantMap result;
    for(const auto &key:settings.allKeys()) result[key]=settings.value(key);
    return result;
}

void AllwinnerController::saveSettings(const QVariantMap &values)
{
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"upgrade_tool","upgrade_tool");
    settings.beginGroup("allwinner");
    for(auto it=values.begin();it!=values.end();++it) settings.setValue(it.key(),it.value());
}

QVariantList AllwinnerController::states() const
{
    QVariantList states;
    for (const auto &state : m_states) {
        states.append(QVariantMap{{"firmware", state.firmware}, {"hint", state.hint.value()},
            {"valid", state.valid}, {"error", state.error}, {"partitions", state.partitions},
            {"files", state.files}, {"summary", QVariant::fromValue(state.summary)},
            {"results", QVariant::fromValue(state.results)}});
    }
    return states;
}

void AllwinnerController::setFirmware(const QString &path)
{
    setContextFirmware(1, path);
}

void AllwinnerController::setContextFirmware(int context, const QString &path)
{
    if(m_locked || !m_active || context<0 || context>2) return;
    auto &state=m_states[context];
    const QString trimmed=path.trimmed();
    if(state.firmware==trimmed && state.inspected && state.valid) return;
    if(state.inspection) state.inspection->request_stop();
    ++state.generation;
    state.firmware=trimmed; state.valid=false; state.inspected=false; state.error=false;
    state.hint.Clear(); state.files.clear(); state.partitions.clear();
    state.summary->Clear(); state.results->Clear();
    saveSettings({{QString("page%1/firmware").arg(context),state.firmware}});
    emit changed();
    ensureContext(context);
}

void AllwinnerController::inspectFirmware(int context)
{
    if(!m_active || context<0 || context>2) return;
    auto &state=m_states[context];
    if(state.firmware.isEmpty()) return;
    state.inspected=true;
    if(state.inspection) state.inspection->request_stop();
    core::InspectionRequest inspection;
    inspection.context=context; inspection.generation=++state.generation;
    inspection.request=core::OperationRequest(core::Operation::AllwinnerInspect);
    inspection.request.file().path=state.firmware.toStdString();
    state.inspection=inspection.cancelled;
    emit inspectRequested(inspection);
}

void AllwinnerController::ensureContext(int context)
{
    if(m_locked || !m_active || context<0 || context>2) return;
    auto &state=m_states[context];
    if(!state.inspected && !state.firmware.isEmpty())
        inspectFirmware(context);
}

void AllwinnerController::run(int operation,const QVariantMap &p)
{
    if(m_locked || !m_active || operation<static_cast<int>(core::Operation::AllwinnerFlash) || operation>static_cast<int>(core::Operation::AllwinnerReboot)) return;
    core::OperationRequest r; r=core::OperationRequest(static_cast<core::Operation>(operation)); 
    bool contextOk=true;
    const int context=r.operation()==core::Operation::AllwinnerDeviceInfo ? 3 : p.value("context",1).toInt(&contextOk);
    if(!contextOk || context<0 || context>3 || (context==3 && r.operation()!=core::Operation::AllwinnerDeviceInfo)) return;
    auto &state=m_states[context];
    r.file().path=p.value("path",state.firmware).toString().trimmed().toStdString();
    r.file().output=p.value("output").toString().trimmed().toStdString();
    auto &o=r.allwinner(); bool valid=true;
    auto choice=[&](const char *key,int fallback,int maximum) { bool ok=true; int value=p.value(key,fallback).toInt(&ok); if(!ok||value<0||value>maximum) valid=false; return value; };
    auto number=[&](const char *key,uint64_t &value,bool *present=nullptr) {
        QString text=p.value(key).toString().trimmed(); if(text.isEmpty()) return;
        bool ok; auto n=text.toULongLong(&ok,text.startsWith("0x",Qt::CaseInsensitive)?16:10);
        if(!ok||text.startsWith('-')||n>uint64_t(INT64_MAX)) valid=false;
        else { value=n; if(present) *present=true; }
    };
    switch(r.operation()) {
    case core::Operation::AllwinnerFlash:
        o.mode=static_cast<core::AllwinnerFlashMode>(choice("mode",3,3));
        o.postAction=static_cast<core::PostAction>(choice("postAction",0,2));
        o.verify=p.value("verify",true).toBool();
        if(o.mode==core::AllwinnerFlashMode::Partition) {
            for(const auto &name:p.value("partitions").toStringList()) o.partitions.push_back(name.toStdString());
            if(o.partitions.empty()) valid=false;
        }
        break;
    case core::Operation::AllwinnerRaw:
        o.rawMode=static_cast<core::RawMode>(choice("rawMode",1,1));
        o.rawStorage=static_cast<core::RawStorage>(choice("rawStorage",0,2));
        o.rawImage=p.value("rawImage").toString().trimmed().toStdString();
        if(o.rawImage.empty()) valid=false;
        if(o.rawMode==core::RawMode::LogicOffset) number("logicOffset",o.logicOffset,&o.hasLogicOffset);
        break;
    case core::Operation::AllwinnerConvert:
        o.convertTarget=static_cast<core::ConvertTarget>(choice("target",2,4));
        number("logicOffset",o.logicOffset,&o.hasLogicOffset);
        if(o.convertTarget==core::ConvertTarget::Spinor) {
            number("ubootOffset",o.ubootOffset,&o.hasUbootOffset); number("norMiB",o.norMiB);
        } else {
            o.secure=static_cast<core::SecureMode>(choice("secure",0,2));
            o.storageSize=p.value("storageSize","auto").toString().toStdString();
        }
        if(r.file().output.empty()) valid=false;
        break;
    case core::Operation::AllwinnerExtract:
        if(r.file().output.empty()) valid=false;
        break;
    default: break;
    }
    if(r.operation()!=core::Operation::AllwinnerDeviceInfo && r.operation()!=core::Operation::AllwinnerReboot && r.file().path.empty()) valid=false;
    if(!valid) {
        SetHint(context, [] { return AppTr("Check the file paths and parameters for this operation; numbers must be in range"); }, true);
        emit errorRequested(state.hint.value(), true); return;
    }
    if(!core::IsLocalOperation(r.operation()) && !m_hasDevice) {
        SetHint(context, [] { return AppTr("Connect and select an Allwinner device first"); }, true);
        emit errorRequested(state.hint.value(), true); return;
    }
    m_context=context;
    if(r.operation()==core::Operation::AllwinnerInspect) {
        state.inspected=true; ++state.generation; if(state.inspection) state.inspection->request_stop();
    }
    const core::Operation runningOp = r.operation();
    SetHint(context, [runningOp] { return AppTr("Running: %1").arg(OperationName(runningOp)); }, false);
    if(r.operation()!=core::Operation::AllwinnerInspect) state.results->Clear();
    emit changed();
    core::TaskJob job; job.steps.push_back(r); emit executeRequested(job);
    if(!m_locked) {
        SetHint(context, [] { return AppTr("Select an online Allwinner device first"); }, true);
    }
}
}
