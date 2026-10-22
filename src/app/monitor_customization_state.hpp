#pragma once
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QVariantMap>
#include <cmath>
#include "layout_defaults.hpp"

namespace glancelot::monitor_state {
inline const QStringList paneIds{"tree","grid","preview","metadata"};
inline QVariantMap options() {
    return {{"displayId",""},{"windowMode","fullscreen"},{"fitMode","shrink"},
        {"viewportLink","independent"},{"background","solid"},{"backgroundColor","#000000"},
        {"guides",false},{"imageInfo",false},{"cursorPolicy","auto"},
        {"followSelection",true},{"folderChange","follow"},{"slideshowTiming","shared"}};
}
inline QVariantMap routes() {
    QVariantMap out;
    for(const auto& id:paneIds) out[id]=QVariantMap{{"detached",false},{"displayId",""},
        {"geometry",QVariantMap{{"x",80},{"y",80},{"width",800},{"height",600}}},{"frozen",false}};
    return out;
}
inline bool text(const QVariant& v,qsizetype max=256) {
    return v.metaType().id()==QMetaType::QString&&v.toString().size()<=max&&!v.toString().contains(QChar(0));
}
inline bool integer(const QVariant& v,double low,double high) {
    const auto type=v.metaType().id();
    if(type!=QMetaType::Int&&type!=QMetaType::UInt&&type!=QMetaType::LongLong&&type!=QMetaType::ULongLong&&type!=QMetaType::Double)return false;
    bool ok=false;const auto x=v.toDouble(&ok);return ok&&std::isfinite(x)&&x>=low&&x<=high&&std::floor(x)==x;
}
inline bool geometry(const QVariantMap& m) {
    return m.size()==4&&integer(m.value("x"),-10000000,10000000)&&integer(m.value("y"),-10000000,10000000)
        &&integer(m.value("width"),160,16384)&&integer(m.value("height"),120,16384);
}
inline bool patchOptions(const QVariantMap& patch,QVariantMap& target) {
    const auto defaults=options();if(patch.size()>defaults.size())return false;
    const QMap<QString,QStringList> choices{
        {"windowMode",{"windowed","fullscreen"}},{"fitMode",{"shrink","fit","fill","actual"}},
        {"viewportLink",{"linked","independent"}},{"background",{"theme","solid","checker"}},
        {"cursorPolicy",{"auto","visible","hidden"}},{"folderChange",{"follow","hold","disable"}},
        {"slideshowTiming",{"shared","independent"}}};
    for(auto it=patch.begin();it!=patch.end();++it) {
        if(!defaults.contains(it.key()))return false;
        if(choices.contains(it.key())) {if(!text(it.value())||!choices[it.key()].contains(it.value().toString()))return false;}
        else if(it.key()=="displayId") {if(!text(it.value()))return false;}
        else if(it.key()=="backgroundColor") {
            const auto s=it.value().toString();if(!text(it.value(),7)||s.size()!=7||!s.startsWith('#'))return false;
            for(auto c:s.mid(1))if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
        } else if(it.value().metaType().id()!=QMetaType::Bool)return false;
    }
    for(auto it=patch.begin();it!=patch.end();++it)target[it.key()]=it.value();return true;
}
inline bool validRoutes(const QVariantMap& value) {
    if(value.size()!=4)return false;
    for(const auto& id:paneIds) {
        const auto p=value.value(id).toMap();
        if(p.isEmpty()||p.value("detached").metaType().id()!=QMetaType::Bool||p.value("frozen").metaType().id()!=QMetaType::Bool
           ||!text(p.value("displayId"))||!geometry(p.value("geometry").toMap()))return false;
    }return true;
}
inline bool dock(const QVariantMap& m) {
    if(m.value("version").toInt()!=1||m.value("locked").metaType().id()!=QMetaType::Bool)return false;
    const auto center=m.value("center").toMap();
    if(!QStringList{"horizontal","vertical"}.contains(center.value("orientation").toString()))return false;
    const auto ratio=center.value("ratio").toDouble();if(!std::isfinite(ratio)||ratio<.1||ratio>.9)return false;
    if(!QStringList{"preview","grid","combined"}.contains(center.value("mode").toString()))return false;
    const auto panes=m.value("panes").toMap();
    for(const auto& id:QStringList{"topbar","statusbar","filmstrip","tree","preview","grid","metadata"}) {
        const auto p=panes.value(id).toMap();
        if(!QStringList{"left","right","top","bottom","center"}.contains(p.value("region").toString())
           ||!integer(p.value("order"),0,99)||!std::isfinite(p.value("extent").toDouble())||p.value("extent").toDouble()<32||p.value("extent").toDouble()>4096
           ||p.value("visible").metaType().id()!=QMetaType::Bool||p.value("frozen").metaType().id()!=QMetaType::Bool)return false;
        if(p.value("region")=="center"&&id!="preview"&&id!="grid")return false;
    }return true;
}
inline QJsonObject ownedDock(const QVariantMap& state) {
    const auto source=QJsonObject::fromVariantMap(state);QJsonObject result;
    for(const auto* key:{"version","locked"})result[key]=source.value(key);
    QJsonObject center;for(const auto* key:{"orientation","ratio","mode"})center[key]=source.value("center").toObject().value(key);result["center"]=center;
    QJsonObject panes;for(const auto& id:layout_detail::defaults().keys()) {
        QJsonObject pane;for(const auto* key:{"region","order","extent","visible","frozen"})pane[key]=source.value("panes").toObject().value(id).toObject().value(key);panes[id]=pane;
    }result["panes"]=panes;return result;
}
inline bool snapshot(const QVariantMap& s,const QString& kind) {
    auto o=options();if(!s.contains("output")||!patchOptions(s.value("output").toMap(),o))return false;
    if(!QStringList{"mirror","pinned","comparison"}.contains(s.value("routingMode").toString()))return false;
    return kind=="presentation"||(kind=="workspace"&&validRoutes(s.value("panes").toMap())&&dock(s.value("dock").toMap()));
}
inline bool presets(const QVariantList& rows) {
    if(rows.size()>64)return false;QSet<QString> ids,names;
    for(const auto& v:rows) {
        const auto r=v.toMap();const auto id=r.value("id");const auto name=r.value("name");const auto kind=r.value("kind").toString();
        if(!text(id)||id.toString().isEmpty()||ids.contains(id.toString())||!text(name,160)
           ||name.toString().trimmed().isEmpty()||name.toString().toUcs4().size()>80||names.contains(name.toString().trimmed().toCaseFolded())
           ||!QStringList{"presentation","workspace"}.contains(kind)||!snapshot(r.value("snapshot").toMap(),kind))return false;
        ids.insert(id.toString());names.insert(name.toString().trimmed().toCaseFolded());
    }return true;
}
inline bool slot(const QVariantMap& s) {
    if(s.value("output").metaType().id()!=QMetaType::QVariantMap||s.value("panes").metaType().id()!=QMetaType::QVariantMap||s.value("presets").metaType().id()!=QMetaType::QVariantList)return false;
    if(s.contains("outputsByDisplay")) {
        if(s.value("outputsByDisplay").metaType().id()!=QMetaType::QVariantMap)return false;
        const auto saved=s.value("outputsByDisplay").toMap();if(saved.size()>16)return false;
        for(auto it=saved.begin();it!=saved.end();++it){auto candidate=options();if(!text(it.key())||it.key().isEmpty()||it.value().metaType().id()!=QMetaType::QVariantMap||!patchOptions(it.value().toMap(),candidate))return false;}
    }
    auto o=options();return patchOptions(s.value("output").toMap(),o)&&validRoutes(s.value("panes").toMap())
        &&presets(s.value("presets").toList());
}
inline bool normalize(const QVariantMap& raw,QVariantMap& result) {
    result=raw;auto output=options();
    if(raw.contains("output")) {
        if(raw.value("output").metaType().id()!=QMetaType::QVariantMap)return false;
        QVariantMap known;const auto stored=raw.value("output").toMap();
        for(auto it=stored.begin();it!=stored.end();++it)if(output.contains(it.key()))known[it.key()]=it.value();
        if(!patchOptions(known,output))return false;
    }
    result["output"]=output;auto paneRoutes=routes();
    if(raw.contains("panes")) {
        if(raw.value("panes").metaType().id()!=QMetaType::QVariantMap)return false;
        const auto stored=raw.value("panes").toMap();
        for(const auto& id:paneIds)if(stored.contains(id)) {
            if(stored.value(id).metaType().id()!=QMetaType::QVariantMap)return false;
            auto route=paneRoutes.value(id).toMap();const auto saved=stored.value(id).toMap();
            for(auto it=saved.begin();it!=saved.end();++it) {
                if(it.key()=="geometry") {
                    if(it.value().metaType().id()!=QMetaType::QVariantMap)return false;
                    auto geom=route.value("geometry").toMap();const auto extra=it.value().toMap();for(auto g=extra.begin();g!=extra.end();++g)if(geom.contains(g.key()))geom[g.key()]=g.value();route["geometry"]=geom;
                }else if(it.key()!="frozen")route[it.key()]=it.value();
            }paneRoutes[id]=route;
        }
    }
    result["panes"]=paneRoutes;if(!result.contains("presets"))result["presets"]=QVariantList{};
    if(raw.contains("outputsByDisplay")) {
        if(raw.value("outputsByDisplay").metaType().id()!=QMetaType::QVariantMap)return false;
        const auto stored=raw.value("outputsByDisplay").toMap();if(stored.size()>64)return false;QVariantMap active;
        for(auto it=stored.begin();it!=stored.end();++it){
            if(!text(it.key())||it.key().isEmpty()||it.value().metaType().id()!=QMetaType::QVariantMap)return false;
            auto record=options();QVariantMap known;const auto original=it.value().toMap();for(auto field=original.begin();field!=original.end();++field)if(record.contains(field.key()))known[field.key()]=field.value();
            if(known.isEmpty())continue;if(!patchOptions(known,record))return false;active[it.key()]=record;
        }if(active.size()>16)return false;result["outputsByDisplay"]=active;
    }
    return slot(result);
}
inline QVariantMap known(const QVariantMap& s) {
    QVariantMap k;for(const auto& key:QStringList{"output","panes","presets","selectedPreset","outputsByDisplay"})if(s.contains(key))k[key]=s[key];
    if(k.value("panes").metaType().id()==QMetaType::QVariantMap){auto routes=k["panes"].toMap();for(const auto& id:paneIds)if(routes.value(id).metaType().id()==QMetaType::QVariantMap){auto route=routes[id].toMap();route.remove("frozen");routes[id]=route;}k["panes"]=routes;}
    return k;
}
inline bool feature(const QJsonObject& object) {
    if(object.value("version")!=QJsonValue(1)||QJsonDocument(object).toJson(QJsonDocument::Indented).size()>256*1024||!object.value("slots").isObject())return false;
    const auto slotRows=object.value("slots").toObject();
    for(auto it=slotRows.begin();it!=slotRows.end();++it){bool ok=false;const auto id=it.key().toInt(&ok);QVariantMap normalized;if(!ok||id<1||id>1000000||!it.value().isObject()||!normalize(it.value().toObject().toVariantMap(),normalized))return false;}
    return true;
}
} // namespace glancelot::monitor_state
