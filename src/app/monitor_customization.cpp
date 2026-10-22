#include "glancelot/monitor_customization.hpp"
#include "glancelot/display_manager.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/settings_file.hpp"
#include "monitor_customization_state.hpp"
#include "monitor_customization_topology.hpp"
#include "settings_retry.hpp"
#include "startup_window_geometry.hpp"
#include "presentation_window.hpp"
#include "image_canvas.hpp"
#include <QGuiApplication>
#include <QJsonArray>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QUuid>
#include <QPainter>
#include <QSignalBlocker>
#include <array>

namespace glancelot {
namespace {
QRect rect(const QVariantMap& m) {return {m.value("x").toInt(),m.value("y").toInt(),m.value("width").toInt(),m.value("height").toInt()};}
QVariantMap mapRect(QRect r) {return {{"x",r.x()},{"y",r.y()},{"width",r.width()},{"height",r.height()}};}
QJsonObject mergeFields(QJsonObject current,const QJsonObject& patch) {
    for(auto it=patch.begin();it!=patch.end();++it) {
        if(it.value().isObject()&&current.value(it.key()).isObject())current[it.key()]=mergeFields(current.value(it.key()).toObject(),it.value().toObject());
        else current[it.key()]=it.value();
    }return current;
}
QScreen* screenFor(const QString& id) {
    for(auto* s:QGuiApplication::screens())if(DisplayManager::computeDisplayId(s)==id)return s;return nullptr;
}
bool canPosition() {return !QGuiApplication::platformName().startsWith("wayland");}
QRect reachable(QRect preferred,QScreen* target,QSize minimum={320,240}) {
    if(target&&!QGuiApplication::screens().contains(target))target=nullptr;
    if(!target)target=QGuiApplication::primaryScreen();
    std::array<QRect,64> areas{};qsizetype n=0;
    for(auto* s:QGuiApplication::screens()){if(n==64)break;areas[n++]=s->availableGeometry();}
    const auto work=target?target->availableGeometry():QRect{};
    // First make the requested target authoritative, then recover within its work area.
    if(target&&!work.intersects(preferred))preferred.moveTopLeft(work.topLeft()+QPoint(24,24));
    const auto span=target?std::span<const QRect>(&work,1):std::span<const QRect>(areas.data(),n);
    const auto result=startup::recoverStartupWindowGeometry(preferred,span,0,minimum).geometry;
    return work.isValid()?monitor_topology::clamp(result,work,minimum):result;
}
class IdentifyLabel final:public QQuickPaintedItem {
    QString label;
public:
    IdentifyLabel(QString value,QQuickItem* parent):QQuickPaintedItem(parent),label(std::move(value)){setSize({300,96});}
    void paint(QPainter* p) override {
        p->fillRect(boundingRect(),QColor("#171d28"));p->setPen(Qt::white);QFont f=p->font();f.setPixelSize(22);p->setFont(f);
        p->drawText(boundingRect().adjusted(12,8,-12,-8),Qt::AlignCenter|Qt::TextWordWrap,label);
    }
};
}

struct MonitorCustomization::Private {
    MonitorCustomization* q;
    DisplayManager* displays;
    PaneLayout* panes;
    QPointer<QQuickWindow> controller,outputWindow;
    QHash<QString,QPointer<QScreen>> boundScreens;
    QMetaObject::Connection outputConnection;
    QMetaObject::Connection themeConnection;
    QVariantMap data, persisted;
    QString feedback;
    int slot{};
    quint64 revision{};
    bool pending{},unsupported{},syncing{};
    QTimer identifyTimer;
    std::vector<std::unique_ptr<QQuickWindow>> labels;

