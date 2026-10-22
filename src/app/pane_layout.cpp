#include "glancelot/pane_layout.hpp"
#include <QCoreApplication>
#include "glancelot/settings_file.hpp"
#include "settings_retry.hpp"
#include <memory>
#include "layout_defaults.hpp"
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace {
QString savingFeedback(){return QCoreApplication::translate("Layout","Saving layout preferences…");}
// Only an integer schema above ours is "newer" and preserved; anything else malformed is replaceable.
bool newerVersion(const QJsonObject& layout) {
    const auto value=layout.value("version");
    return value.isDouble()&&std::floor(value.toDouble())==value.toDouble()&&value.toDouble()>1;
}
bool currentVersion(const QJsonObject& layout){return layout.value("version").isDouble()&&layout.value("version").toDouble()==1;}
const QStringList& stableOrder() {static const QStringList ids{"topbar","statusbar","filmstrip","tree","preview","grid"};return ids;}
bool modeValid(const QString& value){return value=="preview"||value=="grid"||value=="combined";}
bool regionValid(const QString& value) {
    return QStringList{"left","right","top","bottom","center"}.contains(value);
}
QJsonObject validPane(const QJsonObject& source,QJsonObject fallback) {
    if(source.value("region").isString()&&regionValid(source.value("region").toString()))fallback["region"]=source["region"];
    const double order=source.value("order").toDouble(-1),extent=source.value("extent").toDouble(-1);
    if(std::isfinite(order)&&order>=0&&order<=99&&std::floor(order)==order)fallback["order"]=order;
    if(std::isfinite(extent)&&extent>=32&&extent<=4096)fallback["extent"]=extent;
    for(const auto* key:{"visible","frozen"})if(source.value(key).isBool())fallback[key]=source[key];
    return fallback;
}
}
QJsonObject PaneLayout::defaults() {
    return glancelot::layout_detail::defaults();
}
PaneLayout::PaneLayout(QObject* parent):QObject(parent),panes_(defaults()) {}
QVariantMap PaneLayout::panes() const{return panes_.toVariantMap();}
QVariantMap PaneLayout::pane(const QString& id) const{return panes_.value(id).toObject().toVariantMap();}
void PaneLayout::setSlot(int slot) {if(slot<0||slot==slot_)return;slot_=slot;load();}
void PaneLayout::load() {
    panes_=defaults();center_={{"orientation","horizontal"},{"ratio",0.5}};mode_="preview";locked_=false;feedback_.clear();
    if(slot_>0) {
        QJsonObject settings;
        if(!glancelot::readSettings(settings).ok())feedback_=QCoreApplication::translate("Layout","Layout settings could not be read; defaults are active.");
        else {
            glancelot::settings_detail::overlayPendingSettings(this,settings);
            const auto layoutsValue=settings.value("layouts");
            const auto savedValue=layoutsValue.toObject().value(QString::number(slot_));
            const auto saved=savedValue.toObject();
            if((!layoutsValue.isUndefined()&&!layoutsValue.isObject())||(!savedValue.isUndefined()&&!savedValue.isObject()))
                feedback_=QCoreApplication::translate("Layout","Layout settings are invalid; defaults are active.");
            if(!saved.isEmpty()&&newerVersion(saved))feedback_=QCoreApplication::translate("Layout","Unsupported layout version; defaults are active.");
            else if(!saved.isEmpty()&&!currentVersion(saved))feedback_=QCoreApplication::translate("Layout","Layout settings are invalid; defaults are active.");
            else {
                locked_=saved.value("locked").toBool(false);
                const auto center=saved.value("center").toObject();
                const auto orientation=center.value("orientation").toString();
                const double ratio=center.value("ratio").toDouble(-1);
                if(orientation=="horizontal"||orientation=="vertical")center_["orientation"]=orientation;
                if(std::isfinite(ratio)&&ratio>=0.1&&ratio<=0.9)center_["ratio"]=ratio;
                // A view preference, validated on its own: an unknown mode falls back without touching the split.
                const auto mode=center.value("mode").toString();
                if(modeValid(mode))mode_=mode;
                const auto stored=saved.value("panes").toObject();
                for(const auto& id:panes_.keys())panes_[id]=validPane(stored.value(id).toObject(),panes_.value(id).toObject());
            }
        }
    }
    if(feedback_.isEmpty()&&glancelot::settings_detail::hasPendingSettings(this,"layout/"+QString::number(slot_)+"/"))feedback_=savingFeedback();
    emit changed();
}
void PaneLayout::persist(const QString& id,const QJsonObject& fields,bool reset,bool lockOnly) {
    if(slot_==0)return;
    const auto unsupported=std::make_shared<bool>(false);
    struct Completion {bool done{},deferred{};};const auto completion=std::make_shared<Completion>();
    const auto slot=slot_;const auto panes=panes_;const auto centerState=center_;const auto mode=mode_;const auto locked=locked_;
    const auto directory=QDir::cleanPath(QFileInfo(glancelot::settingsDirectory()).absoluteFilePath());
    const auto channel="layout/"+QString::number(slot)+"/"+id+"/"+fields.keys().join('/')+(reset?"/reset":"")+(lockOnly?"/locked":"");
    glancelot::settings_detail::saveWithRetry(this,channel,[id,fields,reset,lockOnly,slot,panes,centerState,mode,locked,unsupported](QJsonObject& settings) {
        auto layouts=settings.value("layouts").toObject();
        auto layout=layouts.value(QString::number(slot)).toObject();
        // Never downgrade or overwrite a newer layout schema; a malformed one is replaced.
        if(!layout.isEmpty()&&newerVersion(layout)){*unsupported=true;return;}
        if(!layout.isEmpty()&&!currentVersion(layout))layout=QJsonObject{};
        layout["version"]=1;
        if(lockOnly)layout["locked"]=locked;
        else if(id=="_center") {
            auto center=layout.value("center").toObject();
            for(auto it=fields.begin();it!=fields.end();++it)center[it.key()]=it.value();
            layout["center"]=center;
        } else {
            auto stored=layout.value("panes").toObject();
            if(reset) {
                for(const auto& key:panes.keys()) {
                    auto entry=stored.value(key).toObject();const auto replacement=panes.value(key).toObject();
                    for(auto it=replacement.begin();it!=replacement.end();++it)entry[it.key()]=it.value();
                    stored[key]=entry;
                }
                layout["locked"]=false;
                auto center=layout.value("center").toObject();
                for(auto it=centerState.begin();it!=centerState.end();++it)center[it.key()]=it.value();
                center["mode"]=mode;
                layout["center"]=center;
            } else if(id=="_panes") { // several panes at once: {paneId: {field: value}}
                for(auto pane=fields.begin();pane!=fields.end();++pane) {
                    auto entry=stored.value(pane.key()).toObject();const auto changes=pane.value().toObject();
                    for(auto it=changes.begin();it!=changes.end();++it)entry[it.key()]=it.value();
                    stored[pane.key()]=entry;
                }
            } else {
                auto entry=stored.value(id).toObject();
                for(auto it=fields.begin();it!=fields.end();++it)entry[it.key()]=it.value();
                stored[id]=entry;
            }
            layout["panes"]=stored;
        }
        layouts[QString::number(slot)]=layout;settings["layouts"]=layouts;
    },[this,unsupported,completion,slot,directory](glancelot::SettingsStatus result){
        completion->done=true;
        if(completion->deferred&&(slot_!=slot||directory!=QDir::cleanPath(QFileInfo(glancelot::settingsDirectory()).absoluteFilePath())||feedback_!=savingFeedback())) {
            if(!result.ok())qWarning("Glancelot: a deferred layout save failed; newer view feedback was preserved");
            return;
        }
        if(completion->deferred&&result.ok()&&glancelot::settings_detail::hasPendingSettings(this,"layout/"+QString::number(slot)+"/"))return;
        if(*unsupported)feedback_=QCoreApplication::translate("Layout","Layout changed for this session; a newer saved layout was preserved.");
        else if(!result.ok())feedback_=QCoreApplication::translate("Layout","Layout changed for this session; settings could not be saved.");
        else feedback_.clear();
        if(completion->deferred)emit changed();
    });
    if(!completion->done){completion->deferred=true;feedback_=savingFeedback();}
}
bool PaneLayout::edit(const QString& id,const QJsonObject& fields,bool geometry) {
    if(!panes_.contains(id))return false;
    auto current=panes_.value(id).toObject();
    if(locked_||(geometry&&current.value("frozen").toBool())) {
        feedback_=locked_?"Unlock the layout before changing panes.":"Unfreeze this pane before changing its layout.";emit changed();return false;
    }
    auto next=current;for(auto it=fields.begin();it!=fields.end();++it)next[it.key()]=it.value();
    if(next==current)return true;
    panes_[id]=next;feedback_.clear();persist(id,fields);emit changed();return true;
}
bool PaneLayout::move(const QString& id,const QString& region,int order) {
    if(!regionValid(region)||order<0||order>99)return false;
    if(region=="center"&&id!="preview"&&id!="grid") {
        feedback_=QCoreApplication::translate("Layout","The center is reserved for image and grid content. Choose an edge for this pane.");
        emit changed();return false;
    }
    if(!panes_.contains(id))return false;
    if(locked_||panes_.value(id).toObject().value("frozen").toBool()) {
        feedback_=locked_?"Unlock the layout before changing panes.":"Unfreeze this pane before changing its layout.";emit changed();return false;
    }
    // Insert at `order` among the region's other panes, then renumber 0..n-1 so orders never tie and
    // "earlier/later" always has an effect when there is a neighbour.
    QStringList members;
    for(const auto& key:panes_.keys())
        if(key!=id&&panes_.value(key).toObject().value("region").toString()==region)members<<key;
    std::sort(members.begin(),members.end(),[&](const QString& a,const QString& b) {
        const int oa=panes_.value(a).toObject().value("order").toInt(),ob=panes_.value(b).toObject().value("order").toInt();
        return oa!=ob?oa<ob:stableOrder().indexOf(a)<stableOrder().indexOf(b);
    });
    members.insert(std::min<qsizetype>(order,members.size()),id);
    QJsonObject changes;
    for(int i=0;i<members.size();++i) {
        auto pane=panes_.value(members[i]).toObject();QJsonObject fields;
        if(members[i]==id&&pane.value("region").toString()!=region)fields["region"]=region;
        if(pane.value("order").toInt()!=i)fields["order"]=i;
        if(fields.isEmpty())continue;
        for(auto it=fields.begin();it!=fields.end();++it)pane[it.key()]=it.value();
        panes_[members[i]]=pane;changes[members[i]]=fields;
    }
    if(changes.isEmpty())return true;
    feedback_.clear();persist("_panes",changes);emit changed();return true;
}
bool PaneLayout::resize(const QString& id,double extent) {
    if(!std::isfinite(extent)||extent<32||extent>4096)return false;
    return edit(id,{{"extent",extent}},true);
}
bool PaneLayout::freeze(const QString& id,bool frozen){return edit(id,frozen?QJsonObject{{"frozen",true},{"visible",true}}:QJsonObject{{"frozen",false}},false);}
bool PaneLayout::setVisible(const QString& id,bool visible){return edit(id,{{"visible",visible}},true);}
void PaneLayout::setLocked(bool locked) {
    if(locked_==locked)return;locked_=locked;feedback_.clear();persist({}, {},false,true);emit changed();
}
void PaneLayout::reset(){panes_=defaults();center_={{"orientation","horizontal"},{"ratio",0.5}};mode_="preview";locked_=false;feedback_.clear();persist({}, {},true);emit changed();}

