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
#include <optional>

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
    QPointer<QScreen> sessionScreen;
    QVariantMap lastAppliedOptions;
    bool movingOutput{};
    QHash<QString,QPointer<QScreen>> boundScreens;
    QMetaObject::Connection outputConnection,commandConnection;
    QMetaObject::Connection themeConnection,controllerScreenConnection;
    QVariantMap data, persisted;
    mutable QVariantMap routeCache;
    QString feedback;
    int slot{};
    quint64 revision{};
    bool pending{},unsupported{},invalid{},syncing{};
    QTimer identifyTimer;
    std::vector<std::unique_ptr<QQuickWindow>> labels;

    Private(MonitorCustomization* owner,DisplayManager* manager,PaneLayout* layout):q(owner),displays(manager),panes(layout) {
        data={{"output",monitor_state::options()},{"panes",monitor_state::routes()},{"presets",QVariantList{}},{"dock",panes->snapshot()}};
        identifyTimer.setSingleShot(true);identifyTimer.setInterval(3000);
        QObject::connect(&identifyTimer,&QTimer::timeout,q,[this]{labels.clear();});
    }
    void notify(){routeCache.clear();emit q->changed();}
    bool fail(QString message) {feedback=std::move(message);notify();return false;}
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
        auto* window=qobject_cast<PresentationWindow*>(displays->presentationWindow());if(!window)return;
        const auto live=window->viewportState();const auto liveGeometry=window->geometry();
        const bool preserveViewport=!lastAppliedOptions.isEmpty()&&lastAppliedOptions.value("fitMode")==opts.value("fitMode");
        auto* target=sessionScreen.data();
        if(!target||!QGuiApplication::screens().contains(target))target=window->screen();
        const auto preferred=opts.value("displayId").toString();
        if(!preferred.isEmpty()&&!screenFor(preferred)&&feedback.isEmpty())feedback="Preferred display unavailable; the explicitly enabled session uses an available display.";
        const bool moved=target&&DisplayManager::computeDisplayId(target)!=displays->activeDisplayId();
        if(moved){movingOutput=true;displays->openPresentation(DisplayManager::computeDisplayId(target),displays->presentationMode());movingOutput=false;}
        if(outputWindow!=window) {
            QObject::disconnect(outputConnection);QObject::disconnect(commandConnection);outputWindow=window;
            outputConnection=QObject::connect(window,&PresentationWindow::primaryViewportChanged,q,[this](const QVariantMap& state){
                if(syncing||data.value("output").toMap().value("viewportLink")!="linked"||!data.value("output").toMap().value("followSelection").toBool()||displays->isPinned()||displays->outputGeneration()!=displays->currentGeneration())return;
                syncing=true;emit q->controllerViewportRequested(state,displays->outputGeneration());syncing=false;
            });
            commandConnection=QObject::connect(window,&PresentationWindow::customizationCommand,q,&MonitorCustomization::commandRequested);
        }
        syncing=true;window->applyCustomization(opts);if(preserveViewport)window->restoreViewport(live);
        if(moved&&opts.value("windowMode")=="windowed")window->setGeometry(reachable(liveGeometry,target));
        lastAppliedOptions=opts;syncing=false;
    }
    bool commit(QVariantMap next,QString routing={},std::function<void()> committed={},std::optional<QVariantMap> dockOverride={},bool resetInvalid=false) {
        if(pending)return fail("A preference save is still pending. Try again when it completes.");
        if(unsupported)return fail("A newer customization version is preserved; these preferences cannot be edited.");
        if(invalid&&!resetInvalid)return fail("Invalid preferences were preserved. Reset monitor preferences explicitly before editing them.");
        if(slot<1)return fail("No window slot is available.");
        const auto currentDock=panes->snapshot();const auto plannedDock=dockOverride.value_or(currentDock);
        if(!monitor_state::slot(next)||!monitor_state::dock(plannedDock))return fail("Invalid monitor preferences.");
        next["dock"]=plannedDock; // runtime transition only; layouts[slot] is the persistence owner
        const auto expected=monitor_state::known(persisted);
        const auto owned=monitor_state::known(next);
        const bool dockChanged=plannedDock!=currentDock;
        QJsonValue expectedLayout;
        if(dockChanged){QJsonObject root;if(!readSettings(root).ok())return fail("Layout settings could not be read.");expectedLayout=root.value("layouts").toObject().value(QString::number(slot));}
        const QPointer<QQuickWindow> expectedWindow=displays->presentationWindow();
        const auto number=QString::number(slot);const auto capturedSlot=slot;const auto capturedRevision=++revision;
        auto applied=std::make_shared<bool>(false);auto refused=std::make_shared<QString>();auto written=std::make_shared<QVariantMap>();auto writtenDock=std::make_shared<QVariantMap>(plannedDock);
        pending=true;feedback="Saving monitor preferences…";
        const bool accepted=settings_detail::saveWithRetry(q,"monitor-customization/"+number,
            [number,expected,owned,dockChanged,plannedDock,expectedLayout,applied,refused,written,writtenDock](QJsonObject& root) {
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
                    if(layouts.value(number)!=expectedLayout){*refused="The pane layout changed while saving. Retry with its current state.";return;}
                    if(existing.contains("version")&&existing.value("version")!=QJsonValue(1)){*refused="A newer pane layout was preserved.";return;}
                    const auto dock=monitor_state::ownedDock(plannedDock);
                    for(const auto& key:QStringList{"version","locked"})existing[key]=dock.value(key);
                    for(const auto& section:QStringList{"center","panes"})existing[section]=mergeFields(existing.value(section).toObject(),dock.value(section).toObject());layouts[number]=existing;candidate["layouts"]=layouts;*writtenDock=existing.toVariantMap();}
                if(QJsonDocument(candidate).toJson(QJsonDocument::Indented).size()>1024*1024){*refused="Settings exceed the 1 MiB limit.";return;}
                root=candidate;*written=old.toVariantMap();*applied=true;
            },[this,next,routing,committed,expectedWindow,capturedSlot,capturedRevision,applied,refused,written,writtenDock,dockChanged](SettingsStatus status){
                if(slot!=capturedSlot||revision!=capturedRevision)return;
                pending=false;
                if(!status.ok()||!*applied){feedback=refused->isEmpty()?"Monitor preferences could not be saved; previous state is unchanged.":*refused;notify();return;}
                data=next;data["dock"]=*writtenDock;persisted=*written;invalid=false;
                QSignalBlocker blocked(panes);
                if(dockChanged){settings_detail::cancelPendingSettings(panes,"layout/"+QString::number(slot)+"/");panes->restoreSnapshot(data.value("dock").toMap());}
                feedback.clear();if(committed)committed();
                if(expectedWindow==displays->presentationWindow()){if(!routing.isEmpty())displays->setPresentationMode(routing);applyOutput();}
                else feedback="Preferences saved; the newer presentation session was left unchanged.";
                blocked.unblock();if(dockChanged)emit panes->changed();notify();
            });
        if(!accepted&&pending){pending=false;feedback="Monitor preferences could not be queued.";}
        notify();return accepted&&(pending||*applied);
    }
};

