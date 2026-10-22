#include "glancelot/settings_recovery.hpp"
#include "settings_raw.hpp"
#include "settings_package.hpp"
#include "settings_policy.hpp"
#include "settings_diff.hpp"
#include "settings_transaction.hpp"
#include "window_claim_guard.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <filesystem>
#include <limits>

namespace glancelot::recovery {
namespace detail {
struct RestorePlanData {
    Context context;
    Scope scope;
    SourceMember source;
    QString packageDirectory,namespaceId,currentRevision,packageRevision,policyRevision,fingerprint;
    QJsonObject replacement;
};
struct RecoveryAccess {
    static RestorePlan make(std::shared_ptr<const RestorePlanData> value){RestorePlan plan;plan.data_=std::move(value);return plan;}
    static auto get(const RestorePlan& plan){return plan.data_;}
};
}
namespace {
Status status(Code code,Options options,QString detail={}){return {code,std::move(detail),options.generation};}
ownership::Guard scopeGuard(const Scope& scope,const Context& context,Options options) {
    if(const auto* owned=std::get_if<OwnedSlotScope>(&scope))return ownership::acquire(owned->claim,context,options);
    if(const auto* offline=std::get_if<OfflineSlotScope>(&scope))return ownership::acquireOffline(offline->slot,context,options);
    ownership::Guard guard;guard.status=status(Code::Ok,options);return guard;
}
policy::Selection selection(const Scope& scope,const ownership::Guard& guard) {
    policy::Selection result;
    if(const auto* global=std::get_if<GlobalScope>(&scope))result.global=global->section;
    else {result.slot=guard.slot;result.slotSection=std::holds_alternative<OwnedSlotScope>(scope)?std::get<OwnedSlotScope>(scope).section:std::get<OfflineSlotScope>(scope).section;}
    return result;
}
QString scopeName(const Scope& scope) {
    if(const auto* global=std::get_if<GlobalScope>(&scope))switch(global->section) {
    case GlobalSection::All:return "global";case GlobalSection::Viewing:return "viewing";case GlobalSection::Browsing:return "browsing";
    case GlobalSection::Appearance:return "appearance";case GlobalSection::Bindings:return "bindings";case GlobalSection::Performance:return "performance";
    }
    if(const auto* owned=std::get_if<OwnedSlotScope>(&scope))return owned->section==SlotSection::Position?"owned-position":"owned-layout";
    const auto& offline=std::get<OfflineSlotScope>(scope);return offline.section==SlotSection::Position?"offline-position":"offline-layout";
}
QString fingerprint(const detail::RestorePlanData& plan,int slot) {
    QByteArray bytes("Glancelot.SettingsPreview");bytes.append(char(0));bytes.append(char(1));
    for(const auto& field:QStringList{plan.namespaceId,plan.policyRevision,plan.currentRevision,plan.packageRevision,scopeName(plan.scope),
            slot?QString::number(slot):QString{},plan.source==SourceMember::Primary?QStringLiteral("primary"):QStringLiteral("last-good")}) {
        const auto utf8=field.toUtf8();for(int shift=0;shift<64;shift+=8)bytes.append(char((quint64(utf8.size())>>shift)&255));bytes.append(utf8);
    }
    return "sf1:"+QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
// Position restore also installs an explicit restoration checkpoint. Generated
// record fields are part of the immutable preview replacement and Apply rows.
Status restoreCheckpoint(QJsonObject& replacement,const ownership::Guard& guard,QList<QStringList>& paths,Options options) {
    const auto rootValue=replacement.value("recovery");
    if(!rootValue.isUndefined()&&!rootValue.isObject())return status(Code::Corrupt,options);
    auto recovery=rootValue.toObject();
    if(!recovery.isEmpty()&&recovery.value("version")!=QJsonValue(1))return status(Code::Unsupported,options);
    const auto windowsValue=recovery.value("windows");if(!windowsValue.isUndefined()&&!windowsValue.isObject())return status(Code::Corrupt,options);
    auto windows=windowsValue.toObject();const auto key=QString::number(guard.slot);const auto old=windows.value(key);
    if(!old.isUndefined()&&!old.isObject())return status(Code::Corrupt,options);
    auto record=old.toObject();if(!record.isEmpty()&&record.value("version")!=QJsonValue(1))return status(Code::Unsupported,options);
    quint64 sequence=0;
    if(guard.claim&&record.value("claimNonce").toString()==guard.claim->nonce) {
        bool valid=false;sequence=record.value("sequence").toString().toULongLong(&valid);
        if(!valid||QString::number(sequence)!=record.value("sequence").toString())return status(Code::Corrupt,options);
        // Restore changes the revision, invalidating any previous clean
        // acknowledgement, but does not consume a navigation sequence from
        // the live Session. Its next accepted checkpoint must still fit.
    }
    recovery["version"]=1;record["version"]=1;
    record["claimNonce"]=guard.claim?guard.claim->nonce:QString{};record["pid"]=guard.claim?guard.claim->pid:0;
    record["start"]=guard.claim?QString::number(guard.claim->stamp):QStringLiteral("0");
    record["state"]=guard.claim?"dirty":"clean";record["sequence"]=QString::number(sequence);
    record["checkpointRevision"]=QUuid::createUuid().toString(QUuid::WithoutBraces);record["origin"]="restore";
    windows[key]=record;recovery["windows"]=windows;replacement["recovery"]=recovery;
    paths.append(QStringList{"recovery","version"});
    for(const auto* field:{"version","claimNonce","pid","start","state","sequence","checkpointRevision","origin"})paths.append(QStringList{"recovery","windows",key,QLatin1String(field)});
    return status(Code::Ok,options);
}
settings_detail::Seal sealPreimage(const RawSnapshot& snapshot,Options options) {
    namespace fs=std::filesystem;
    const auto root=QDir(snapshot.directory).filePath("recovery-preimages");std::error_code error;
#ifdef Q_OS_WIN
    const auto native=fs::path(root.toStdWString());
#else
    const auto native=fs::path(QFile::encodeName(root).constData());
#endif
    auto kind=fs::symlink_status(native,error);
    if(error==std::errc::no_such_file_or_directory||(!error&&kind.type()==fs::file_type::not_found)) {
        error.clear();if(!fs::create_directory(native,error)&&error)return {status(Code::WriteFailed,options),{}};
        kind=fs::symlink_status(native,error);
    }
    if(error||kind.type()!=fs::file_type::directory)return {status(Code::WriteFailed,options),{}};
    const auto destination=QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto captured=package::write(snapshot,destination,options);
    return {captured.status,captured.destination};
}
}
PreviewResult preview(const Context& input,const QString& packageDirectory,Scope scope,SourceMember member,Options options) {
    PreviewResult result;result.status=status(Code::Corrupt,options);
    auto fail=[&](Status value){result.status=std::move(value);return result;};
    if(member!=SourceMember::Primary&&member!=SourceMember::LastGood)return result;
    const Context context=input;QString resolved,namespaceId;
    const auto resolvedStatus=raw::resolve(context,options,resolved,namespaceId);if(resolvedStatus.code!=Code::Ok)return fail(resolvedStatus);
    const auto loaded=package::read(packageDirectory,options);if(loaded.status.code!=Code::Ok)return fail(loaded.status);
    const auto& source=member==SourceMember::Primary?loaded.data->primary:loaded.data->lastGood;
    if(!source.present||source.kind==ParseKind::Corrupt)return fail(status(Code::Corrupt,options,"Selected backup member is absent or corrupt."));
    if(source.kind!=ParseKind::Supported)return fail(status(Code::Unsupported,options));
    auto guard=scopeGuard(scope,{resolved,context.lifetime},options);if(guard.status.code!=Code::Ok)return fail(guard.status);
    const auto current=readRaw({resolved,context.lifetime},options);if(current.status.code!=Code::Ok)return fail(current.status);
    const auto& raw=*current.snapshot;QJsonObject base;
    if(raw.primary.kind==ParseKind::Unsupported)return fail(status(Code::Unsupported,options));
    if(raw.primary.kind==ParseKind::Corrupt) {
        if(raw.lastGood.kind!=ParseKind::Supported)return fail(status(Code::Corrupt,options,"No supported current logical base; original bytes are retained."));
        base=QJsonDocument::fromJson(raw.lastGood.bytes).object();result.status.detail="Corrupt primary bytes will be preserved exactly; unknown logical fields cannot be reconstructed.";
    }else if(raw.primary.present)base=QJsonDocument::fromJson(raw.primary.bytes).object();
    const auto sourceObject=QJsonDocument::fromJson(source.bytes).object();
    auto merged=policy::merge(base,sourceObject,selection(scope,guard),options);
    if(merged.status.code!=Code::Ok) {
        if(!merged.invalidPath.isEmpty()){DiffRow row;row.path=merged.invalidPath;row.treatment=Treatment::Invalid;result.rows.append(std::move(row));}
        return fail(merged.status);
    }
    const auto selected=selection(scope,guard);
    if(selected.slotSection&&*selected.slotSection==SlotSection::Position) {
        const auto checkpointStatus=restoreCheckpoint(*merged.replacement,guard,merged.ownedPaths,options);
        if(checkpointStatus.code!=Code::Ok)return fail(checkpointStatus);
    }
    // Use the writer's exact serialization, including indentation. Two bounded
    // inputs can merge into an oversized output; never offer such a plan.
    if(QJsonDocument(*merged.replacement).toJson(QJsonDocument::Indented).size()>MaxMemberBytes)
        return fail(status(Code::LimitExceeded,options,"Merged settings exceed the settings-file limit."));
    const auto report=diff::build(base,sourceObject,*merged.replacement,merged.ownedPaths,options);
    result.rows=report.rows;result.truncated=report.truncated;
    if(report.status.code!=Code::Ok)return fail(report.status);
    if(guard.claim&&!guard.claim->active.load())return fail(status(Code::Conflict,options));
    auto plan=std::make_shared<detail::RestorePlanData>();plan->context={resolved,context.lifetime};plan->scope=std::move(scope);plan->source=member;
    plan->packageDirectory=packageDirectory;plan->namespaceId=raw.namespaceId;plan->currentRevision=raw.revision;
    plan->packageRevision=loaded.data->revision;plan->policyRevision=policy::revision();plan->replacement=std::move(*merged.replacement);
    plan->fingerprint=fingerprint(*plan,guard.slot);
    result.expectedCurrentRevision=plan->currentRevision;result.packageRevision=plan->packageRevision;
    result.namespaceId=plan->namespaceId;result.policyRevision=plan->policyRevision;result.fingerprint=plan->fingerprint;
    result.plan=detail::RecoveryAccess::make(std::move(plan));result.status.code=Code::Ok;return result;
}
ApplyResult apply(const RestorePlan& opaque,Options options) {
    const auto plan=detail::RecoveryAccess::get(opaque);if(!plan)return {status(Code::Corrupt,options),{}, {}};
    auto guard=scopeGuard(plan->scope,plan->context,options);if(guard.status.code!=Code::Ok)return {guard.status,{}, {}};
    settings_detail::Request request;request.context=plan->context;request.options=options;request.policy=settings_detail::Policy::Restore;
    request.expectedRevision=plan->currentRevision;request.expectedNamespace=plan->namespaceId;
    request.validate=[&] {
        if(options.cancellation.stop_requested())return status(Code::Cancelled,options);
        if(guard.claim&&!guard.claim->active.load())return status(Code::Conflict,options);
        if(policy::revision()!=plan->policyRevision)return status(Code::Conflict,options);
        const auto loaded=package::read(plan->packageDirectory,options);
        if(loaded.status.code!=Code::Ok)return loaded.status;
        return status(loaded.data->revision==plan->packageRevision?Code::Ok:Code::Conflict,options);
    };
    const auto checked=request.validate();if(checked.code!=Code::Ok)return {checked,{}, {}};
    const auto applied=settings_detail::transact(request,[&](const settings_detail::View& view) {
        return settings_detail::Edit{view.base==plan->replacement?Code::NoChange:Code::Committed,plan->replacement};
    },[&](const RawSnapshot& snapshot){return sealPreimage(snapshot,options);});
    return {applied.status,applied.revision,applied.preimage};
}
}
