#include "glancelot/recovery_checkpoint.hpp"
#include "settings_transaction.hpp"
#include "settings_policy.hpp"
#include "window_claim_guard.hpp"
#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <cmath>

namespace glancelot::recovery {
namespace {
Status status(Code code,Options options) {return {code,{},options.generation};}
bool decimal(const QJsonValue& value,quint64& number) {
    if(!value.isString())return false;
    const auto text=value.toString();bool ok=false;number=text.toULongLong(&ok);
    return ok&&QString::number(number)==text;
}
bool uuid(const QString& text) {
    const QUuid value(text);return !value.isNull()&&value.toString(QUuid::WithoutBraces)==text;
}
Code version(const QJsonObject& object) {
    const auto value=object.value("version");
    if(value==QJsonValue(1))return Code::Ok;
    if(value.isDouble()&&std::isfinite(value.toDouble())&&value.toDouble()>1&&std::floor(value.toDouble())==value.toDouble())return Code::Unsupported;
    return Code::Corrupt;
}
struct Records {
    QJsonObject recovery,windows,record;
    bool present{};
    quint64 sequence{};
};
Code records(const QJsonObject& root,int slot,Records& result) {
    const auto recovery=root.value("recovery");
    if(recovery.isUndefined())return Code::Ok;
    if(!recovery.isObject())return Code::Corrupt;
    result.recovery=recovery.toObject();auto code=version(result.recovery);if(code!=Code::Ok)return code;
    const auto windows=result.recovery.value("windows");
    if(!windows.isUndefined()&&!windows.isObject())return Code::Corrupt;
    result.windows=windows.toObject();const auto record=result.windows.value(QString::number(slot));
    if(record.isUndefined())return Code::Ok;
    if(!record.isObject())return Code::Corrupt;
    result.present=true;result.record=record.toObject();code=version(result.record);if(code!=Code::Ok)return code;
    const auto& row=result.record;quint64 start=0;
    if(!decimal(row.value("sequence"),result.sequence)||!decimal(row.value("start"),start))return Code::Corrupt;
    const auto nonce=row.value("claimNonce"),pid=row.value("pid"),state=row.value("state"),revision=row.value("checkpointRevision"),origin=row.value("origin");
    if(!nonce.isString()||(!nonce.toString().isEmpty()&&!uuid(nonce.toString()))||!pid.isDouble()||pid.toDouble()<0||pid.toDouble()>2147483647||std::floor(pid.toDouble())!=pid.toDouble()||
       !state.isString()||(state.toString()!="dirty"&&state.toString()!="clean")||!revision.isString()||(!revision.toString().isEmpty()&&!uuid(revision.toString()))||
       !origin.isString()||(origin.toString()!="navigation"&&origin.toString()!="restore"&&origin.toString()!="empty"))return Code::Corrupt;
    if((result.sequence>0||origin.toString()!="empty")&&revision.toString().isEmpty())return Code::Corrupt;
    if(nonce.toString().isEmpty()) {
        if(pid.toDouble()!=0||start!=0||result.sequence!=0||state.toString()!="clean"||origin.toString()!="restore")return Code::Corrupt;
    }else if(pid.toDouble()==0||start<=1)return Code::Corrupt;
    return Code::Ok;
}
bool sameClaim(const Records& row,const ownership::Guard& guard) {
    return row.present&&row.record.value("claimNonce").toString()==guard.claim->nonce&&
        row.record.value("pid").toInteger()==guard.claim->pid&&row.record.value("start").toString()==QString::number(guard.claim->stamp);
}
void install(QJsonObject& root,Records row,const ownership::Guard& guard) {
    row.recovery["version"]=1;row.record["version"]=1;
    row.windows[QString::number(guard.slot)]=row.record;row.recovery["windows"]=row.windows;root["recovery"]=row.recovery;
}
ownership::Guard acquire(const WindowClaim& identity,Options options) {
    const auto claim=glancelot::detail::WindowClaimAccess::get(identity);
    if(!claim){ownership::Guard result;result.status=status(Code::Conflict,options);return result;}
    return ownership::acquire(identity,{claim->directory,claim->lifetime},options);
}
settings_detail::Request request(const ownership::Guard& guard,Options options) {
    settings_detail::Request result;result.context={guard.directory,guard.lifetime};result.options=options;
    result.policy=settings_detail::Policy::Checkpoint;result.expectedNamespace=guard.namespaceId;
    // The held session guard prevents another cooperating claim changing its
    // record; the atomic flag also closes same-process release before replace.
    result.validate=[claim=guard.claim,options]{return status(claim->active.load()?Code::Ok:Code::Conflict,options);};
    return result;
}
Code position(const QJsonObject& root,int slot,ResumeState& result,Options options) {
    auto source=root;source["schema"]=1;
    policy::Selection selected;selected.slot=slot;selected.slotSection=SlotSection::Position;
    const auto valid=policy::merge(root,source,selected,options);if(valid.status.code!=Code::Ok)return valid.status.code;
    const auto row=root.value("windows").toObject().value(QString::number(slot)).toObject();
    result.folder=row.value("folder").toString();result.image=row.value("image").toString();
    result.hasPosition=!result.folder.isEmpty()||!result.image.isEmpty();return Code::Ok;
}
bool validPath(const QString& value) {return value.size()<=32768&&!value.contains(QChar(0))&&(value.isEmpty()||QDir::isAbsolutePath(value));}
}

BeginResult beginWindow(const WindowClaim& identity,Options options) {
    auto guard=acquire(identity,options);if(!guard.held())return {guard.status,{}};
    ResumeState previous;
    const auto written=settings_detail::transact(request(guard,options),[&](const settings_detail::View& view) {
        Records row;auto code=records(view.base,guard.slot,row);if(code!=Code::Ok)return settings_detail::Edit{code,{}};
        code=position(view.base,guard.slot,previous,options);if(code!=Code::Ok)return settings_detail::Edit{code,{}};
        previous.priorDirty=row.present&&row.record.value("state").toString()=="dirty";
        previous.checkpointRevision=row.record.value("checkpointRevision").toString();
        if(sameClaim(row,guard))return settings_detail::Edit{Code::NoChange,{}}; // never reset this claim's sequence
        if(previous.hasPosition&&previous.checkpointRevision.isEmpty())previous.checkpointRevision=QUuid::createUuid().toString(QUuid::WithoutBraces);
        row.record["claimNonce"]=guard.claim->nonce;row.record["pid"]=guard.claim->pid;row.record["start"]=QString::number(guard.claim->stamp);
        row.record["state"]="dirty";row.record["sequence"]="0";row.record["checkpointRevision"]=previous.checkpointRevision;
        if(!row.present)row.record["origin"]=previous.hasPosition?"navigation":"empty";
        auto replacement=view.base;install(replacement,std::move(row),guard);
        return settings_detail::Edit{Code::Committed,std::move(replacement)};
    });
    if(written.status.code!=Code::Committed&&written.status.code!=Code::NoChange)previous={};
    return {written.status,std::move(previous)};
}
CheckpointResult checkpoint(const WindowClaim& identity,const CheckpointInput& input,Options options) {
    if(!input.sequence||!validPath(input.folder)||!validPath(input.image))return {status(Code::Corrupt,options),{},0};
    auto guard=acquire(identity,options);if(!guard.held())return {guard.status,{},0};
    QString revision;
    const auto written=settings_detail::transact(request(guard,options),[&](const settings_detail::View& view) {
        Records row;const auto code=records(view.base,guard.slot,row);if(code!=Code::Ok)return settings_detail::Edit{code,{}};
        if(!sameClaim(row,guard)||row.record.value("state")!=QJsonValue("dirty")||input.sequence<=row.sequence)return settings_detail::Edit{Code::Conflict,{}};
        auto replacement=view.base;
        const auto windowsValue=replacement.value("windows");if(!windowsValue.isUndefined()&&!windowsValue.isObject())return settings_detail::Edit{Code::Corrupt,{}};
        auto windows=windowsValue.toObject();const auto key=QString::number(guard.slot);const auto old=windows.value(key);
        if(!old.isUndefined()&&!old.isObject())return settings_detail::Edit{Code::Corrupt,{}};
        auto position=old.toObject();position["folder"]=input.folder;position["image"]=input.image;position["saved"]=QDateTime::currentSecsSinceEpoch();
        windows[key]=position;replacement["windows"]=windows;
        revision=QUuid::createUuid().toString(QUuid::WithoutBraces);row.record["checkpointRevision"]=revision;
        row.record["sequence"]=QString::number(input.sequence);row.record["state"]="dirty";row.record["origin"]="navigation";
        install(replacement,std::move(row),guard);return settings_detail::Edit{Code::Committed,std::move(replacement)};
    });
    const bool committed=written.status.code==Code::Committed;
    return {written.status,committed?revision:QString{},committed?input.sequence:0};
}
Status finishWindow(const WindowClaim& identity,const QString& expectedRevision,quint64 expectedSequence,Options options) {
    auto guard=acquire(identity,options);if(!guard.held())return guard.status;
    return settings_detail::transact(request(guard,options),[&](const settings_detail::View& view) {
        Records row;const auto code=records(view.base,guard.slot,row);if(code!=Code::Ok)return settings_detail::Edit{code,{}};
        if(!sameClaim(row,guard)||row.sequence!=expectedSequence||row.record.value("checkpointRevision").toString()!=expectedRevision)return settings_detail::Edit{Code::Conflict,{}};
        if(row.record.value("state")==QJsonValue("clean"))return settings_detail::Edit{Code::NoChange,{}};
        row.record["state"]="clean";auto replacement=view.base;install(replacement,std::move(row),guard);
        return settings_detail::Edit{Code::Committed,std::move(replacement)};
    }).status;
}
}