    Private(MonitorCustomization* owner,DisplayManager* manager,PaneLayout* layout):q(owner),displays(manager),panes(layout) {
        data={{"output",monitor_state::options()},{"panes",monitor_state::routes()},{"presets",QVariantList{}},{"dock",panes->snapshot()}};
        identifyTimer.setSingleShot(true);identifyTimer.setInterval(3000);
        QObject::connect(&identifyTimer,&QTimer::timeout,q,[this]{labels.clear();});
    }
    bool fail(QString message) {feedback=std::move(message);emit q->changed();return false;}
    bool editable(const QString& id) {
        if(!monitor_state::paneIds.contains(id))return fail("Unknown pane.");
        if(panes->locked())return fail("Unlock the layout before moving panes.");
        if(data.value("panes").toMap().value(id).toMap().value("frozen").toBool()||panes->pane(id).value("frozen").toBool())return fail("Unfreeze the pane before moving it.");
        return true;
    }
    void applyOutput() {
        auto opts=data.value("output").toMap();displays->setRoutingPolicy(opts);
        if(opts.value("background")=="theme"&&controller)opts["backgroundColor"]=controller->color().name(QColor::HexRgb);
        if(!displays->isPresentationActive())return;
        const auto target=opts.value("displayId").toString();
        if(!target.isEmpty()&&!screenFor(target)){displays->closePresentation();feedback="The preferred display is unavailable; output was disabled.";return;}
        if(!target.isEmpty()&&screenFor(target)&&target!=displays->activeDisplayId())displays->openPresentation(target,displays->presentationMode());
        auto* window=qobject_cast<PresentationWindow*>(displays->presentationWindow());
        if(!window)return;
        if(outputWindow!=window) {
            QObject::disconnect(outputConnection);outputWindow=window;
            outputConnection=QObject::connect(window,&PresentationWindow::primaryViewportChanged,q,[this](const QVariantMap& state){
                if(syncing||data.value("output").toMap().value("viewportLink")!="linked"||!data.value("output").toMap().value("followSelection").toBool()||displays->isPinned())return;
                syncing=true;emit q->controllerViewportRequested(state,displays->currentGeneration());syncing=false;
            });
        }
        syncing=true;window->applyCustomization(opts);syncing=false;
    }
    bool commit(QVariantMap next,QString routing={},std::function<void()> committed={}) {
        if(pending)return fail("A preference save is still pending. Try again when it completes.");
        if(unsupported)return fail("A newer customization version is preserved; these preferences cannot be edited.");
        if(slot<1)return fail("No window slot is available.");
        if(!monitor_state::slot(next))return fail("Invalid monitor preferences.");
        const auto expected=monitor_state::known(persisted);
        const auto owned=monitor_state::known(next);
        const auto currentDock=panes->snapshot();
        const bool dockChanged=next.value("dock").toMap()!=currentDock;
        const auto number=QString::number(slot);const auto capturedSlot=slot;const auto capturedRevision=++revision;
        auto applied=std::make_shared<bool>(false);auto refused=std::make_shared<QString>();auto written=std::make_shared<QVariantMap>();
        pending=true;feedback="Saving monitor preferences…";
        const bool accepted=settings_detail::saveWithRetry(q,"monitor-customization/"+number,
            [number,expected,owned,dockChanged,applied,refused,written](QJsonObject& root) {
                *applied=false;auto feature=root.value("monitorCustomization").toObject();
                if(feature.contains("version")&&feature.value("version")!=QJsonValue(1)){*refused="A newer customization version was preserved.";return;}
                auto slotRows=feature.value("slots").toObject();auto old=slotRows.value(number).toObject();
                if(monitor_state::known(old.toVariantMap())!=expected){*refused="This slot changed in another window. Reload it before retrying.";return;}
                for(auto it=owned.begin();it!=owned.end();++it) {
                    const auto value=QJsonValue::fromVariant(it.value());
                    if(it.key()=="presets") {
                        QJsonArray rows;const auto before=old.value("presets").toArray();
                        for(const auto& entry:value.toArray()){auto record=entry.toObject();for(const auto& prior:before)if(prior.toObject().value("id")==record.value("id")){record=mergeFields(prior.toObject(),record);break;}rows.append(record);}old[it.key()]=rows;
                    }else if(it.key()=="outputsByDisplay") {
                        auto before=old.value(it.key()).toObject();const auto incoming=value.toObject();
                        for(const auto& id:before.keys())if(!incoming.contains(id)){auto remaining=before.value(id).toObject();for(const auto& field:monitor_state::options().keys())remaining.remove(field);if(remaining.isEmpty())before.remove(id);else before[id]=remaining;}
                        old[it.key()]=mergeFields(before,incoming);
                    }
                    else if(value.isObject())old[it.key()]=mergeFields(old.value(it.key()).toObject(),value.toObject());
                    else old[it.key()]=value;
                }
                if(!owned.contains("selectedPreset"))old.remove("selectedPreset");
                slotRows[number]=old;feature["slots"]=slotRows;feature["version"]=1;
                if(QJsonDocument(feature).toJson(QJsonDocument::Indented).size()>256*1024){*refused="Monitor preferences exceed the 256 KiB limit.";return;}
                auto candidate=root;candidate["monitorCustomization"]=feature;
                if(dockChanged){auto layouts=candidate.value("layouts").toObject();auto existing=layouts.value(number).toObject();
                    if(existing.contains("version")&&existing.value("version")!=QJsonValue(1)){*refused="A newer pane layout was preserved.";return;}
                    const auto dock=QJsonObject::fromVariantMap(owned.value("dock").toMap());for(auto it=dock.begin();it!=dock.end();++it)existing[it.key()]=it.value();layouts[number]=existing;candidate["layouts"]=layouts;}
                if(QJsonDocument(candidate).toJson(QJsonDocument::Indented).size()>1024*1024){*refused="Settings exceed the 1 MiB limit.";return;}
                root=candidate;*written=old.toVariantMap();*applied=true;
            },[this,next,routing,committed,capturedSlot,capturedRevision,applied,refused,written,dockChanged](SettingsStatus status){
                if(slot!=capturedSlot||revision!=capturedRevision)return;
                pending=false;
                if(!status.ok()||!*applied){feedback=refused->isEmpty()?"Monitor preferences could not be saved; previous state is unchanged.":*refused;emit q->changed();return;}
                data=next;persisted=*written;
                QSignalBlocker blocked(panes);
                if(dockChanged)panes->restoreSnapshot(data.value("dock").toMap());
                if(!routing.isEmpty())displays->setPresentationMode(routing);
                feedback.clear();if(committed)committed();applyOutput();blocked.unblock();if(dockChanged)emit panes->changed();emit q->changed();
            });
        if(!accepted&&pending){pending=false;feedback="Monitor preferences could not be queued.";}
        emit q->changed();return accepted&&(pending||*applied);
    }
};

MonitorCustomization::MonitorCustomization(DisplayManager* manager,PaneLayout* panes,QObject* parent)
    :QObject(parent),d(std::make_unique<Private>(this,manager,panes)) {
    connect(manager,&DisplayManager::presentationActiveChanged,this,[this]{d->applyOutput();emit changed();});
    connect(manager,&DisplayManager::displaysChanged,this,[this]{
        if(d->controller)d->controller->setGeometry(reachable(d->controller->geometry(),d->controller->screen(),d->controller->minimumSize().expandedTo({320,240})));
        emit changed();
    });
    connect(panes,&PaneLayout::changed,this,[this]{emit changed();});
}
MonitorCustomization::~MonitorCustomization()=default;
void MonitorCustomization::setControllerWindow(QQuickWindow* window){
    QObject::disconnect(d->themeConnection);d->controller=window;
    if(window)d->themeConnection=connect(window,&QQuickWindow::colorChanged,this,[this]{d->applyOutput();});
    d->applyOutput();
}
void MonitorCustomization::reloadPreferences() {
    const auto slot=d->slot;if(slot<1)return;
    settings_detail::cancelPendingSettings(this,"monitor-customization/"+QString::number(slot));
    settings_detail::cancelPendingSettings(d->panes,"layout/"+QString::number(slot)+"/");
    {QSignalBlocker blocked(d->panes);d->panes->setSlot(0);d->panes->setSlot(slot);}
    d->slot=0;setSlot(slot);
    emit d->panes->changed();
    emit changed();
}
void MonitorCustomization::setSlot(int slot) {
    if(slot<1||slot>1000000||slot==d->slot)return;
    ++d->revision;d->slot=slot;d->pending=false;d->unsupported=false;
    QJsonObject root;const auto status=readSettings(root);
    const auto feature=root.value("monitorCustomization").toObject();
    const auto saved=feature.value("slots").toObject().value(QString::number(slot)).toObject().toVariantMap();
    d->persisted=saved;d->data={{"output",monitor_state::options()},{"panes",monitor_state::routes()},{"presets",QVariantList{}},{"dock",d->panes->snapshot()}};
    d->unsupported=feature.contains("version")&&feature.value("version")!=QJsonValue(1);
    d->feedback.clear();
    if(!status.ok())d->feedback="Monitor preferences could not be read; safe defaults are active.";
    else if(d->unsupported)d->feedback="A newer customization version was preserved; safe defaults are active.";
    else if(!saved.isEmpty()) {
        auto raw=d->data;for(auto it=saved.begin();it!=saved.end();++it)raw[it.key()]=it.value();QVariantMap next;
        if(monitor_state::normalize(raw,next))d->data=next;else d->feedback="Invalid monitor preferences were preserved; safe defaults are active.";
    }
    // Loading preferences never opens an output or creates detached windows.
    auto routes=d->data.value("panes").toMap();
    for(const auto& id:monitor_state::paneIds){auto route=routes[id].toMap();route["detached"]=false;routes[id]=route;}
    d->data["panes"]=routes;d->applyOutput();emit changed();
}
QVariantMap MonitorCustomization::outputOptions() const{return d->data.value("output").toMap();}
QVariantMap MonitorCustomization::paneRoutes() const {
    auto routes=d->data.value("panes").toMap();
    for(const auto& id:monitor_state::paneIds){auto route=routes[id].toMap();auto* screen=screenFor(route.value("displayId").toString());
        auto* controllerScreen=d->controller?d->controller->screen():QGuiApplication::primaryScreen();
        const auto placement=monitor_topology::place(route.value("displayId").toString(),rect(route.value("geometry").toMap()),d->displays->displays(),controllerScreen?DisplayManager::computeDisplayId(controllerScreen):QString{},screen&&d->boundScreens.value(id)==screen);
        route["effectiveDisplayId"]=placement.displayId;route["recovered"]=placement.recovered&&route.value("detached").toBool();
        route["effectiveGeometry"]=mapRect(placement.geometry);
        route["frozen"]=route.value("frozen").toBool()||d->panes->pane(id).value("frozen").toBool();routes[id]=route;
    }return routes;
}
QVariantList MonitorCustomization::presets() const{return d->data.value("presets").toList();}
bool MonitorCustomization::layoutLocked() const{return d->panes->locked();}
QString MonitorCustomization::feedback() const{return d->feedback;}
void MonitorCustomization::setLayoutLocked(bool locked){auto next=d->data;auto dock=d->panes->snapshot();dock["locked"]=locked;next["dock"]=dock;d->commit(next);}
bool MonitorCustomization::setOutputOptions(const QVariantMap& patch) {
    auto opts=outputOptions();auto saved=d->data.value("outputsByDisplay").toMap();
    const auto previousId=opts.value("displayId").toString();
    const auto nextId=patch.value("displayId",previousId).toString();
    if(nextId!=previousId) {
        if(!previousId.isEmpty())saved[previousId]=opts;
        opts=saved.contains(nextId)?saved.value(nextId).toMap():monitor_state::options();
    }
    if(!monitor_state::patchOptions(patch,opts))return d->fail("Invalid presentation option; nothing changed.");
    if(opts.value("viewportLink")=="linked"&&(!opts.value("followSelection").toBool()||d->displays->isPinned()))return d->fail("Linked controller viewports require an output that follows the current image.");
    if(patch.contains("displayId")&&!screenFor(opts.value("displayId").toString()))return d->fail("That display is unavailable.");
    if(patch.contains("displayId")&&!canPosition())return d->fail("This compositor does not support explicit window placement.");
    if(!nextId.isEmpty())saved[nextId]=opts;
    if(saved.size()>16)return d->fail("At most16 display preferences can be remembered. Reset monitor preferences before adding another.");
    auto next=d->data;next["output"]=opts;next["outputsByDisplay"]=saved;next["dock"]=d->panes->snapshot();return d->commit(next);
}
bool MonitorCustomization::syncControllerViewport(const QVariantMap& state,quint64 generation) {
    if(d->syncing||generation!=d->displays->currentGeneration()||outputOptions().value("viewportLink")!="linked"||d->displays->isPinned()||!outputOptions().value("followSelection").toBool())return false;
    const auto fit=state.value("fit").toString();
    if(state.contains("rotation")&&(!monitor_state::integer(state.value("rotation"),0,270)||state.value("rotation").toInt()%90))return false;
    if(!fit.isEmpty()&&!QStringList{"shrink","fit","fill","width","height","actual"}.contains(fit))return false;
    for(const auto& key:QStringList{"zoom","centerX","centerY","sourceWidth","sourceHeight","rotation"})if(state.contains(key)){
        bool ok=false;const auto number=state.value(key).toDouble(&ok);if(!ok||!std::isfinite(number)||std::abs(number)>1e9)return false;
    }
    if(fit.isEmpty()&&(!state.contains("zoom")||state.value("zoom").toDouble()<.001||state.value("zoom").toDouble()>64||!state.contains("centerX")||!state.contains("centerY")))return false;
    auto* window=qobject_cast<PresentationWindow*>(d->displays->presentationWindow());if(!window)return false;
    d->syncing=true;window->primaryCanvas()->setViewState(state);d->syncing=false;return true;
}
bool MonitorCustomization::identifyDisplays() {
    const auto screens=QGuiApplication::screens();if(screens.isEmpty()||screens.size()>64)return d->fail("Display identification is unavailable for this topology.");
    d->labels.clear();int number=0;
    for(auto* screen:screens){auto label=std::make_unique<QQuickWindow>();label->setFlags(Qt::ToolTip|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);label->setScreen(screen);
        label->setGeometry(QRect(screen->availableGeometry().center()-QPoint(150,48),QSize(300,96)));new IdentifyLabel(QString("%1 — %2").arg(++number).arg(screen->name()),label->contentItem());label->show();d->labels.push_back(std::move(label));}
    d->identifyTimer.start();return true;
}
bool MonitorCustomization::moveOutput(const QString& id) {
    if(!d->displays->isPresentationActive())return d->fail("Enable presentation before moving its window.");
    return setOutputOptions({{"displayId",id}});
}
bool MonitorCustomization::swapControllerAndOutput() {
    if(!canPosition())return d->fail("This compositor does not support swapping windows.");
    auto* target=screenFor(d->displays->activeDisplayId());
    if(!d->controller||!d->displays->isPresentationActive()||!target||target==d->controller->screen())return d->fail("An active output on a different display is required.");
    auto* old=d->controller->screen();const auto oldId=DisplayManager::computeDisplayId(old);
    const auto geometry=reachable(d->controller->geometry(),target,d->controller->minimumSize().expandedTo({320,240}));
    auto next=d->data;auto opts=outputOptions();opts["displayId"]=oldId;next["output"]=opts;
    const QPointer<QScreen> destination=target;
    return d->commit(next,{},[this,destination,geometry]{if(destination&&d->controller){d->controller->setScreen(destination);d->controller->setGeometry(geometry);}});
}
bool MonitorCustomization::detachPane(const QString& id,const QString& displayId) {
    if(!d->editable(id))return false;
    if(!canPosition()||!screenFor(displayId))return d->fail("That display cannot host a detached pane.");
    auto next=d->data;auto routes=next.value("panes").toMap();auto route=routes[id].toMap();route["detached"]=true;route["displayId"]=displayId;routes[id]=route;next["panes"]=routes;next["dock"]=d->panes->snapshot();
    const QPointer<QScreen> selected=screenFor(displayId);return d->commit(next,{},[this,id,selected]{d->boundScreens[id]=selected;});
}
bool MonitorCustomization::redockPane(const QString& id) {
    if(!d->editable(id))return false;auto next=d->data;auto routes=next.value("panes").toMap();auto route=routes[id].toMap();route["detached"]=false;routes[id]=route;next["panes"]=routes;next["dock"]=d->panes->snapshot();return d->commit(next);
}
bool MonitorCustomization::setPaneFrozen(const QString& id,bool frozen) {
    if(!monitor_state::paneIds.contains(id))return d->fail("Unknown pane.");
    if(layoutLocked())return d->fail("Unlock the layout before changing pane freeze.");
    auto next=d->data;auto routes=next.value("panes").toMap();auto route=routes[id].toMap();route["frozen"]=frozen;routes[id]=route;next["panes"]=routes;
    auto dock=d->panes->snapshot();auto panes=dock.value("panes").toMap();if(panes.contains(id)){auto p=panes[id].toMap();p["frozen"]=frozen;panes[id]=p;dock["panes"]=panes;}next["dock"]=dock;return d->commit(next);
}
bool MonitorCustomization::updatePaneGeometry(const QString& id,const QVariantMap& geometry) {
    if(!d->editable(id))return false;if(!monitor_state::geometry(geometry))return d->fail("Invalid pane geometry.");
    auto next=d->data;auto routes=next.value("panes").toMap();auto route=routes[id].toMap();route["geometry"]=geometry;routes[id]=route;next["panes"]=routes;next["dock"]=d->panes->snapshot();return d->commit(next);
}
bool MonitorCustomization::savePreset(const QString& id,const QString& name,const QString& kind) {
    if(!QStringList{"presentation","workspace"}.contains(kind))return d->fail("Choose a presentation or workspace preset.");
    auto rows=presets();int index=-1;for(int i=0;i<rows.size();++i)if(rows[i].toMap().value("id")==id)index=i;
    if(!id.isEmpty()&&index<0)return d->fail("That preset no longer exists.");
    QVariantMap snapshot{{"output",outputOptions()},{"routingMode",d->displays->presentationModeString()}};
    if(kind=="workspace"){snapshot["panes"]=d->data.value("panes");snapshot["dock"]=d->panes->snapshot();}
    QVariantMap record=index<0?QVariantMap{}:rows[index].toMap();record["id"]=id.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):id;record["name"]=name.trimmed();record["kind"]=kind;record["snapshot"]=snapshot;
    if(index<0)rows.append(record);else rows[index]=record;
    if(!monitor_state::presets(rows))return d->fail("Preset names must be unique, nonempty and at most80 characters; at most64 presets are allowed.");
    auto next=d->data;next["presets"]=rows;next["dock"]=d->panes->snapshot();return d->commit(next);
}
bool MonitorCustomization::renamePreset(const QString& id,const QString& name) {
    auto rows=presets();bool found=false;for(auto& v:rows){auto r=v.toMap();if(r.value("id")==id){r["name"]=name.trimmed();v=r;found=true;}}
    if(!found||!monitor_state::presets(rows))return d->fail("Choose an existing preset and a unique valid name.");auto next=d->data;next["presets"]=rows;next["dock"]=d->panes->snapshot();return d->commit(next);
}
bool MonitorCustomization::deletePreset(const QString& id) {
    auto rows=presets();bool found=false;for(int i=0;i<rows.size();++i)if(rows[i].toMap().value("id")==id){rows.removeAt(i);found=true;break;}
    if(!found)return d->fail("That preset no longer exists.");auto next=d->data;next["presets"]=rows;if(next.value("selectedPreset")==id)next.remove("selectedPreset");next["dock"]=d->panes->snapshot();return d->commit(next);
}
bool MonitorCustomization::applyPreset(const QString& id) {
    QVariantMap record;for(const auto& v:presets())if(v.toMap().value("id")==id)record=v.toMap();
    if(record.isEmpty())return d->fail("That preset no longer exists.");const auto kind=record.value("kind").toString();const auto snapshot=record.value("snapshot").toMap();
    if(!monitor_state::snapshot(snapshot,kind))return d->fail("This preset is invalid.");
    if(kind=="workspace"&&(layoutLocked()||std::any_of(monitor_state::paneIds.begin(),monitor_state::paneIds.end(),[this](const QString& pane){return paneRoutes().value(pane).toMap().value("frozen").toBool();})))return d->fail("Unlock the layout and unfreeze panes before applying a workspace preset.");
    auto next=d->data;next["output"]=snapshot.value("output");next["dock"]=d->panes->snapshot();
    if(kind=="workspace"){next["panes"]=snapshot.value("panes");next["dock"]=snapshot.value("dock");}
    next["selectedPreset"]=id;return d->commit(next,snapshot.value("routingMode").toString(),[this,kind]{if(kind=="workspace")for(const auto& pane:monitor_state::paneIds)d->boundScreens[pane]=screenFor(d->data.value("panes").toMap().value(pane).toMap().value("displayId").toString());});
}
bool MonitorCustomization::resetPreferences() {
    auto next=d->data;next["output"]=monitor_state::options();next["panes"]=monitor_state::routes();next["presets"]=QVariantList{};next["outputsByDisplay"]=QVariantMap{};next.remove("selectedPreset");
    auto dock=d->panes->snapshot();dock["locked"]=false;auto panes=dock.value("panes").toMap();for(auto it=panes.begin();it!=panes.end();++it){auto p=it.value().toMap();p["frozen"]=false;it.value()=p;}dock["panes"]=panes;next["dock"]=dock;return d->commit(next,"mirror");
}
void MonitorCustomization::recoverToController() {
    d->displays->closePresentation();auto routes=d->data.value("panes").toMap();
    for(const auto& id:monitor_state::paneIds){auto route=routes[id].toMap();route["detached"]=false;routes[id]=route;}
    // Emergency access is immediate even if disk is busy or a preference version is newer.
    d->data["panes"]=routes;if(d->controller)d->controller->setGeometry(reachable(d->controller->geometry(),QGuiApplication::primaryScreen(),d->controller->minimumSize().expandedTo({320,240})));
    d->feedback="Presentation closed; panes returned to the controller. Lock preferences were retained.";emit changed();
}
} // namespace glancelot