MonitorCustomization::MonitorCustomization(DisplayManager* manager,PaneLayout* panes,QObject* parent)
    :QObject(parent),d(std::make_unique<Private>(this,manager,panes)) {
    connect(manager,&DisplayManager::presentationActiveChanged,this,[this](bool active){
        if(active&&!d->movingOutput)d->sessionScreen=d->displays->presentationWindow()?d->displays->presentationWindow()->screen():screenFor(d->displays->activeDisplayId());
        if(!active)d->sessionScreen=nullptr;
        d->applyOutput();d->notify();
    });
    connect(manager,&DisplayManager::routingStatusChanged,this,[this]{d->notify();});
    connect(manager,&DisplayManager::displaysChanged,this,[this]{
        if(d->controller&&d->controller->visibility()!=QWindow::FullScreen&&d->controller->visibility()!=QWindow::Maximized&&d->controller->visibility()!=QWindow::Minimized)d->controller->setGeometry(reachable(d->controller->geometry(),d->controller->screen(),d->controller->minimumSize().expandedTo({320,240})));
        d->notify();
    });
    connect(panes,&PaneLayout::changed,this,[this]{d->notify();});
}
MonitorCustomization::~MonitorCustomization()=default;
void MonitorCustomization::setControllerWindow(QQuickWindow* window){
    QObject::disconnect(d->themeConnection);QObject::disconnect(d->controllerScreenConnection);d->controller=window;
    if(window)d->themeConnection=connect(window,&QQuickWindow::colorChanged,this,[this]{d->applyOutput();});
    if(window)d->controllerScreenConnection=connect(window,&QWindow::screenChanged,this,[this]{d->notify();});
    d->applyOutput();d->notify();
}
void MonitorCustomization::reloadPreferences() {
    const auto slot=d->slot;if(slot<1)return;
    settings_detail::cancelPendingSettings(this,"monitor-customization/"+QString::number(slot));
    settings_detail::cancelPendingSettings(d->panes,"layout/"+QString::number(slot)+"/");
    {QSignalBlocker blocked(d->panes);d->panes->setSlot(0);d->panes->setSlot(slot);}
    d->slot=0;setSlot(slot);
    emit d->panes->changed();
    d->notify();
}
void MonitorCustomization::setSlot(int slot) {
    if(slot<1||slot>1000000||slot==d->slot)return;
    ++d->revision;d->slot=slot;d->pending=false;d->unsupported=false;d->invalid=false;
    QJsonObject root;const auto status=readSettings(root);
    const auto feature=root.value("monitorCustomization").toObject();
    const auto saved=feature.value("slots").toObject().value(QString::number(slot)).toObject().toVariantMap();
    d->persisted=saved;d->data={{"output",monitor_state::options()},{"panes",monitor_state::routes()},{"presets",QVariantList{}},{"dock",d->panes->snapshot()}};
    d->unsupported=feature.contains("version")&&feature.value("version")!=QJsonValue(1);
    d->feedback.clear();
    if(root.contains("monitorCustomization")&&!root.value("monitorCustomization").isObject())d->invalid=true;
    if(feature.contains("slots")&&!feature.value("slots").isObject())d->invalid=true;
    if(feature.value("slots").toObject().contains(QString::number(slot))&&!feature.value("slots").toObject().value(QString::number(slot)).isObject())d->invalid=true;
    if(d->invalid)d->feedback="Invalid monitor preferences were preserved; reset them explicitly before editing.";
    else if(!status.ok())d->feedback="Monitor preferences could not be read; safe defaults are active.";
    else if(d->unsupported)d->feedback="A newer customization version was preserved; safe defaults are active.";
    else if(!saved.isEmpty()) {
        auto raw=d->data;for(auto it=saved.begin();it!=saved.end();++it)raw[it.key()]=it.value();QVariantMap next;
        raw["dock"]=d->panes->snapshot();
        if(monitor_state::normalize(raw,next))d->data=next;else {d->invalid=true;d->feedback="Invalid monitor preferences were preserved; reset them explicitly before editing.";}
    }
    // Clean presentation is never enabled by preference restore. Control panes may
    // restore; weak identities recover to the controller instead of an ordinal guess.
    d->boundScreens.clear();d->applyOutput();d->notify();
}
QVariantMap MonitorCustomization::outputOptions() const{return d->data.value("output").toMap();}
QVariantMap MonitorCustomization::paneRoutes() const {
    if(!d->routeCache.isEmpty())return d->routeCache;
    auto routes=d->data.value("panes").toMap();
    const auto all=d->displays->displays();const auto screens=QGuiApplication::screens();QHash<QString,QScreen*> byId;
    for(const auto& info:all)if(info.index>=0&&info.index<screens.size())byId[info.id]=screens[info.index];
    for(const auto& id:monitor_state::paneIds){auto route=routes[id].toMap();auto* screen=byId.value(route.value("displayId").toString());
        const auto live=d->boundScreens.value(id);
        if(live&&QGuiApplication::screens().contains(live.data())){screen=live.data();}
        bool strong=false;for(const auto& info:all)if(info.id==route.value("displayId").toString())strong=monitor_topology::strongIdentity(info,all);
        auto* controllerScreen=d->controller?d->controller->screen():QGuiApplication::primaryScreen();
        const auto placement=monitor_topology::place(live?DisplayManager::computeDisplayId(live):route.value("displayId").toString(),rect(route.value("geometry").toMap()),all,controllerScreen?DisplayManager::computeDisplayId(controllerScreen):QString{},screen&&(live==screen||strong));
        route["effectiveDisplayId"]=placement.displayId;route["recovered"]=placement.recovered&&route.value("detached").toBool();
        route["effectiveGeometry"]=mapRect(placement.geometry);
        route["frozen"]=route.value("frozen").toBool()||d->panes->pane(id).value("frozen").toBool();routes[id]=route;
    }d->routeCache=routes;return routes;
}
QVariantList MonitorCustomization::presets() const{return d->data.value("presets").toList();}
bool MonitorCustomization::layoutLocked() const{return d->panes->locked();}
QString MonitorCustomization::feedback() const{return d->feedback;}
QString MonitorCustomization::outputRoutingStatus() const{return d->displays->routingStatus();}
void MonitorCustomization::setLayoutLocked(bool locked){auto dock=d->panes->snapshot();dock["locked"]=locked;d->commit(d->data,{},{},dock);}
bool MonitorCustomization::setOutputOptions(const QVariantMap& patch) {
    auto opts=outputOptions();auto saved=d->data.value("outputsByDisplay").toMap();
    const auto previousId=opts.value("displayId").toString();
    const auto nextId=patch.value("displayId",previousId).toString();
    if(nextId!=previousId) {
        if(!previousId.isEmpty())saved[previousId]=opts;
        if(saved.contains(nextId)){const auto stored=saved.value(nextId).toMap();for(const auto& key:QStringList{"windowMode","fitMode","background","backgroundColor","guides","imageInfo","cursorPolicy"})if(stored.contains(key))opts[key]=stored[key];}
    }
    if(!monitor_state::patchOptions(patch,opts))return d->fail("Invalid presentation option; nothing changed.");
    if(opts.value("viewportLink")=="linked"&&(!opts.value("followSelection").toBool()||d->displays->isPinned()))return d->fail("Linked controller viewports require an output that follows the current image.");
    if(patch.contains("displayId")&&!opts.value("displayId").toString().isEmpty()&&!screenFor(opts.value("displayId").toString()))return d->fail("That display is unavailable.");
    if(patch.contains("displayId")&&!canPosition())return d->fail("This compositor does not support explicit window placement.");
    if(!nextId.isEmpty())saved[nextId]=opts;
    if(saved.size()>16)return d->fail("At most16 display preferences can be remembered. Reset monitor preferences before adding another.");
    auto next=d->data;next["output"]=opts;next["outputsByDisplay"]=saved;
    const bool explicitTarget=patch.contains("displayId");const QPointer<QScreen> selected=screenFor(nextId);const QPointer<QQuickWindow> session=d->displays->presentationWindow();
    return d->commit(next,{},[this,explicitTarget,selected,session]{if(explicitTarget&&session==d->displays->presentationWindow())d->sessionScreen=selected?selected.data():(session?session->screen():nullptr);});
}
bool MonitorCustomization::syncControllerViewport(const QVariantMap& state,quint64 generation) {
    if(d->syncing||generation!=d->displays->currentGeneration()||generation!=d->displays->outputGeneration()||outputOptions().value("viewportLink")!="linked"||d->displays->isPinned()||!outputOptions().value("followSelection").toBool())return false;
    const auto fit=state.value("fit").toString();
    if(state.contains("rotation")&&(!monitor_state::integer(state.value("rotation"),0,270)||state.value("rotation").toInt()%90))return false;
    if(!fit.isEmpty()&&!QStringList{"shrink","fit","fill","width","height","actual"}.contains(fit))return false;
    for(const auto& key:QStringList{"zoom","centerX","centerY","sourceWidth","sourceHeight","rotation"})if(state.contains(key)){
        bool ok=false;const auto number=state.value(key).toDouble(&ok);if(!ok||!std::isfinite(number)||std::abs(number)>1e9)return false;
    }
    if(fit.isEmpty()&&(!state.contains("zoom")||state.value("zoom").toDouble()<.001||state.value("zoom").toDouble()>64||!state.contains("centerX")||!state.contains("centerY")))return false;
    auto* window=qobject_cast<PresentationWindow*>(d->displays->presentationWindow());if(!window)return false;
    d->syncing=true;window->applyControllerViewport(state);d->syncing=false;return true;
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
    if(!canPosition()||!screenFor(id))return d->fail("That display cannot host the output.");
    auto next=d->data;auto opts=outputOptions();opts["displayId"]=id;next["output"]=opts;
    const QPointer<QScreen> target=screenFor(id);const QPointer<QQuickWindow> session=d->displays->presentationWindow();
    return d->commit(next,{},[this,target,session]{if(session==d->displays->presentationWindow())d->sessionScreen=target;});
}
bool MonitorCustomization::swapControllerAndOutput() {
    if(!canPosition())return d->fail("This compositor does not support swapping windows.");
    auto* target=screenFor(d->displays->activeDisplayId());
    if(!d->controller||!d->displays->isPresentationActive()||!target||target==d->controller->screen())return d->fail("An active output on a different display is required.");
    auto* old=d->controller->screen();const auto oldId=DisplayManager::computeDisplayId(old);
    const auto geometry=reachable(d->controller->geometry(),target,d->controller->minimumSize().expandedTo({320,240}));
    auto next=d->data;auto opts=outputOptions();opts["displayId"]=oldId;next["output"]=opts;
    const QPointer<QScreen> destination=target;
    const QPointer<QQuickWindow> session=d->displays->presentationWindow();
    const QPointer<QScreen> outputTarget=old;
    return d->commit(next,{},[this,destination,geometry,session,outputTarget]{if(session==d->displays->presentationWindow()){d->sessionScreen=outputTarget;if(destination&&d->controller){d->controller->setScreen(destination);if(d->controller->visibility()==QWindow::FullScreen)d->controller->setGeometry(destination->geometry());else if(d->controller->visibility()!=QWindow::Maximized)d->controller->setGeometry(geometry);}}});
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
    auto dock=d->panes->snapshot();auto panes=dock.value("panes").toMap();if(panes.contains(id)){auto p=panes[id].toMap();p["frozen"]=frozen;panes[id]=p;dock["panes"]=panes;}return d->commit(next,{},{},dock);
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
    next["selectedPreset"]=id;return d->commit(next,snapshot.value("routingMode").toString(),[this,kind]{
        const auto all=d->displays->displays();
        const auto strong=[&all](const QString& id){for(const auto& info:all)if(info.id==id)return monitor_topology::strongIdentity(info,all);return false;};
        const auto preferred=d->data.value("output").toMap().value("displayId").toString();auto* target=screenFor(preferred);
        if(target&&(target==d->sessionScreen||strong(preferred)))d->sessionScreen=target;
        else if(!preferred.isEmpty())d->feedback="Preset applied; reselect the preferred display before moving output to an unavailable or weak identity.";
        if(kind=="workspace")for(const auto& pane:monitor_state::paneIds){
            const auto preferredPane=d->data.value("panes").toMap().value(pane).toMap().value("displayId").toString();auto* selected=screenFor(preferredPane);
            if(!selected||(d->boundScreens.value(pane)!=selected&&!strong(preferredPane)))d->boundScreens[pane]=nullptr;
            else d->boundScreens[pane]=selected;
        }
    },kind=="workspace"?std::optional<QVariantMap>(snapshot.value("dock").toMap()):std::nullopt);
}
bool MonitorCustomization::resetPreferences() {
    auto next=d->data;next["output"]=monitor_state::options();next["panes"]=monitor_state::routes();next["presets"]=QVariantList{};next["outputsByDisplay"]=QVariantMap{};next.remove("selectedPreset");
    auto dock=d->panes->snapshot();dock["locked"]=false;auto panes=dock.value("panes").toMap();for(auto it=panes.begin();it!=panes.end();++it){auto p=it.value().toMap();p["frozen"]=false;it.value()=p;}dock["panes"]=panes;return d->commit(next,"mirror",{},dock,true);
}
void MonitorCustomization::recoverToController() {
    settings_detail::cancelPendingSettings(this,"monitor-customization/"+QString::number(d->slot));++d->revision;d->pending=false;
    d->displays->closePresentation();auto routes=d->data.value("panes").toMap();
    for(const auto& id:monitor_state::paneIds){auto route=routes[id].toMap();route["detached"]=false;routes[id]=route;}
    // Emergency access is immediate even if disk is busy or a preference version is newer.
    d->data["panes"]=routes;if(d->controller) {
        if(d->controller->visibility()==QWindow::Minimized)d->controller->showNormal();
        if(d->controller->visibility()!=QWindow::FullScreen&&d->controller->visibility()!=QWindow::Maximized)d->controller->setGeometry(reachable(d->controller->geometry(),d->controller->screen(),d->controller->minimumSize().expandedTo({320,240})));
        d->controller->requestActivate();
    }
    d->feedback="Presentation closed; panes returned to the controller. Lock preferences were retained.";d->notify();
}
} // namespace glancelot
