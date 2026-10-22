#pragma once
// Private PERF-MEASURE test driver. Inactive unless --perf-report and the explicit
// GLANCELOT_PERF_REPLAY=standard-v1 environment option are both supplied.
// Calls existing, read-only viewing APIs; never dispatches mutations or shell tools.
#include "glancelot/viewer_controller.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/thumbnail_service.hpp"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>

namespace glancelot::perf {
class Replay final : public QObject {
    enum class Phase { Initial, CacheFirst, CacheSecond, Forward, Backward, Raise,
        JumpNext, JumpBack, GridWait, Scroll, CombinedWait, CombinedSelected, Burst, Failure, Recovery };
    QGuiApplication& app_;
    ViewerController& viewer_;
    DirectoryEntryModel& grid_;
    ThumbnailService& thumbnails_;
    QPointer<QObject> root_;
    QTimer timer_;
    QElapsedTimer total_, step_;
    Phase phase_{Phase::Initial};
    bool done_{}, waiting_{}, probeReady_{};
    quint64 probeId_{};
    int requestedSteps_{}, steps_{}, progress_{}, cycles_{}, cycle_{}, scroll_{};
    QString result_, failure_, startPath_, folder_, priorPath_, target_, burstA_, burstB_;
    std::filesystem::path rootBefore_;
    QJsonObject actions_;
    void count(const char* key) { const auto name=QLatin1String(key); actions_[name]=actions_.value(name).toInt()+1; }
    void phase(Phase value) { phase_=value; step_.restart(); waiting_=false; progress_=0; }
    bool ready() const { return !viewer_.loading()&&!viewer_.directoryPending()&&viewer_.imageReady(); }
    void finish(bool passed,const char* reason) {
        if(done_)return;
        done_=true;timer_.stop();
        QSaveFile file(result_);
        const QByteArray json=QJsonDocument(QJsonObject{{"schema",1},{"passed",passed},{"reason",QLatin1String(reason)},
            {"actions",actions_},{"elapsed_ms",double(total_.elapsed())}}).toJson();
        const bool saved=file.open(QIODevice::WriteOnly)&&file.write(json)==json.size()&&file.commit();
        std::fprintf(stderr,"Glancelot perf replay: %s\n",saved?reason:"verdict_write_failed");
        app_.exit(passed&&saved?0:5);
    }
    void probe() {
        probeReady_=false;
        probeId_=thumbnails_.request(viewer_.filePath(),128,thumbnails_.epoch());
        if(!probeId_)finish(false,"thumbnail_probe_rejected");
        count("thumbnail_probe");step_.restart();
    }
    bool images() {
        burstA_.clear();burstB_.clear();
        for(int i=0;i<qMin(grid_.count(),64);++i) if(grid_.kindAt(i)=="image") {
            if(burstA_.isEmpty())burstA_=grid_.pathAt(i);
            else {burstB_=grid_.pathAt(i);return true;}
        }
        return false;
    }
    void tick() {
        if(done_)return;
        if(!root_){finish(false,"window_closed");return;}
        if(total_.elapsed()>300000||step_.elapsed()>30000) {finish(false,"replay_deadline");return;}
        switch(phase_) {
        case Phase::Initial:
            if(!ready()||viewer_.fileCount()<2)return;
            steps_=qMin(requestedSteps_,viewer_.fileCount()-1);
            startPath_=viewer_.filePath();folder_=viewer_.directory();rootBefore_=viewer_.snapshot().root;
            count("open");phase(Phase::CacheFirst);probe();return;
        case Phase::CacheFirst:
            if(!probeReady_)return;
            phase(Phase::CacheSecond);probe();return;
        case Phase::CacheSecond:
            if(!probeReady_)return;
            phase(Phase::Forward);return;
        case Phase::Forward:
        case Phase::Backward: {
            const bool forward=phase_==Phase::Forward;
            if(waiting_) {
                if(!ready()||viewer_.isCurrentImage(priorPath_))return;
                waiting_=false;++progress_;
            }
            if(progress_>=steps_) {
                if(forward)phase(Phase::Backward);
                else {
                    if(!viewer_.isCurrentImage(startPath_)) {finish(false,"reverse_did_not_restore_start");return;}
                    if(++cycle_<cycles_)phase(Phase::Forward);
                    else {phase(Phase::Raise);viewer_.raiseRoot();count("raise_root");}
                }
                return;
            }
            priorPath_=viewer_.filePath();waiting_=true;step_.restart();
            if(forward){viewer_.next();count("next");}else{viewer_.previous();count("previous");}
            return;
        }
        case Phase::Raise:
            if(!ready()||viewer_.snapshot().root==rootBefore_)return;
            phase(Phase::JumpNext);viewer_.nextFolder();count("next_folder");return;
        case Phase::JumpNext:
            if(!ready()||viewer_.directory()==folder_)return;
            phase(Phase::JumpBack);viewer_.previousFolder();count("previous_folder");return;
        case Phase::JumpBack:
            if(!ready()||viewer_.directory()!=folder_)return;
            phase(Phase::GridWait);viewer_.trigger("view-grid");count("grid");return;
        case Phase::GridWait:
            if(grid_.loading()||grid_.count()<2||!root_->property("gridMode").toBool())return;
            phase(Phase::Scroll);return;
        case Phase::Scroll: {
            if(step_.elapsed()<300)return;
            auto* view=root_->findChild<QQuickItem*>("photoGridView");
            if(!view){finish(false,"grid_view_missing");return;}
            const auto extent=qMax(0.0,view->property("contentHeight").toDouble()-view->height());
            const double fractions[]{0.0,0.5,1.0,1.0,0.5,0.0};
            view->setProperty("contentY",extent*fractions[scroll_]);count("grid_scroll");
            step_.restart();
            if(++scroll_==6){phase(Phase::CombinedWait);viewer_.trigger("view-combined");count("combined");}
            return;
        }
        case Phase::CombinedWait:
            if(grid_.loading()||!root_->property("combinedMode").toBool()||grid_.count()<2)return;
            if(!images()){finish(false,"combined_fixture_has_no_image_pair");return;}
            for(int i=0;i<qMin(grid_.count(),64);++i) if(grid_.kindAt(i)=="image"&&!viewer_.isCurrentImage(grid_.pathAt(i))) {
                target_=grid_.pathAt(i);grid_.select(i);phase(Phase::CombinedSelected);count("combined_selection");return;
            }
            finish(false,"combined_target_missing");return;
        case Phase::CombinedSelected:
            if(!ready()||!viewer_.isCurrentImage(target_))return;
            // Explicit opens in preview-only mode make the two-request cancellation test independent
            // of the combined pane's cursor-follow policy. Both paths were captured from this listing.
            viewer_.trigger("view-preview");count("preview");
            { const QString first=viewer_.isCurrentImage(burstA_)?burstB_:burstA_;
              burstB_=viewer_.filePath();
              phase(Phase::Burst);viewer_.open(first);viewer_.open(burstB_);count("burst_open");count("burst_open");return; }
        case Phase::Burst:
            if(!ready()||!viewer_.isCurrentImage(burstB_))return;
            phase(Phase::Failure);viewer_.open(failure_);count("corrupt_open");return;
        case Phase::Failure:
            if(viewer_.loading())return;
            if(viewer_.imageReady()){finish(false,"corrupt_fixture_unexpectedly_decoded");return;}
            phase(Phase::Recovery);viewer_.open(burstB_);count("recovery_open");return;
        case Phase::Recovery:
            if(!ready()||!viewer_.isCurrentImage(burstB_))return;
            if(thumbnails_.active()||thumbnails_.pending())return;
            finish(true,"ok");return;
        }
    }
public:
    Replay(QGuiApplication& app,ViewerController& viewer,DirectoryEntryModel& grid,ThumbnailService& thumbnails,
           QObject* root,int steps,int cycles,QString result,QString failure)
        :QObject(&app),app_(app),viewer_(viewer),grid_(grid),thumbnails_(thumbnails),root_(root),
        requestedSteps_(steps),cycles_(cycles),result_(std::move(result)),failure_(std::move(failure)) {
        connect(&thumbnails_,&ThumbnailService::ready,this,[this](quint64 id,const QImage& image){
            if(!done_&&id==probeId_){if(image.isNull())finish(false,"empty_thumbnail_probe");else probeReady_=true;}
        });
        connect(&thumbnails_,&ThumbnailService::failed,this,[this](quint64 id,const QString&){if(!done_&&id==probeId_)finish(false,"thumbnail_probe_failed");});
        timer_.setInterval(50);connect(&timer_,&QTimer::timeout,this,[this]{tick();});
        total_.start();step_.start();timer_.start();
    }
};
inline bool installReplay(QGuiApplication& app,ViewerController& viewer,DirectoryEntryModel& grid,
                          ThumbnailService& thumbnails,QObject* root,bool reporting) {
    const auto mode=qEnvironmentVariable("GLANCELOT_PERF_REPLAY");
    if(mode.isEmpty())return true;
    bool stepOk{},cycleOk{};
    const int steps=qEnvironmentVariableIntValue("GLANCELOT_PERF_REPLAY_STEPS",&stepOk);
    const int cycles=qEnvironmentVariableIntValue("GLANCELOT_PERF_REPLAY_CYCLES",&cycleOk);
    const auto result=qEnvironmentVariable("GLANCELOT_PERF_REPLAY_RESULT"),failure=qEnvironmentVariable("GLANCELOT_PERF_REPLAY_FAILURE");
    if(!reporting||mode!="standard-v1"||!stepOk||!cycleOk||steps<1||steps>200||cycles<1||cycles>20||result.isEmpty()||failure.isEmpty()) {
        std::fprintf(stderr,"Glancelot perf replay: invalid explicit test configuration\n");return false;
    }
    new Replay(app,viewer,grid,thumbnails,root,steps,cycles,result,failure);
    return true;
}
}
