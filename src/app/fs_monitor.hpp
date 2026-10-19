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

namespace fs_watch {
struct Observation {
    quint64 scope{}, revision{};
    QString directory,image;
    std::shared_ptr<const Snapshot> before,after,parentBefore,parentAfter;
    bool fileEvent{}, forced{}, coverage{};
};
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
        QObject* receiver{};
        std::function<void(Observation)> accept;
        std::stop_source stop;
    };
    class Worker final : public QThread {
        std::shared_ptr<State> state;
    public:
        explicit Worker(std::shared_ptr<State> value):state(std::move(value)){}
        void run() override {
            QObject context;QFileSystemWatcher watcher;QTimer poll;
            QElapsedTimer clock;clock.start();
            Request scope;std::shared_ptr<const Snapshot> previous,parent;
            bool dirty{},fileEvent{},forced{};qint64 firstDirty{},lastDirty{},lastProbe=-2000;
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
                    if(latest.scope!=scope.scope) {
                        scope=latest;previous.reset();parent.reset();fileEvent=false;
                        if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());
                        if(!watcher.directories().isEmpty())watcher.removePaths(watcher.directories());
                    }
                    forced=forced||latest.force;mark(false);
                    // A new scope needs its baseline immediately, before ordinary events.
                    if(!previous)firstDirty=lastDirty=clock.elapsed()-500;
                }
                if(scope.directory.isEmpty())return;
                const auto now=clock.elapsed();
                if(!dirty&&now-lastProbe>=2000)mark(false);
                if(!dirty||(!forced&&now-lastDirty<120&&now-firstDirty<500))return;
                dirty=false;lastProbe=now;
                const auto cancellation=state->stop.get_token();
                auto current=std::make_shared<Snapshot>(snapshot(scope.directory,cancellation));
                auto currentParent=std::make_shared<Snapshot>(snapshot(QDir(scope.directory).absolutePath()+"/..",cancellation));
                if(cancellation.stop_requested())return;
                // At most directory, its parent and current image. Remove obsolete native
                // registrations, then rearm names dropped after an atomic replace.
                QStringList wanted;
                if(current->revision.known&&current->revision.directory)wanted<<scope.directory;
                const auto parentPath=QDir::cleanPath(QDir(scope.directory).absolutePath()+"/..");
                if(currentParent->revision.known&&currentParent->revision.directory)wanted<<parentPath;
                if(!scope.image.isEmpty()&&probe(native(scope.image)).known)wanted<<scope.image;
                wanted.removeDuplicates();
                auto registered=watcher.files()+watcher.directories();
                for(const auto& path:registered)if(!wanted.contains(path))watcher.removePath(path);
                registered=watcher.files()+watcher.directories();
                for(const auto& path:wanted)if(!registered.contains(path))watcher.addPath(path);
                registered=watcher.files()+watcher.directories();
                bool coverage=current->revision.known&&current->revision.directory&&!wanted.isEmpty();
                for(const auto& path:wanted)coverage=coverage&&registered.contains(path);
                Observation result{scope.scope,++revision,scope.directory,scope.image,previous,current,parent,currentParent,fileEvent,forced,coverage};
                previous=current;parent=currentParent;fileEvent=forced=false;
                std::lock_guard guard(state->mutex);
                if(state->closed)return;
                state->publication=std::move(result);
                if(!state->publicationQueued) {
                    state->publicationQueued=true;
                    QMetaObject::invokeMethod(state->receiver,[shared=state] {
                        std::optional<Observation> value;
                        {
                            std::lock_guard guard(shared->mutex);
                            if(shared->closed)return;
                            value=std::move(shared->publication);shared->publication.reset();shared->publicationQueued=false;
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
    Monitor(QObject* receiver,std::function<void(Observation)> accept) {
        state->receiver=receiver;state->accept=std::move(accept);
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
    quint64 setScope(const QString& folder,const QString& file) {
        if(key(folder)==key(directory)&&key(file)==key(image))return scope;
        directory=folder;image=file;++scope;
        std::lock_guard guard(state->mutex);state->request={folder,file,scope,false};state->changed=true;
        return scope;
    }
    void refresh() {
        std::lock_guard guard(state->mutex);state->request={directory,image,scope,true};state->changed=true;
    }
    quint64 generation() const{return scope;}
};
}