bool PaneLayout::editCenter(const QJsonObject& fields) {
    if(locked_||panes_.value("preview").toObject().value("frozen").toBool()||panes_.value("grid").toObject().value("frozen").toBool()) {
        feedback_=QCoreApplication::translate("Layout","Unlock the layout and unfreeze the center panes before changing the split.");emit changed();return false;
    }
    auto next=center_;for(auto it=fields.begin();it!=fields.end();++it)next[it.key()]=it.value();
    if(next==center_)return true;
    center_=next;feedback_.clear();persist("_center",fields);emit changed();return true;
}
bool PaneLayout::setCenterOrientation(const QString& orientation) {
    if(orientation!="horizontal"&&orientation!="vertical")return false;
    return editCenter({{"orientation",orientation}});
}
bool PaneLayout::setCenterRatio(double ratio) {
    if(!std::isfinite(ratio)||ratio<0.1||ratio>0.9)return false;
    return editCenter({{"ratio",ratio}});
}
QString PaneLayout::contentMode() const{return mode_;}
// A view preference, not geometry: allowed under the global lock and with frozen center panes.
bool PaneLayout::setContentMode(const QString& mode) {
    if(!modeValid(mode))return false;
    if(mode==mode_)return true;
    mode_=mode;persist("_center",{{"mode",mode}});emit changed();return true;
}

