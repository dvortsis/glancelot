#include "glancelot/pane_layout.hpp"
#include "glancelot/settings_file.hpp"
#include <QJsonArray>
#include <cmath>

namespace {
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
    QJsonObject result;
    const auto add=[&](const char* id,const char* region,int order,int extent,bool visible) {
        result[id]=QJsonObject{{"region",region},{"order",order},{"extent",extent},{"visible",visible},{"frozen",false}};
    };
    add("tree","left",0,280,true);add("preview","center",0,640,true);add("grid","center",1,640,true);
    add("filmstrip","top",1,112,true);add("topbar","top",0,48,true);add("statusbar","bottom",0,48,true);
    return result;
}
PaneLayout::PaneLayout(QObject* parent):QObject(parent),panes_(defaults()) {}
QVariantMap PaneLayout::panes() const{return panes_.toVariantMap();}
QVariantMap PaneLayout::pane(const QString& id) const{return panes_.value(id).toObject().toVariantMap();}
void PaneLayout::setSlot(int slot) {if(slot<0||slot==slot_)return;slot_=slot;load();}
void PaneLayout::load() {
    panes_=defaults();locked_=false;feedback_.clear();
    if(slot_>0) {
        QJsonObject settings;
        if(!glancelot::readSettings(settings).ok())feedback_="Layout settings could not be read; defaults are active.";
        else {
            const auto layoutsValue=settings.value("layouts");
            const auto savedValue=layoutsValue.toObject().value(QString::number(slot_));
            const auto saved=savedValue.toObject();
            if((!layoutsValue.isUndefined()&&!layoutsValue.isObject())||(!savedValue.isUndefined()&&!savedValue.isObject()))
                feedback_="Layout settings are invalid; defaults are active.";
            if(!saved.isEmpty()&&saved.value("version").toInt()!=1)feedback_="Unsupported layout version; defaults are active.";
            else {
                locked_=saved.value("locked").toBool(false);
                const auto stored=saved.value("panes").toObject();
                for(const auto& id:panes_.keys())panes_[id]=validPane(stored.value(id).toObject(),panes_.value(id).toObject());
            }
        }
    }
    emit changed();
}
void PaneLayout::persist(const QString& id,const QJsonObject& fields,bool reset,bool lockOnly) {
    if(slot_==0)return;
    bool unsupported=false;
    const auto result=glancelot::updateSettings([&](QJsonObject& settings) {
        auto layouts=settings.value("layouts").toObject();
        auto layout=layouts.value(QString::number(slot_)).toObject();
        // Never downgrade or overwrite a newer layout schema.
        if(!layout.isEmpty()&&layout.value("version").toInt()!=1){unsupported=true;return;}
        layout["version"]=1;
        if(lockOnly)layout["locked"]=locked_;
        else {
            auto stored=layout.value("panes").toObject();
            if(reset) {
                for(const auto& key:panes_.keys()) {
                    auto entry=stored.value(key).toObject();const auto replacement=panes_.value(key).toObject();
                    for(auto it=replacement.begin();it!=replacement.end();++it)entry[it.key()]=it.value();
                    stored[key]=entry;
                }
                layout["locked"]=false;
            } else {
                auto entry=stored.value(id).toObject();
                for(auto it=fields.begin();it!=fields.end();++it)entry[it.key()]=it.value();
                stored[id]=entry;
            }
            layout["panes"]=stored;
        }
        layouts[QString::number(slot_)]=layout;settings["layouts"]=layouts;
    });
    if(unsupported)feedback_="Layout changed for this session; a newer saved layout was preserved.";
    else if(!result.ok())feedback_="Layout changed for this session; settings could not be saved.";
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
    return edit(id,{{"region",region},{"order",order}},true);
}
bool PaneLayout::resize(const QString& id,double extent) {
    if(!std::isfinite(extent)||extent<32||extent>4096)return false;
    return edit(id,{{"extent",extent}},true);
}
bool PaneLayout::freeze(const QString& id,bool frozen){return edit(id,{{"frozen",frozen}},false);}
bool PaneLayout::setVisible(const QString& id,bool visible){return edit(id,{{"visible",visible}},true);}
void PaneLayout::setLocked(bool locked) {
    if(locked_==locked)return;locked_=locked;feedback_.clear();persist({}, {},false,true);emit changed();
}
void PaneLayout::reset(){panes_=defaults();locked_=false;feedback_.clear();persist({}, {},true);emit changed();}
