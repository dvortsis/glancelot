#include "settings_policy.hpp"
#include "glancelot/command_registry.hpp"
#include "binding_validation.hpp"
#include "layout_defaults.hpp"
#include "monitor_customization_state.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <functional>

namespace glancelot::recovery::policy {
namespace {
QJsonValue valueAt(const QJsonObject& root,const QStringList& path) {
    QJsonValue result(root);for(const auto& part:path) {
        if(!result.isObject())return QJsonValue(QJsonValue::Undefined);
        result=result.toObject().value(part);
    }return result;
}
bool objectParents(const QJsonObject& root,const QStringList& path) {
    QJsonValue result(root);
    for(qsizetype i=0;i+1<path.size();++i) {
        if(result.isUndefined())return true;
        if(!result.isObject())return false;
        result=result.toObject().value(path[i]);
    }
    return result.isUndefined()||result.isObject();
}
void setAt(QJsonObject& root,const QStringList& path,qsizetype offset,const QJsonValue& value) {
    const auto& key=path[offset];
    if(offset+1==path.size()) {if(value.isUndefined())root.remove(key);else root[key]=value;return;}
    const bool existed=root.contains(key);auto child=root.value(key).toObject();setAt(child,path,offset+1,value);
    // Missing source keys do not manufacture empty parent objects.
    if(!child.isEmpty()||existed)root[key]=child;
}
bool number(const QJsonValue& value,double low,double high,bool integral=false) {
    if(!value.isDouble())return false;const auto x=value.toDouble();
    return std::isfinite(x)&&x>=low&&x<=high&&(!integral||std::floor(x)==x);
}
bool choice(const QJsonValue& value,std::initializer_list<const char*> choices) {
    if(!value.isString())return false;for(const auto* candidate:choices)if(value.toString()==QLatin1String(candidate))return true;return false;
}
QByteArray themeCatalog() {QFile file(":/themes/catalog.json");return file.open(QIODevice::ReadOnly)?file.read(1024*1024+1):QByteArray{};}
struct Field {QStringList path;std::function<bool(const QJsonValue&)> valid;};
}
QString revision() {
    QJsonArray commands;for(const auto& command:commandRegistry())commands.append(QJsonObject{
        {"id",QString::fromStdString(command.id)},{"binding",QString::fromStdString(command.defaultBinding)},
        {"scope",QString::fromStdString(command.scope)},{"hold",command.hold},{"repeat",command.repeat}});
    QByteArray bytes("Glancelot.SettingsPolicy");bytes.append(char(0));bytes.append(char(1));
    const auto append=[&](const QByteArray& field){for(int shift=0;shift<64;shift+=8)bytes.append(char((quint64(field.size())>>shift)&255));bytes.append(field);};
    append(QJsonDocument(commands).toJson(QJsonDocument::Compact));append(themeCatalog());
    append(QJsonDocument(layout_detail::defaults()).toJson(QJsonDocument::Compact));
    append(QByteArrayLiteral("known-fields-v4-monitor-customization"));
    return "policy1:"+QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
Result merge(const QJsonObject& current,const QJsonObject& source,Selection selection,Options options) {
    Result result;result.status={Code::Ok,{},options.generation};
    auto refuse=[&](Code code,const QStringList& path,QString detail={}) {
        result.status={code,std::move(detail),options.generation};result.invalidPath=path;result.replacement.reset();return result;
    };
    if(options.cancellation.stop_requested())return refuse(Code::Cancelled,{});
    if(source.value("schema")!=QJsonValue(1)||(!current.isEmpty()&&current.value("schema")!=QJsonValue(1)))return refuse(Code::Unsupported,{"schema"});
    QList<Field> fields;const auto boolean=[](const QJsonValue& v){return v.isBool();};
    const auto add=[&](QStringList path,std::function<bool(const QJsonValue&)> valid){fields.append({std::move(path),std::move(valid)});};
    const auto enabled=[&](GlobalSection section){return !selection.slotSection&&(selection.global==GlobalSection::All||selection.global==section);};
    if(selection.slotSection) {
        if(*selection.slotSection!=SlotSection::Position&&*selection.slotSection!=SlotSection::Layout)return refuse(Code::Corrupt,{},"Invalid selected slot section.");
        if(selection.slot<1||selection.slot>1000000)return refuse(Code::Corrupt,{},"Invalid selected slot.");
        const QString slot=QString::number(selection.slot);
        if(*selection.slotSection==SlotSection::Position) {
            for(const auto* key:{"folder","image"})add({"windows",slot,QLatin1String(key)},[](const QJsonValue& v){
                if(!v.isString())return false;const auto text=v.toString();return text.size()<=32768&&!text.contains(QChar(0))&&(text.isEmpty()||QDir::isAbsolutePath(text));});
            add({"windows",slot,"saved"},[](const QJsonValue& v){return number(v,0,9007199254740991.0,true);});
        }else {
            const QStringList prefix{"layouts",slot};
            const auto sourceLayout=valueAt(source,prefix),currentLayout=valueAt(current,prefix);
            for(const auto& layout:{sourceLayout,currentLayout})if(layout.isObject()) {
                const auto version=layout.toObject().value("version");
                if(number(version,2,9007199254740991.0,true))return refuse(Code::Unsupported,{"layouts",slot,"version"});
                if(!layout.toObject().isEmpty()&&version!=QJsonValue(1))return refuse(Code::Corrupt,{"layouts",slot,"version"});
            }
            add({"layouts",slot,"version"},[](const QJsonValue& v){return v==QJsonValue(1);});
            add({"layouts",slot,"locked"},boolean);
            add({"layouts",slot,"center","orientation"},[](const QJsonValue& v){return choice(v,{"horizontal","vertical"});});
            add({"layouts",slot,"center","ratio"},[](const QJsonValue& v){return number(v,0.1,0.9);});
            add({"layouts",slot,"center","mode"},[](const QJsonValue& v){return choice(v,{"preview","grid","combined"});});
            for(const auto& pane:layout_detail::defaults().keys()) {
                const QStringList base{"layouts",slot,"panes",pane};
                add(base+QStringList{"region"},[](const QJsonValue& v){return choice(v,{"left","right","top","bottom","center"});});
                add(base+QStringList{"order"},[](const QJsonValue& v){return number(v,0,99,true);});
                add(base+QStringList{"extent"},[](const QJsonValue& v){return number(v,32,4096);});
                add(base+QStringList{"visible"},boolean);add(base+QStringList{"frozen"},boolean);
            }
            for(const auto& root:{source,current}) {
                const auto feature=root.value("monitorCustomization");
                if(!feature.isUndefined()&&(!feature.isObject()||feature.toObject().value("version")!=QJsonValue(1)))
                    return refuse(Code::Unsupported,{"monitorCustomization","version"});
            }
            add({"monitorCustomization","version"},[](const QJsonValue& v){return v==QJsonValue(1);});
            const QStringList monitorBase{"monitorCustomization","slots",slot};
            for(const auto& key:monitor_state::options().keys())add(monitorBase+QStringList{"output",key},[key](const QJsonValue& v){auto o=monitor_state::options();return monitor_state::patchOptions({{key,v.toVariant()}},o);});
            for(const auto& pane:monitor_state::paneIds) {
                const auto route=monitorBase+QStringList{"panes",pane};
                add(route+QStringList{"detached"},boolean);add(route+QStringList{"frozen"},boolean);
                add(route+QStringList{"displayId"},[](const QJsonValue& v){return monitor_state::text(v.toVariant());});
                for(const auto* coordinate:{"x","y"})add(route+QStringList{"geometry",QLatin1String(coordinate)},[](const QJsonValue& v){return number(v,-10000000,10000000,true);});
                add(route+QStringList{"geometry","width"},[](const QJsonValue& v){return number(v,160,16384,true);});
                add(route+QStringList{"geometry","height"},[](const QJsonValue& v){return number(v,120,16384,true);});
            }
            add(monitorBase+QStringList{"presets"},[](const QJsonValue& v){return v.isArray()&&QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact).size()<=256*1024&&monitor_state::presets(v.toArray().toVariantList());});
            add(monitorBase+QStringList{"selectedPreset"},[](const QJsonValue& v){return monitor_state::text(v.toVariant());});
            const auto sourceRecords=valueAt(source,monitorBase+QStringList{"outputsByDisplay"});
            const auto currentRecords=valueAt(current,monitorBase+QStringList{"outputsByDisplay"});
            QSet<QString> displayIds;
            for(const auto& records:{sourceRecords,currentRecords}) {
                if(!records.isUndefined()&&(!records.isObject()||records.toObject().size()>64))return refuse(Code::Corrupt,monitorBase+QStringList{"outputsByDisplay"});
                for(const auto& id:records.toObject().keys()){if(id.isEmpty()||id.size()>256)return refuse(Code::Corrupt,monitorBase+QStringList{"outputsByDisplay"});displayIds.insert(id);}
            }
            for(const auto& id:displayIds)for(const auto& key:monitor_state::options().keys())
                add(monitorBase+QStringList{"outputsByDisplay",id,key},[key](const QJsonValue& v){auto o=monitor_state::options();return monitor_state::patchOptions({{key,v.toVariant()}},o);});
        }
    }else {
        switch(selection.global) {
        case GlobalSection::All:case GlobalSection::Viewing:case GlobalSection::Browsing:case GlobalSection::Appearance:case GlobalSection::Bindings:case GlobalSection::Performance:break;
        default:return refuse(Code::Corrupt,{},"Invalid selected section.");
        }
    }
    if(enabled(GlobalSection::Viewing)) {
        add({"boundary"},[](const QJsonValue& v){return choice(v,{"continue","stop","loop"});});
        add({"wheel"},[](const QJsonValue& v){return choice(v,{"navigate","zoom","pan"});});
        for(const auto* modifier:{"ctrl","shift","alt"})
            add({"wheelModifiers",QLatin1String(modifier)},[](const QJsonValue& v){return choice(v,{"navigate","zoom","pan","pan-horizontal"});});
    }
    if(enabled(GlobalSection::Browsing)) {
        for(const auto* key:{"filmstrip","tree","treePinned"})add({QLatin1String(key)},boolean);
        add({"sort","key"},[](const QJsonValue& v){return choice(v,{"name","modified","size","shuffle"});});
        add({"sort","descending"},boolean);add({"sort","seed"},[](const QJsonValue& v){return number(v,0,9007199254740991.0,true);});
    }
    if(enabled(GlobalSection::Appearance)) {
        QJsonParseError error;const auto catalogBytes=themeCatalog();const auto catalog=QJsonDocument::fromJson(catalogBytes,&error);
        if(catalogBytes.size()>1024*1024||error.error!=QJsonParseError::NoError||!catalog.isArray())return refuse(Code::Unreadable,{"appearance","family"},"Bundled theme catalog unavailable.");
        QSet<QString> families;for(const auto& row:catalog.array())if(row.isObject()&&row.toObject().value("id").isString())families.insert(row.toObject().value("id").toString());
        if(families.isEmpty())return refuse(Code::Unreadable,{"appearance","family"});
        add({"textScale"},[](const QJsonValue& v){return number(v,0.75,2);});add({"reducedMotion"},boolean);
        add({"appearance","family"},[families](const QJsonValue& v){return v.isString()&&families.contains(v.toString());});
        add({"appearance","mode"},[](const QJsonValue& v){return choice(v,{"system","dark","light"});});
    }
    if(enabled(GlobalSection::Bindings))for(const auto& command:commandRegistry())add({"bindings",QString::fromStdString(command.id)},[](const QJsonValue& v){
        if(!v.isString())return false;const auto text=v.toString();const auto sequence=QKeySequence::fromString(text,QKeySequence::PortableText);
        return (text.trimmed().isEmpty()||!sequence.isEmpty())&&bindings::usableShortcut(sequence);
    });
    if(enabled(GlobalSection::Performance))add({"storageProfile"},[](const QJsonValue& v){return choice(v,{"auto","ssd","hdd","rotational","network"});});
    auto replacement=current;
    for(const auto& field:fields) {
        if(options.cancellation.stop_requested())return refuse(Code::Cancelled,{});
        result.ownedPaths.append(field.path);
        if(!objectParents(source,field.path))return refuse(Code::Corrupt,field.path,"Selected source section is malformed.");
        if(!objectParents(current,field.path))return refuse(Code::Corrupt,field.path,"Cannot preserve malformed current container.");
        const auto value=valueAt(source,field.path);
        if(!value.isUndefined()&&!field.valid(value))return refuse(Code::Corrupt,field.path,"Invalid selected source setting.");
        setAt(replacement,field.path,0,value);
    }
    if(selection.slotSection==SlotSection::Layout&&
       valueAt(source,{"layouts",QString::number(selection.slot),"version"}).isUndefined()) {
        // Removing owned values must leave unknown layout data readable by
        // PaneLayout, otherwise its next edit would discard a versionless row.
        auto layouts=replacement.value("layouts").toObject();const auto slot=QString::number(selection.slot);
        auto layout=layouts.value(slot).toObject();
        if(layout.value("center").isObject()&&layout.value("center").toObject().isEmpty())layout.remove("center");
        if(layout.value("panes").isObject()) {
            auto panes=layout.value("panes").toObject();
            for(const auto& pane:layout_detail::defaults().keys())
                if(panes.value(pane).isObject()&&panes.value(pane).toObject().isEmpty())panes.remove(pane);
            if(panes.isEmpty())layout.remove("panes");else layout["panes"]=panes;
        }
        if(layout.isEmpty())layouts.remove(slot);else {layout["version"]=1;layouts[slot]=layout;}
        if(!layouts.isEmpty()||replacement.contains("layouts"))replacement["layouts"]=layouts;
    }
    if(selection.slotSection==SlotSection::Layout&&replacement.value("monitorCustomization").isObject()) {
        auto feature=replacement.value("monitorCustomization").toObject();
        if(!feature.isEmpty()){feature["version"]=1;replacement["monitorCustomization"]=feature;}
    }
    if(enabled(GlobalSection::Bindings)) {
        const auto resolved=resolveCommandBindings(replacement.value("bindings").toObject());
        if(!resolved.valid)return refuse(Code::Corrupt,{"bindings"},resolved.error);
    }
    replacement["schema"]=1;result.replacement=std::move(replacement);return result;
}
}
