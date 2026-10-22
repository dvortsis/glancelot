#include "settings_maintenance.hpp"
#include "settings_raw.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace glancelot::startup {
using namespace recovery;
QString codeName(Code code) {
    switch(code) {
    case Code::Ok:return "Ok";case Code::NoChange:return "NoChange";case Code::Committed:return "Committed";
    case Code::Conflict:return "Conflict";case Code::Cancelled:return "Cancelled";case Code::Unsupported:return "Unsupported";
    case Code::Corrupt:return "Corrupt";case Code::Busy:return "Busy";case Code::Unreadable:return "Unreadable";
    case Code::WriteFailed:return "WriteFailed";case Code::LimitExceeded:return "LimitExceeded";
    }return "Corrupt";
}
int exitCode(Code code) {
    switch(code) {
    case Code::Ok:case Code::NoChange:case Code::Committed:return 0;
    case Code::Conflict:return 3;case Code::Cancelled:return 4;case Code::Unsupported:return 5;
    case Code::Corrupt:case Code::LimitExceeded:return 6;
    case Code::Busy:case Code::Unreadable:case Code::WriteFailed:return 7;
    }return 6;
}
namespace {
QJsonObject envelope(const Status& status) {
    return {{"schema",1},{"code",codeName(status.code)},{"detail",status.detail},{"generation",QString::number(status.generation)}};
}
QJsonObject report(const PreviewResult& preview) {
    auto root=envelope(preview.status);root["truncated"]=preview.truncated;
    root["expected_current_revision"]=preview.expectedCurrentRevision;root["package_revision"]=preview.packageRevision;
    root["namespace_id"]=preview.namespaceId;root["policy_revision"]=preview.policyRevision;root["fingerprint"]=preview.fingerprint;
    QJsonArray rows;
    for(const auto& row:preview.rows) {
        QJsonArray path;for(const auto& part:row.path)path.append(part);
        QJsonObject encoded{{"path",path},{"had_before",row.hadBefore},{"has_after",row.hasAfter},
            {"change",row.change==Change::Added?"added":row.change==Change::Removed?"removed":"changed"},
            {"treatment",row.treatment==Treatment::Apply?"apply":row.treatment==Treatment::Invalid?"invalid":"preserve"}};
        if(row.hadBefore)encoded["before"]=row.before;
        if(row.hasAfter)encoded["after"]=row.after;
        rows.append(encoded);
    }
    root["rows"]=rows;return root;
}
MaintenanceResult bounded(QJsonObject object,Code code) {
    object["code"]=codeName(code);
    auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(bytes.size()>=MaxPreviewBytes) {
        const auto generation=object.value("generation").toString().toULongLong();
        object=envelope({Code::LimitExceeded,"JSON result exceeds the report limit.",generation});object["truncated"]=true;
        bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);code=Code::LimitExceeded;
    }
    bytes.append('\n');return {std::move(bytes),exitCode(code)};
}
}
MaintenanceResult maintenance(const Intent& intent,const Context& context,Options options) {
    if(!intent.maintenance()) {
        auto root=envelope({Code::Corrupt,"Expected a validated settings maintenance command.",options.generation});
        root["code"]="ArgumentError";return {QJsonDocument(root).toJson(QJsonDocument::Compact)+'\n',2};
    }
    if(intent.mode==Mode::Backup) {
        const auto captured=captureBackup(context,intent.packageDirectory,options);auto root=envelope(captured.status);
        root["destination"]=captured.destination;root["package_revision"]=captured.packageRevision;return bounded(root,captured.status.code);
    }
    Scope scope=GlobalScope{intent.global};
    if(intent.slotSection)scope=OfflineSlotScope{*intent.slotSection,intent.slot};
    auto proposed=preview(context,intent.packageDirectory,std::move(scope),intent.source,options);
    auto root=report(proposed);
    if(intent.mode==Mode::Diff||proposed.status.code!=Code::Ok)return bounded(root,proposed.status.code);
    if(!proposed.plan.applicable()||proposed.truncated)return bounded(envelope({Code::Corrupt,"Preview is not applicable.",options.generation}),Code::Corrupt);
    if(proposed.fingerprint!=intent.expectedFingerprint) {
        root["code"]=codeName(Code::Conflict);root["detail"]="Preview fingerprint differs; obtain a new settings-diff result.";
        return bounded(root,Code::Conflict);
    }
    // Confirm the report is bounded before mutation, even if future serializer
    // changes invalidate the conservative diff accounting.
    QString directory,namespaceId;const auto resolved=raw::resolve(context,options,directory,namespaceId);
    if(resolved.code!=Code::Ok)return bounded(envelope(resolved),resolved.code);
    const qint64 outcomeReserve=8192+6*(qint64(directory.size())+128);
    if(QJsonDocument(root).toJson(QJsonDocument::Compact).size()>=MaxPreviewBytes-outcomeReserve) {
        root["truncated"]=true;return bounded(root,Code::LimitExceeded);
    }
    const auto applied=apply(proposed.plan,options);
    const auto outcome=envelope(applied.status);for(auto it=outcome.begin();it!=outcome.end();++it)root[it.key()]=it.value();
    root["resulting_revision"]=applied.resultingRevision;root["preimage"]=applied.preimage;
    return bounded(root,applied.status.code);
}
}