// ---- F049 layout presets
namespace {
constexpr int maxUserPresets=12;
bool presetNameValid(const QString& name){const auto trimmed=name.trimmed();return !trimmed.isEmpty()&&trimmed.size()<=40&&trimmed==name;}
}
QStringList PaneLayout::builtIns() const{return {QStringLiteral("Default"),QStringLiteral("Review"),QStringLiteral("Browse")};}
QJsonObject PaneLayout::userPresets() const {
    QJsonObject settings;
    if(!glancelot::readSettings(settings).ok())return {};
    const auto value=settings.value("layoutPresets");
    return value.isObject()?value.toObject():QJsonObject{};
}
QStringList PaneLayout::presetNames() const {
    auto names=builtIns();
    auto user=userPresets().keys();user.sort(Qt::CaseInsensitive);
    for(const auto& name:user)if(!names.contains(name,Qt::CaseInsensitive))names<<name;
    return names;
}
bool PaneLayout::isBuiltInPreset(const QString& name) const{return builtIns().contains(name,Qt::CaseInsensitive);}
// A stored or built-in preset as {panes, center{orientation,ratio,mode}}, validated field by field against the defaults.
QJsonObject PaneLayout::presetObject(const QString& name) const {
    QJsonObject panes=defaults();QJsonObject center{{"orientation","horizontal"},{"ratio",0.5},{"mode","preview"}};
    if(name.compare("Default",Qt::CaseInsensitive)==0){}
    else if(name.compare("Review",Qt::CaseInsensitive)==0) { // the photo, the filmstrip under it, nothing else
        auto set=[&](const char* id,const QJsonObject& fields){auto pane=panes.value(id).toObject();for(auto it=fields.begin();it!=fields.end();++it)pane[it.key()]=it.value();panes[id]=pane;};
        set("tree",{{"visible",false}});set("filmstrip",{{"region","bottom"},{"order",0},{"extent",112}});set("topbar",{{"visible",false}});
    } else if(name.compare("Browse",Qt::CaseInsensitive)==0) { // tree, grid and photo side by side
        center["mode"]="combined";
    } else {
        const auto stored=userPresets().value(name).toObject();
        if(stored.isEmpty()||!currentVersion(stored))return {};
        const auto savedPanes=stored.value("panes").toObject();
        for(const auto& id:panes.keys())panes[id]=validPane(savedPanes.value(id).toObject(),panes.value(id).toObject());
        const auto c=stored.value("center").toObject();
        if(c.value("orientation").toString()=="vertical")center["orientation"]="vertical";
        const double ratio=c.value("ratio").toDouble(-1);if(std::isfinite(ratio)&&ratio>=0.1&&ratio<=0.9)center["ratio"]=ratio;
        if(modeValid(c.value("mode").toString()))center["mode"]=c.value("mode");
    }
    for(const auto& id:panes.keys()){auto pane=panes.value(id).toObject();pane["frozen"]=false;panes[id]=pane;} // presets never carry a freeze
    return QJsonObject{{"version",1},{"panes",panes},{"center",center}};
}
bool PaneLayout::savePreset(const QString& name) {
    if(!presetNameValid(name)){feedback_=QCoreApplication::translate("Layout","Preset names are 1 to 40 characters with no leading or trailing space.");emit changed();return false;}
    if(isBuiltInPreset(name)){feedback_=QCoreApplication::translate("Layout","That name belongs to a built-in preset. Choose another.");emit changed();return false;}
    QJsonObject entry{{"version",1},{"panes",panes_},{"center",QJsonObject{{"orientation",center_.value("orientation")},{"ratio",center_.value("ratio")},{"mode",mode_}}}};
    bool tooMany=false;
    const auto result=glancelot::updateSettings([&](QJsonObject& settings){
        auto presets=settings.value("layoutPresets").toObject();
        if(!presets.contains(name)&&presets.size()>=maxUserPresets){tooMany=true;return;}
        presets[name]=entry;settings["layoutPresets"]=presets;
    });
    if(tooMany){feedback_=QCoreApplication::translate("Layout","You can keep up to 12 presets. Delete one first.");emit changed();return false;}
    if(!result.ok()){feedback_=QCoreApplication::translate("Layout","The preset could not be saved.");emit changed();return false;}
    feedback_.clear();emit presetsChanged();emit changed();return true;
}
bool PaneLayout::deletePreset(const QString& name) {
    if(isBuiltInPreset(name))return false;
    bool found=false;
    const auto result=glancelot::updateSettings([&](QJsonObject& settings){
        auto presets=settings.value("layoutPresets").toObject();
        if(!presets.contains(name))return;
        presets.remove(name);found=true;settings["layoutPresets"]=presets;
    });
    if(!found||!result.ok())return false;
    emit presetsChanged();return true;
}
void PaneLayout::persistWhole() {
    if(slot_==0)return;
    glancelot::updateSettings([&](QJsonObject& settings){
        auto layouts=settings.value("layouts").toObject();
        auto layout=layouts.value(QString::number(slot_)).toObject();
        if(!layout.isEmpty()&&newerVersion(layout))return;
        layout=QJsonObject{{"version",1},{"locked",locked_},{"panes",panes_},{"center",QJsonObject{{"orientation",center_.value("orientation")},{"ratio",center_.value("ratio")},{"mode",mode_}}}};
        layouts[QString::number(slot_)]=layout;settings["layouts"]=layouts;
    });
}
bool PaneLayout::applyPreset(const QString& name) {
    bool known=false;for(const auto& candidate:presetNames())if(candidate.compare(name,Qt::CaseInsensitive)==0)known=true;
    if(!known)return false;
    if(locked_){feedback_=QCoreApplication::translate("Layout","Unlock the layout before applying a preset.");emit changed();return false;}
    for(const auto& id:panes_.keys())if(panes_.value(id).toObject().value("frozen").toBool()){feedback_=QCoreApplication::translate("Layout","Unfreeze the panes before applying a preset.");emit changed();return false;}
    const auto preset=presetObject(name);
    if(preset.isEmpty()){feedback_=QCoreApplication::translate("Layout","That preset could not be read.");emit changed();return false;}
    panes_=preset.value("panes").toObject();
    const auto c=preset.value("center").toObject();
    center_={{"orientation",c.value("orientation")},{"ratio",c.value("ratio")}};
    mode_=c.value("mode").toString();
    feedback_.clear();persistWhole();emit changed();return true;
}
