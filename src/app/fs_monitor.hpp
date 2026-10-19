#pragma once
#include "fs_revision.hpp"
#include <QFileSystemWatcher>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QMetaObject>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <algorithm>
#include <atomic>

namespace fs_watch {
struct Observation {
    quint64 scope{}, revision{};
    QString directory,image;
    std::shared_ptr<const Snapshot> before,after,parentBefore,parentAfter;
    bool fileEvent{}, forced{}, coverage{}, baseline{};
    Revision imageBefore,imageAfter;
    QString renamedImage,renamedDirectory;
};
struct MonitorOptions {
    bool registerNative{true};
    bool manualBaseline{};
    std::shared_ptr<std::atomic_bool> registrationGate;
    std::function<Snapshot(const QString&,std::stop_token)> read=[](const QString& path,std::stop_token stop){return snapshot(path,stop);};
};
struct MonitorStats {quint64 snapshots{},publications{};int maxHandles{};};
// One worker, one latest scope/request and one coalesced publication. No filesystem
// calls on the GUI thread, even registration and watch rearming. Timed fallback is
// necessary because QFileSystemWatcher cannot report every native queue overflow.
class Monitor {
    struct Request {QString directory,image;quint64 scope{};bool force{};};
    struct State {
        std::mutex mutex;
        Request request;
        bool changed{},closed{},publicationQueued{};
        std::optional<Observation> publication;
        std::shared_ptr<const Snapshot> accepted,acceptedParent;
        quint64 acceptedScope{};
        Revision acceptedImage;
        QObject* receiver{};
        std::function<void(Observation)> accept;
        std::stop_source stop;
        MonitorOptions options;
        MonitorStats stats;
        // Call only while holding mutex. Never carry an old scope's identities.
        void establishScope(quint64 value) {
            if(acceptedScope==value)return;
            acceptedScope=value;accepted.reset();acceptedParent.reset();acceptedImage={};
        }
        void remember(const Observation& value) {
            establishScope(value.scope);
            if(value.after&&value.after->complete)accepted=value.after;
            if(value.parentAfter&&value.parentAfter->complete)acceptedParent=value.parentAfter;
            acceptedImage=value.imageAfter;
        }
    };
    class Worker final : public QThread {
        std::shared_ptr<State> state;
    public:
        explicit Worker(std::shared_ptr<State> value):state(std::move(value)){}
        void run() override {
            QObject context;QFileSystemWatcher watcher;QTimer poll;
            QElapsedTimer clock;clock.start();
            Request scope;
            bool dirty{},fileEvent{},forced{},baselinePending{};qint64 firstDirty{},lastDirty{},lastProbe=-2000,retryDelay=2000;
            quint64 revision{};
            const auto mark=[&](bool file) {
                const auto now=clock.elapsed();if(!dirty)firstDirty=now;
                dirty=true;lastDirty=now;fileEvent=fileEvent||file;
            };
            QObject::connect(&watcher,&QFileSystemWatcher::fileChanged,&context,[&]{mark(true);});
            QObject::connect(&watcher,&QFileSystemWatcher::directoryChanged,&context,[&]{mark(false);});
            QObject::connect(&poll,&QTimer::timeout,&context,[&] {
                Request latest;bool changed=false;
                {
                    std::lock_guard guard(state->mutex);
                    if(state->closed){quit();return;}
                    if(state->changed){latest=state->request;state->request.force=false;state->changed=false;changed=true;}
                }
                if(changed) {
                    const bool newScope=latest.scope!=scope.scope;
                    if(newScope) {
                        scope=latest;fileEvent=false;baselinePending=true;
                        retryDelay=2000;
                        if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());
                        if(!watcher.directories().isEmpty())watcher.removePaths(watcher.directories());
                    }
                    forced=forced||latest.force;mark(false);
                    // A new scope needs its baseline immediately, before ordinary events.
                    if(newScope)firstDirty=lastDirty=clock.elapsed()-500;
                }
                if(scope.directory.isEmpty())return;
                const auto now=clock.elapsed();
                if(!dirty&&now-lastProbe>=retryDelay)mark(false);
                if(!dirty||(!forced&&now-lastDirty<120&&now-firstDirty<500))return;
                dirty=false;lastProbe=now;
                const auto cancellation=state->stop.get_token();
                auto current=std::make_shared<Snapshot>(state->options.read(scope.directory,cancellation));
                // Normalize before probing: on POSIX, missing-folder/.. cannot
                // be traversed after the folder was renamed or deleted.
                const auto parentPath=QDir::cleanPath(QDir(scope.directory).absolutePath()+"/..");
                auto currentParent=std::make_shared<Snapshot>(state->options.read(parentPath,cancellation));
                if(cancellation.stop_requested())return;
                // At most directory, its parent and current image. Remove obsolete native
                // registrations, then rearm names dropped after an atomic replace.
                QStringList wanted;
                if(current->revision.known&&current->revision.directory)wanted<<scope.directory;
                if(currentParent->revision.known&&currentParent->revision.directory)wanted<<parentPath;
                const auto currentImage=scope.image.isEmpty()?Revision{}:probe(native(scope.image));
                if(currentImage.known)wanted<<scope.image;
                wanted.removeDuplicates();
                const bool nativeEnabled=state->options.registerNative&&
                    (!state->options.registrationGate||state->options.registrationGate->load());
                auto registered=watcher.files()+watcher.directories();
                for(const auto& path:registered)if(!nativeEnabled||!wanted.contains(path))watcher.removePath(path);
                registered=watcher.files()+watcher.directories();
                if(nativeEnabled)for(const auto& path:wanted)if(!registered.contains(path))watcher.addPath(path);
                registered=watcher.files()+watcher.directories();
                bool coverage=current->revision.known&&current->revision.directory&&!current->revision.link&&
                    currentParent->revision.known&&currentParent->revision.directory&&!currentParent->revision.link;
                if(!scope.image.isEmpty())coverage=coverage&&currentImage.known&&!currentImage.directory;
                for(const auto& path:wanted)coverage=coverage&&registered.contains(path);
                // Delay is measured from completion, so an expensive/stalled
                // scan cannot turn periodic recovery into a continuously busy loop.
                lastProbe=clock.elapsed();
                retryDelay=coverage&&current->complete?2000:std::min<qint64>(30000,retryDelay*2);
                std::shared_ptr<const Snapshot> before,parentBefore;
                Revision imageBefore;
                {
                    std::lock_guard guard(state->mutex);
                    if(state->closed)return;
                    state->establishScope(scope.scope);
                    before=state->accepted;parentBefore=state->acceptedParent;imageBefore=state->acceptedImage;
                    // Seed the first complete identities even if the receiver has not
                    // consumed a publication. Partial observations never erase them.
                    if(!state->accepted&&current->complete)state->accepted=current;
                    if(!state->acceptedParent&&currentParent->complete)state->acceptedParent=currentParent;
                    if(!state->acceptedImage.known)state->acceptedImage=currentImage;
                }
                Observation result{scope.scope,++revision,scope.directory,scope.image,
                    before,current,parentBefore,currentParent,fileEvent,forced,coverage};
                result.imageBefore=imageBefore;result.imageAfter=currentImage;
                result.baseline=baselinePending;baselinePending=false;
                // Identity matching is worker work too. Retain only the two paths
                // needed by the controller, not another unbounded history/map.
                if(before&&!scope.image.isEmpty())
                    result.renamedImage=renames(*before,*current).value(key(scope.image));
                if(parentBefore)
                    result.renamedDirectory=renames(*parentBefore,*currentParent).value(key(scope.directory));
                fileEvent=forced=false;
                std::lock_guard guard(state->mutex);
                if(state->closed)return;
                ++state->stats.snapshots;
                state->stats.maxHandles=std::max(state->stats.maxHandles,int(registered.size()));
                if(state->publication&&state->publication->scope==scope.scope) {
                    result.fileEvent=result.fileEvent||state->publication->fileEvent;
                    result.forced=result.forced||state->publication->forced;
                    result.baseline=result.baseline||state->publication->baseline;
                }
                state->publication=std::move(result);
                if(!state->publicationQueued) {
                    state->publicationQueued=true;
                    QMetaObject::invokeMethod(state->receiver,[shared=state] {
                        std::optional<Observation> value;
                        {
                            std::lock_guard guard(shared->mutex);
                            if(shared->closed)return;
                            value=std::move(shared->publication);shared->publication.reset();shared->publicationQueued=false;
                            if(value) {
                                ++shared->stats.publications;
                                if(!shared->options.manualBaseline&&value->scope==shared->request.scope)
                                    shared->remember(*value);
                            }
                        }
                        if(value)shared->accept(std::move(*value));
                    },Qt::QueuedConnection);
                }
            });
            poll.start(50);exec();
        }
    };
    std::shared_ptr<State> state=std::make_shared<State>();
    Worker* worker{};
    QString directory,image;quint64 scope{};
public:
    Monitor(QObject* receiver,std::function<void(Observation)> accept,MonitorOptions options={}) {
        state->receiver=receiver;state->accept=std::move(accept);
        state->options=std::move(options);
        worker=new Worker(state);
        QObject::connect(worker,&QThread::finished,worker,&QObject::deleteLater);
        worker->start();
    }
    ~Monitor() {
        {
            std::lock_guard guard(state->mutex);state->closed=true;state->stop.request_stop();
            state->publication.reset();state->accept={};
        }
        worker->quit();
        // A blocked native call owns only shared/by-value worker data. It cannot
        // retain the controller or force unbounded GUI shutdown waiting.
        if(worker->wait(250))delete worker;
    }
    quint64 setScope(const QString& folder,const QString& file,bool restart=false) {
        if(!restart&&key(folder)==key(directory)&&key(file)==key(image))return scope;
        directory=folder;image=file;++scope;
        std::lock_guard guard(state->mutex);state->request={folder,file,scope,false};state->changed=true;
        return scope;
    }
    void refresh() {
        std::lock_guard guard(state->mutex);state->request={directory,image,scope,true};state->changed=true;
    }
    // A busy controller acknowledges only after reconciliation. Later worker
    // matches then still use the earliest unprocessed identity baseline.
    void acknowledge(const Observation& value) {
        std::lock_guard guard(state->mutex);
        if(state->closed||value.scope!=scope)return;
        state->remember(value);
    }
    quint64 generation() const{return scope;}
    MonitorStats stats()const{std::lock_guard guard(state->mutex);return state->stats;}
};
}
