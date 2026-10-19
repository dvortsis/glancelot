#include "glancelot/perf_trace.hpp"
#include "../core/perf_context.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include "glancelot/viewer_controller.hpp"
#include "glancelot/navigation.hpp"
#include "glancelot/settings_file.hpp"
#include "glancelot/platform_integration.hpp"
#include <QtConcurrent/QtConcurrentRun>
#include <QCache>
#include <QColorSpace>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QProcess>
#include <QSaveFile>
#include <QSharedMemory>
#include <QStandardPaths>
#include <QTimer>
#include <QThreadPool>
#include <QUrl>
#include <QUuid>
#include <QCoreApplication>
#include <algorithm>
#include <functional>
#include <deque>
#include <stop_token>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace glancelot;
namespace {
bool usableShortcut(const QKeySequence& sequence) {
    for (int i=0; i<sequence.count(); ++i) {
        const auto key=sequence[i];
        if (key.key()==Qt::Key_unknown || key==QKeyCombination(Qt::NoModifier,Qt::Key_Escape)) return false;
    }
    return true;
}
bool shortcutOverlap(const QKeySequence& left,const QKeySequence& right) {
    return !left.isEmpty() && !right.isEmpty()
        && (left.matches(right)!=QKeySequence::NoMatch || right.matches(left)!=QKeySequence::NoMatch);
}
bool usableBindings(const QJsonObject& bindings) {
    QList<QKeySequence> seen;
    for (const auto& value:bindings) {
        const auto text=value.toString();
        const auto sequence=QKeySequence::fromString(text,QKeySequence::PortableText);
        if ((!text.trimmed().isEmpty() && sequence.isEmpty()) || !usableShortcut(sequence)) return false;
        for (const auto& other:seen) if (shortcutOverlap(sequence,other)) return false;
        seen.append(sequence);
    }
    return true;
}
void trace(const char* event) {
    const auto path=qEnvironmentVariable("GLANCELOT_TRACE_FILE");
    if(path.isEmpty())return;
    QFile file(path);if(file.open(QIODevice::Append)) { file.write(event);file.write("\n"); }
}
std::filesystem::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).constData());
#endif
}
QString displayPath(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QFile::decodeName(path.native().c_str());
#endif
}
const std::vector<Command> registry {
    {"open","Open image","Ctrl+O"}, {"next","Next image","Right",true}, {"previous","Previous image","Left",true},
    {"next-folder","Next folder","PgDown"}, {"previous-folder","Previous folder","PgUp"},
    {"palette","Find a command","Ctrl+Shift+P"}, {"settings","Settings","Ctrl+,"},
    {"fullscreen","Toggle fullscreen","F11"}, {"fit","Fit image (shrink only)","F"},
    {"actual","Actual size (physical 1:1)","1"}, {"zoom-in","Zoom in","+",true}, {"zoom-out","Zoom out","-",true},
    {"rotate","Rotate view clockwise","R"}, {"background","Cycle canvas background","B"},
    {"grid","Toggle folder grid","G"}, {"frameless","Toggle window frame","Ctrl+F11"}, {"filmstrip","Toggle filmstrip","T"}, {"tree","Toggle folder tree","Ctrl+B"}, {"sort","Cycle file sort (name, modified, size, shuffle)","S"}, {"sort-reverse","Reverse file sort direction","Shift+S"},
    // Layout commands start unbound so existing users' shortcuts never collide.
    {"arrange-panes","Arrange panes",""}, {"layout-lock","Lock or unlock workspace layout",""}, {"layout-reset","Reset workspace layout",""},
    // Content modes (GRID-PREVIEW-01). Ctrl+Shift+G was free; saved maps that already use it keep their choice.
    {"view-preview","Show photo preview only",""}, {"view-grid","Show folder grid only",""},
    {"view-combined","Show grid and preview together","Ctrl+Shift+G"}, {"combined-orientation","Switch grid and preview side by side or stacked",""},
    {"reveal-in-file-manager", "Reveal in File Manager", "Ctrl+Alt+R"},
    {"open-terminal", "Open Terminal", "Ctrl+Alt+T"},
    {"copy-path", "Copy file path", "Ctrl+C"},
    {"copy-file", "Copy file", "Ctrl+Shift+C"},
    {"copy-image", "Copy image", "Ctrl+Alt+C"},
    {"select-all", "Select all items in grid", ""},
    {"clear-selection", "Clear grid selection", "Ctrl+D"},
    {"invert-selection", "Invert grid selection", "Ctrl+I"},
    {"grid-zoom-in", "Zoom in thumbnails", "Ctrl+="},
    {"grid-zoom-out", "Zoom out thumbnails", "Ctrl+-"},
    {"grid-zoom-reset", "Reset thumbnail size", "Ctrl+0"},
#ifdef Q_OS_WIN
    {"delete-selection","Move selected items to the Recycle Bin","Delete"},
    {"delete-current-file","Move current photo to the Recycle Bin","Ctrl+Delete"},
    {"delete-current-folder","Move current folder to the Recycle Bin","Ctrl+Shift+Delete"},
#else
    {"delete-selection","Move selected items to the Trash","Delete"},
    {"delete-current-file","Move current photo to the Trash","Ctrl+Delete"},
    {"delete-current-folder","Move current folder to the Trash","Ctrl+Shift+Delete"},
#endif
    {"quit","Quit","Ctrl+Q"}
};
// True when `folder` is `root` or below it (separator-aware; case-insensitive on Windows).
bool insideRoot(const QString& root, const QString& folder) {
    auto key=[](const QString& path){ auto value=QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
        value=value.toLower();
#endif
        return value; };
    const QString r=key(root),f=key(folder);
    if(r.isEmpty()||f.isEmpty())return false;
    return f==r||(f.startsWith(r)&&(r.endsWith(QLatin1Char('/'))||f.at(r.size())==QLatin1Char('/')));
}
QString configDirectory() {
    const QString override = qEnvironmentVariable("GLANCELOT_CONFIG_DIR");
    return override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : override;
}
QString sortKeyName(glancelot::SortKey key) {
    switch (key) {
    case glancelot::SortKey::Modified: return "modified";
    case glancelot::SortKey::Size: return "size";
    case glancelot::SortKey::Shuffle: return "shuffle";
    default: return "name";
    }
}
BoundaryMode boundaryValue(const QString& value) {
    return value == "stop" ? BoundaryMode::Stop : value == "loop" ? BoundaryMode::LoopFolder : BoundaryMode::ContinueTree;
}
}

struct ViewerController::Private {
    ViewerController* q;
    QImage image;
    QString path, status{"Open a JPEG or PNG to begin."}, boundary{"continue"}, wheel{"navigate"};
    QString directoryError;
    bool directoryPending{};
    bool loading{}, imageReady{}, filmstrip{true}, tree{}, treePin{};
    qreal textScale{1.0};
    bool reducedMotion{false};
    quint64 generation{}, navigationGeneration{};
    Navigator nav;
    QJsonObject bindings;
    QProcess* worker{};
    qint64 perfQueued{-1}, perfDecoding{-1};
    quint64 perfGeneration{};
    bool perfFirstDecode{true};
    std::uint8_t perfFlags{};
    void finishDecodeTrace(std::uint8_t flags, std::uint64_t bytes = 0) {
        using namespace glancelot::perf;
        detail::finish(perfDecoding >= 0 ? Stage::Decode : Stage::DecodeQueue, perfGeneration,
            perfDecoding >= 0 ? perfDecoding : perfQueued, std::uint8_t(perfFlags | flags), bytes);
        perfQueued = perfDecoding = -1;
    }
    QTimer deadline, reload;
    QFileSystemWatcher watcher;
    std::unique_ptr<QSharedMemory> transfer;
    qint64 admittedPixelBytes{};
    QByteArray output;
    QCache<QString,QImage> cache{128*1024}; // KiB, only source-revision keyed immutable RGBA pixels.
    struct NavigationResult { Navigator navigator; std::optional<std::filesystem::path> destination; QString message; bool directoryVisit{}, directoryFailed{}; };
    struct NavigationJob { quint64 token{}, sequence{}; bool scan{}; std::function<NavigationResult()> work; QString emptyMessage; };
    QThreadPool* navigationPool=new QThreadPool;
    bool navigationBusy{}, navigationReady{};
    std::deque<std::pair<int,bool>> navigationIntents;
    bool navigationOpen{};
    std::optional<NavigationJob> pendingNavigation;
    std::stop_source navigationStop;
    glancelot::SortSpec sortSpec;
    bool sortSaved{true};
    int slot{};
    QString pendingFolder, pendingImage, lastDecodedPath;
    int positionRetries{};
    bool positionDirty{}, restoreRequested{};
    QTimer positionTimer;
    // Paths this window just moved to the trash (DELETE-01); cleared by the next explicit open.
    QStringList removedPaths;
    bool removedCovers(const QString& value) const {
        if (value.isEmpty()) return false;
        for (const auto& removed: removedPaths) if (insideRoot(removed,value)) return true;
        return false;
    }

    explicit Private(ViewerController* owner):q(owner) {
        trace("controller:construct");
        navigationPool->setMaxThreadCount(1);
        for (const auto& command: registry) bindings[QString::fromStdString(command.id)] = QString::fromStdString(command.defaultBinding);
        load();
        trace("controller:settings-loaded");
        positionTimer.setSingleShot(true); positionTimer.setInterval(500);
        QObject::connect(&positionTimer,&QTimer::timeout,q,[this]{ writePosition(); });
        deadline.setSingleShot(true); deadline.setInterval(15000);
        QObject::connect(&deadline,&QTimer::timeout,q,[this]{ fail("Decoder timed out. You can open another image or retry."); });
        reload.setSingleShot(true); reload.setInterval(120);
        QObject::connect(&reload,&QTimer::timeout,q,[this]{
            cache.clear();
            emit q->filesInvalidated();
            // A removal this window reconciled itself is already being handled; stepping again would skip a photo.
            if (removedCovers(path) || removedCovers(path.isEmpty() ? displayPath(nav.snapshot().directory) : QString{})) return;
            if (QFileInfo::exists(path)) { const QString target=path; q->open(target); }
            else if(path.isEmpty()&&!nav.snapshot().directory.empty())q->navigateDirectory(displayPath(nav.snapshot().directory));
            else { navigate(1, false); }
        });
        QObject::connect(&watcher,&QFileSystemWatcher::fileChanged,q,[this]{ reload.start(); });
        QObject::connect(&watcher,&QFileSystemWatcher::directoryChanged,q,[this]{ reload.start(); });
    }
    ~Private() {
        writePosition(true);navigationStop.request_stop();cancel();
        // A blocked filesystem call cannot honor cancellation. Jobs own only by-value
        // captures, so a stalled thread can be abandoned rather than freezing window close.
        if(navigationPool->waitForDone(1000))delete navigationPool;
    }
    void notePosition(const QString& file) {
        lastDecodedPath=file;
        if (slot<=0) return;
        positionRetries=0;
        pendingImage=file; pendingFolder=QFileInfo(file).absolutePath(); positionDirty=true;
        positionTimer.start();
    }
    bool writePosition(bool final = false) {
        positionTimer.stop();
        if (!positionDirty || slot<=0) return true;
        const QString folder=pendingFolder,image=pendingImage;
        const int target=slot;
        const auto result=updateSettings([&](QJsonObject& object) {
            // Re-read under the lock: only this slot's entry changes; other windows' slots, bindings, themes
            // and unknown keys stay as they are on disk.
            auto windows=object.value("windows").toObject();
            windows[QString::number(target)]=QJsonObject{{"folder",folder},{"image",image},{"saved",QDateTime::currentSecsSinceEpoch()}};
            object["windows"]=windows;
        },final);
        positionDirty=!result.ok();
        // Retry transient contention without waiting in the UI or requiring another navigation.
        if (!final && positionDirty && (result.code==SettingsStatus::Busy || result.code==SettingsStatus::WriteFailed) && ++positionRetries<=8)
            positionTimer.start(500);
        return result.ok();
    }
    QString cacheKey(const QString& file) const {
        const QFileInfo info(file);
        return file + '|' + QString::number(info.size()) + '|' + QString::number(info.lastModified().toMSecsSinceEpoch());
    }
    void load() {
        QJsonObject object;
        const auto result=readSettings(object);
        if (result.code==SettingsStatus::Unreadable) { status="Settings could not be read; defaults are active."; return; }
        if (!result.ok()) { status="Settings are invalid or from a newer version; defaults are active. Start Glancelot with --reset-settings to set a damaged file aside."; return; }
        if (result.recovered) status="Settings were damaged; your last saved copy was restored.";
        if (object.isEmpty()) return;
        auto candidate=bindings;
        const auto stored=object.value("bindings").toObject();
        for (auto it=stored.begin();it!=stored.end();++it) {
            if (candidate.contains(it.key()) && it.value().isString()) candidate[it.key()]=QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText).toString(QKeySequence::PortableText);
        }
        // A command added after the settings file was written gets its default only if that key is still free;
        // otherwise the user's explicit choice wins and the new command starts unassigned.
        for (auto it=candidate.begin();it!=candidate.end();++it) {
            if (stored.contains(it.key()) || it.value().toString().isEmpty()) continue;
            for (auto other=stored.begin();other!=stored.end();++other) {
                if (other.key()!=it.key() && candidate.contains(other.key()) && shortcutOverlap(QKeySequence::fromString(candidate.value(other.key()).toString(),QKeySequence::PortableText),QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText))) { it.value()=QString(); break; }
            }
        }
        if (usableBindings(candidate)) bindings=candidate;
        else status="Saved shortcuts conflict or use reserved Escape; default shortcuts are active. Reset shortcuts in Settings to repair them.";
        const auto savedBoundary=object.value("boundary").toString();
        if (savedBoundary=="continue" || savedBoundary=="stop" || savedBoundary=="loop") boundary=savedBoundary;
        const auto savedWheel=object.value("wheel").toString();
        if (savedWheel=="navigate" || savedWheel=="zoom" || savedWheel=="pan") wheel=savedWheel;
        if (object.value("filmstrip").isBool()) filmstrip=object.value("filmstrip").toBool();
        const auto savedSort=object.value("sort").toObject();
        {
            const auto key=savedSort.value("key").toString();
            if (key=="name") sortSpec.key=glancelot::SortKey::Name;
            else if (key=="modified") sortSpec.key=glancelot::SortKey::Modified;
            else if (key=="size") sortSpec.key=glancelot::SortKey::Size;
            else if (key=="shuffle") sortSpec.key=glancelot::SortKey::Shuffle;
            if (savedSort.value("descending").isBool()) sortSpec.descending=savedSort.value("descending").toBool();
            if (savedSort.value("seed").isDouble() && savedSort.value("seed").toDouble()>=1 && savedSort.value("seed").toDouble()<9007199254740992.0) sortSpec.seed=quint64(savedSort.value("seed").toDouble());
        }
        if (object.value("tree").isBool()) tree=object.value("tree").toBool();
        if (object.value("treePinned").isBool()) treePin=object.value("treePinned").toBool();
        if (object.value("textScale").isDouble()) {
            const double s = object.value("textScale").toDouble();
            if (s >= 0.75 && s <= 2.0) textScale = s;
        } else textScale = 1.0;
        if (object.value("reducedMotion").isBool()) reducedMotion = object.value("reducedMotion").toBool();
        else reducedMotion = false;
    }
    // Writes only the named preferences through the shared locked writer, so another window's changes to other
    // keys (and unknown keys, never a future schema) survive.
    bool save(std::initializer_list<const char*> keys,const QString& bindingId=QString()) {
        return updateSettings([&](QJsonObject& object) {
            for (const char* key: keys) {
                const QString name=QString::fromLatin1(key);
                // One changed shortcut is merged into the stored map, so another window's other edits survive.
                if (name=="bindings" && !bindingId.isEmpty()) { auto stored=object.value(name).toObject(); stored[bindingId]=bindings.value(bindingId); object[name]=stored; }
                else if (name=="bindings") object[name]=bindings;
                else if (name=="boundary") object[name]=boundary;
                else if (name=="wheel") object[name]=wheel;
                else if (name=="filmstrip") object[name]=filmstrip;
                else if (name=="tree") object[name]=tree;
                else if (name=="treePinned") object[name]=treePin;
                else if (name=="textScale") object[name]=textScale;
                else if (name=="reducedMotion") object[name]=reducedMotion;
            }
        }).ok();
    }
    void cancel(std::uint8_t traceFlags = glancelot::perf::Cancelled) {
        finishDecodeTrace(std::uint8_t(traceFlags | (perfGeneration != generation ? glancelot::perf::Stale : 0)));
        deadline.stop();
        if (worker) {
            worker->disconnect(q);
            worker->setParent(nullptr);
            // No wait on UI thread; QProcess is deleted only after the process has stopped.
            if (worker->state()==QProcess::NotRunning) worker->deleteLater();
            else { QObject::connect(worker,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),worker,&QObject::deleteLater); worker->kill(); }
            worker=nullptr;
        }
        transfer.reset(); admittedPixelBytes=0; output.clear();
    }
    void fail(const QString& message) { trace(qPrintable("failure: "+message)); cancel(glancelot::perf::Failed); loading=false; imageReady=false; status=message; emit q->stateChanged(); }
    void observe() {
        if (!watcher.files().isEmpty()) watcher.removePaths(watcher.files());
        if (!watcher.directories().isEmpty()) watcher.removePaths(watcher.directories());
        if (QFileInfo::exists(path)) watcher.addPath(path);
        if (QFileInfo(QFileInfo(path).absolutePath()).isDir()) watcher.addPath(QFileInfo(path).absolutePath());
    }
    void decode(const QString& file,quint64 token) {
        trace("decode:begin");
        const auto key=[&] { glancelot::perf::ScopedSpan lookup(glancelot::perf::Stage::CacheLookup,token); return cacheKey(file); }();
        // Explicit opens are decoded again: coarse timestamps cannot prove content identity.
        // The bounded retained cache is reserved for a future validated-revision prefetch path.
        QString program=QCoreApplication::applicationDirPath()+"/glancelot-decoder";
#ifdef Q_OS_WIN
        program+=".exe";
#endif
        perfGeneration = token; perfQueued = glancelot::perf::detail::begin();
        perfDecoding = -1; perfFlags = perfFirstDecode ? glancelot::perf::Cold : glancelot::perf::None;
        perfFirstDecode = false;
        worker=new QProcess(q);
        worker->setStandardErrorFile(QProcess::nullDevice()); // bound memory even if a codec floods diagnostics
        auto* process=worker;
        QObject::connect(process,&QProcess::started,q,[this,token,process] {
            if (token != generation || process != worker) return;
            glancelot::perf::detail::finish(glancelot::perf::Stage::DecodeQueue,token,perfQueued,perfFlags);
            perfQueued = -1; perfDecoding = glancelot::perf::detail::begin();
        });
        QObject::connect(process,&QProcess::readyReadStandardOutput,q,[this,token,process,key]{
            trace("decode:message");
            if (token!=generation || process!=worker) return;
            output+=process->readAllStandardOutput();
            if (output.size()>65536) { fail("Decoder exceeded the control-message limit."); return; }
            while (output.contains('\n')) {
                const auto newline=output.indexOf('\n');
                const auto line=output.left(newline); output.remove(0,newline+1);
                QJsonParseError parse;
                const auto message=QJsonDocument::fromJson(line,&parse).object();
                if (parse.error!=QJsonParseError::NoError) { fail("Invalid decoder response."); return; }
                const auto phase=message.value("phase").toString();
                if (phase=="error") { fail(message.value("error").toString()); return; }
                if (phase=="probe") {
                    const qint64 width=message.value("width").toInteger(),height=message.value("height").toInteger();
                    const qint64 bytes=message.value("bytes").toInteger();
                    if (transfer || width<=0 || height<=0 || width>32768 || height>32768 || bytes!=width*height*4 || bytes>256LL*1024*1024) { fail("Decoder requested an invalid pixel allocation."); return; }
                    quint64 allowance=1024ULL*1024*1024;
#ifdef Q_OS_WIN
                    MEMORYSTATUSEX memory{}; memory.dwLength=sizeof(memory);
                    if (GlobalMemoryStatusEx(&memory)) allowance=std::min(allowance, memory.ullAvailPhys*4/10);
#endif
                    // Include decoder source/converted buffers, shared transfer and parent copy.
                    const quint64 admitted=quint64(bytes)*4+quint64(image.sizeInBytes())+quint64(cache.totalCost())*1024;
                    if (admitted>allowance) { cache.clear(); if (quint64(bytes)*4+quint64(image.sizeInBytes())>allowance) { fail("Insufficient memory headroom for this image. Close other images or try a smaller file."); return; } }
                    transfer=std::make_unique<QSharedMemory>("Glancelot-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
                    if (!transfer->create(bytes)) { fail("Cannot reserve the image transfer buffer: "+transfer->errorString()); return; }
                    admittedPixelBytes=bytes;
                    process->write(QJsonDocument(QJsonObject{{"key",transfer->key()}}).toJson(QJsonDocument::Compact)+'\n');
                } else if (phase=="decoded") {
                    const int width=message.value("width").toInt(),height=message.value("height").toInt(),stride=message.value("stride").toInt();
                    if (!transfer || width<=0 || height<=0 || width>32768 || height>32768 || stride!=width*4 || qint64(stride)*height!=admittedPixelBytes || admittedPixelBytes>transfer->size()) { fail("Decoder returned invalid image geometry."); return; }
                    if (!transfer->lock()) { fail("Cannot read decoded pixels."); return; }
                    QImage result(static_cast<const uchar*>(transfer->constData()),width,height,stride,QImage::Format_RGBA8888);
                    QImage owned=result.copy(); transfer->unlock();
                    if (owned.isNull()) { fail("Cannot allocate the current image."); return; }
                    image=std::move(owned);
                    image.setColorSpace(QColorSpace::SRgb);
                    const int cost=int((image.sizeInBytes()+1023)/1024);
                    if (cost<=cache.maxCost()) cache.insert(key,new QImage(image),cost);
                    finishDecodeTrace(glancelot::perf::None,std::uint64_t(image.sizeInBytes()));
                    loading=false; imageReady=true; deadline.stop();
                    status=message.value("color").toString()+" · display profile not yet managed";
                    notePosition(path);
                    process->write("ok\n");
                    observe(); emit q->imageChanged(); emit q->stateChanged();
                } else { fail("Unknown decoder response."); return; }
            }
        });
        QObject::connect(process,&QProcess::errorOccurred,q,[this,token](QProcess::ProcessError error){ if(token==generation && loading) fail(error==QProcess::FailedToStart ? "Decoder is missing. Keep glancelot-decoder next to the viewer." : "Decoder process failed; try another image."); });
        QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),q,[this,process,token](int code,QProcess::ExitStatus exit){
            if (token!=generation || process!=worker) return;
            worker=nullptr; transfer.reset(); process->deleteLater();
            if (loading) { finishDecodeTrace(glancelot::perf::Failed); deadline.stop(); loading=false; status=(exit==QProcess::CrashExit || code!=0) ? "Decoder failed; the previous valid image is preserved." : "Decoder returned no image."; emit q->stateChanged(); }
        });
        process->start(program,{file}); deadline.start();
        trace("decode:started");
    }
    void submitNavigation(NavigationJob job) {
        trace("navigation:submit");
        // At most one running job and one replacement pending job, including rapid open storms.
        if (navigationBusy) { pendingNavigation=std::move(job); return; }
        navigationBusy=true;
        auto* future=new QFutureWatcher<NavigationResult>(q);
        const auto token=job.token,sequence=job.sequence;
        const bool scanJob=job.scan;
        const QString emptyMessage=job.emptyMessage;
        QObject::connect(future,&QFutureWatcher<NavigationResult>::finished,q,[this,future,token,sequence,scanJob,emptyMessage]{
            trace("navigation:finished");
            auto result=future->result(); future->deleteLater(); navigationBusy=false;
            if (token==generation && sequence==navigationGeneration) {
                if(result.directoryVisit) {
                    directoryPending=false;
                    directoryError=result.directoryFailed?result.message:QString{};
                    if(result.directoryFailed) { status=result.message; emit q->stateChanged(); }
                    else if(!result.destination) {
                        ++generation; cancel(); reload.stop();
                        path.clear(); image=QImage{}; imageReady=false; loading=false;
                        lastDecodedPath.clear(); pendingImage.clear();
                        pendingFolder=displayPath(result.navigator.snapshot().directory);
                        positionRetries=0;positionDirty=slot>0;
                        if(positionDirty)positionTimer.start();
                        if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());
                        if(!watcher.directories().isEmpty())watcher.removePaths(watcher.directories());
                        watcher.addPath(displayPath(result.navigator.snapshot().directory));
                    }
                }
                const bool clearedImage=result.directoryVisit&&!result.directoryFailed&&!result.destination;
                nav=std::move(result.navigator); nav.setBoundary(boundaryValue(boundary));
                if(clearedImage)emit q->imageChanged();
                navigationReady=true; emit q->stateChanged();
                if(result.destination) { navigationOpen=true; q->open(displayPath(*result.destination)); navigationOpen=false; }
                else if(scanJob && !result.message.isEmpty()) { status=result.message; emit q->stateChanged(); }
                else if(!scanJob) { status=!result.message.isEmpty() ? result.message : emptyMessage.isEmpty() ? QString("Folder boundary reached. Change the boundary mode or choose another image.") : emptyMessage; emit q->stateChanged(); }
                if(result.directoryFailed&&!path.isEmpty())scan(path,generation);
                if(!result.destination && !pendingNavigation && needsResort()) startResort();
                if(!navigationBusy && navigationReady && !navigationIntents.empty()) { const auto intent=navigationIntents.front(); navigationIntents.pop_front(); navigate(intent.first,intent.second); }
            }
            if(!navigationBusy && pendingNavigation) { auto pending=std::move(*pendingNavigation); pendingNavigation.reset(); submitNavigation(std::move(pending)); }
        });
        future->setFuture(QtConcurrent::run(navigationPool,[work=std::move(job.work),sequence]() mutable { glancelot::perf::detail::GenerationContext context(sequence); return work(); }));
    }
    // Applies a new file order on the navigation worker (never the GUI thread); the current image stays current.
    // The navigator the UI shows can be older than a job that is still in flight, so a re-sort is only ever built
    // from an accepted navigator: immediately when idle, otherwise by the completion of the job already running.
    bool needsResort() const { return !nav.snapshot().current.empty() && !(nav.sort()==sortSpec); }
    void startResort() {
        navigationReady=false;
        Navigator copy=nav;
        copy.setCancellation(navigationStop.get_token());
        const auto next=sortSpec;
        const QString done=QString("Sorted by %1%2%3.").arg(sortKeyName(next.key),next.descending?" (reversed)":"",sortSaved?"":" for this session; settings could not be saved");
        submitNavigation({generation,++navigationGeneration,true,[copy,next,done]() mutable {
            copy.setSort(next);
            return NavigationResult{std::move(copy),std::nullopt,done};
        }});
    }
    void changeSort(const glancelot::SortSpec& next) {
        if (next==sortSpec) return;
        const auto previous=sortSpec;
        sortSpec=next;
        sortSaved=updateSettings([&](QJsonObject& object){
            // A stale window changes only the fields the user edited; other windows'
            // key/direction/seed choices and unknown nested preferences survive.
            auto stored=object.value("sort").toObject();
            if(next.key!=previous.key)stored["key"]=sortKeyName(next.key);
            if(next.descending!=previous.descending)stored["descending"]=next.descending;
            if(next.seed!=previous.seed || (next.key==SortKey::Shuffle && next.key!=previous.key))stored["seed"]=double(next.seed);
            object["sort"]=stored;
        }).ok();
        if (nav.snapshot().current.empty()) {
            status=sortSaved?"File order saved.":"File order changed for this session; settings could not be saved.";
            emit q->stateChanged(); return;
        }
        status=QString("Sorting by %1%2…").arg(sortKeyName(next.key),next.descending?" (reversed)":"");
        emit q->stateChanged();
        if (!navigationBusy && navigationReady && !pendingNavigation) startResort();
    }
    void scan(const QString& file,quint64 token) {
        navigationReady=false;
        const auto oldRoot=nav.snapshot().root;
        navigationStop.request_stop(); navigationStop=std::stop_source{};
        const auto cancellation=navigationStop.get_token();
        submitNavigation({token,++navigationGeneration,true,[file,oldRoot,cancellation,spec=sortSpec]{
            Navigator result; std::error_code error;
            result.setCancellation(cancellation);
            result.setSort(spec); // nothing is listed yet, so this is free
            if (result.open(nativePath(file),error) && !oldRoot.empty()) result.setRoot(oldRoot);
            return NavigationResult{std::move(result),std::nullopt};
        }});
    }
    void navigate(int direction,bool folderOnly) {
        if(!navigationReady || navigationBusy) {
            if(navigationIntents.size()<32) navigationIntents.emplace_back(direction,folderOnly);
            else { status="Navigation queue full; release the key or wheel to catch up."; emit q->stateChanged(); }
            return;
        }
        Navigator copy=nav;
        const auto cancellation=navigationStop.get_token();
        copy.setCancellation(cancellation);
        submitNavigation({generation,++navigationGeneration,false,[copy,direction,folderOnly]() mutable {
            const auto destination=folderOnly ? copy.folder(direction) : copy.step(direction);
            return NavigationResult{std::move(copy),destination};
        }});
    }
};

ViewerController::ViewerController(QObject* parent):QObject(parent),d(std::make_unique<Private>(this)){}
ViewerController::~ViewerController()=default; // ~Private flushes the pending window position
int ViewerController::windowSlot() const{return d->slot;}
void ViewerController::setWindowSlot(int slot){
    if(slot<0||slot==d->slot)return;
    d->writePosition();
    d->positionTimer.stop(); d->positionDirty=false;
    d->slot=slot;
    if(slot>0&&!d->lastDecodedPath.isEmpty())d->notePosition(d->lastDecodedPath);
    emit stateChanged();
}
void ViewerController::flushWindowPosition(){d->writePosition();}
void ViewerController::restoreWindow(){
    if(d->slot<=0||!d->path.isEmpty()||d->restoreRequested)return;
    QJsonObject settings;
    if(!readSettings(settings).ok())return;
    const auto entry=settings.value("windows").toObject().value(QString::number(d->slot)).toObject();
    const QString folder=entry.value("folder").toString(),image=entry.value("image").toString();
    if(folder.isEmpty()&&image.isEmpty())return;
    d->restoreRequested=true;
    d->status="Restoring your last folder…";emit stateChanged();
    d->submitNavigation({d->generation,++d->navigationGeneration,false,[image=nativePath(image),folder=nativePath(folder),folderText=folder,cancellation=d->navigationStop.get_token()]() mutable {
        namespace fs=std::filesystem;
        std::optional<fs::path> destination;QString message;std::error_code ec;
        if(image.empty()&&!folder.empty()) {
            Navigator target;target.setCancellation(cancellation);
            if(target.openDirectory(folder,ec)) {
                const auto current=target.snapshot().current;
                return Private::NavigationResult{std::move(target),current.empty()?std::nullopt:std::optional(current),"No supported images in this folder.",true,false};
            }
        }
        // The saved image is only trusted inside a listable, link-free folder, like every other open.
        if(!image.empty()&&isSupportedImage(image)&&fs::is_regular_file(image,ec)&&childFolders(image.parent_path(),cancellation).accessible) destination=image;
        else if(folder.empty()||!fs::is_directory(folder,ec)) message="Your last folder for this window is unavailable: "+folderText+". Open a photo to continue; the saved location is kept.";
        else if(!childFolders(folder,cancellation).accessible) message="Your last folder for this window cannot be read (no access, or it is a link): "+folderText+". The saved location is kept.";
        else {
            const auto files=imageFiles(folder,cancellation);
            if(!files.empty()) destination=files.front();
            else message="Your last folder for this window has no supported images: "+folderText+". The saved location is kept.";
        }
        return Private::NavigationResult{Navigator{},destination,message};
    },""});
}
QImage ViewerController::image() const{return d->image;}
QString ViewerController::filePath() const{return d->path;}
QString ViewerController::fileName() const{return QFileInfo(d->path).fileName();}
QString ViewerController::directory() const{return d->path.isEmpty()?displayPath(d->nav.snapshot().directory):QFileInfo(d->path).absolutePath();}
QString ViewerController::directoryError() const{return d->directoryError;}
bool ViewerController::directoryPending() const{return d->directoryPending;}
void ViewerController::copyDirectory() const{QGuiApplication::clipboard()->setText(directory());}
bool ViewerController::copyFilePath(const QStringList& paths) {
    QStringList resolved;
    for (const QString& p : paths) {
        if (!p.trimmed().isEmpty()) {
            const QUrl url(p);
            resolved.append(url.isLocalFile() ? url.toLocalFile() : p);
        }
    }
    if (resolved.isEmpty()) {
        if (!d->path.isEmpty()) resolved.append(d->path);
        else if (!directory().isEmpty()) resolved.append(directory());
    }
    if (resolved.isEmpty()) {
        d->status = QStringLiteral("Nothing selected to copy path.");
        emit stateChanged();
        return false;
    }
    QStringList nativePaths;
    nativePaths.reserve(resolved.size());
    for (const QString& p : resolved) {
        nativePaths.append(QDir::toNativeSeparators(p));
    }
#ifdef Q_OS_WIN
    const QString newline = QStringLiteral("\r\n");
#else
    const QString newline = QStringLiteral("\n");
#endif
    const QString joinedText = nativePaths.join(newline);
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(joinedText);
    }
    const int count = nativePaths.size();
    d->status = count == 1 ? QStringLiteral("Copied path to clipboard.") : QStringLiteral("Copied %1 paths to clipboard.").arg(count);
    emit stateChanged();
    return true;
}

bool ViewerController::copyFileUrls(const QStringList& paths) {
    QStringList resolved;
    for (const QString& p : paths) {
        if (!p.trimmed().isEmpty()) {
            const QUrl url(p);
            resolved.append(url.isLocalFile() ? url.toLocalFile() : p);
        }
    }
    if (resolved.isEmpty()) {
        if (!d->path.isEmpty()) resolved.append(d->path);
        else if (!directory().isEmpty()) resolved.append(directory());
    }
    if (resolved.isEmpty()) {
        d->status = QStringLiteral("Nothing selected to copy file.");
        emit stateChanged();
        return false;
    }
    QStringList nativePaths;
    QList<QUrl> urls;
    nativePaths.reserve(resolved.size());
    urls.reserve(resolved.size());
    for (const QString& p : resolved) {
        nativePaths.append(QDir::toNativeSeparators(p));
        urls.append(QUrl::fromLocalFile(p));
    }
#ifdef Q_OS_WIN
    const QString newline = QStringLiteral("\r\n");
#else
    const QString newline = QStringLiteral("\n");
#endif
    const QString joinedText = nativePaths.join(newline);
    if (auto* clipboard = QGuiApplication::clipboard()) {
        auto* mime = new QMimeData;
        mime->setUrls(urls);
        mime->setText(joinedText);
        clipboard->setMimeData(mime);
    }
    const int count = nativePaths.size();
    d->status = count == 1 ? QStringLiteral("Copied file to clipboard.") : QStringLiteral("Copied %1 files to clipboard.").arg(count);
    emit stateChanged();
    return true;
}

bool ViewerController::copyImageBitmap() {
    if (d->image.isNull()) {
        d->status = QStringLiteral("No image open to copy.");
        emit stateChanged();
        return false;
    }
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setImage(d->image);
    }
    d->status = QStringLiteral("Copied image bitmap to clipboard.");
    emit stateChanged();
    return true;
}
bool ViewerController::revealInFileManager(const QString& path) {
    QString target = path;
    if (target.isEmpty()) {
        target = !d->path.isEmpty() ? d->path : directory();
    } else {
        const QUrl url(target);
        if (url.isLocalFile()) target = url.toLocalFile();
    }
    if (target.isEmpty()) {
        d->status = "No photo or folder is open to reveal.";
        emit stateChanged();
        return false;
    }
    const bool ok = glancelot::PlatformIntegration::revealInFileManager(target);
    if (!ok) {
        d->status = "Could not open file manager.";
        emit stateChanged();
        return false;
    }
    return true;
}
bool ViewerController::openTerminal(const QString& directory) {
    QString targetDir = directory;
    if (targetDir.isEmpty()) {
        targetDir = this->directory();
        if (targetDir.isEmpty() && !d->path.isEmpty()) {
            targetDir = QFileInfo(d->path).absolutePath();
        }
    } else {
        const QUrl url(targetDir);
        if (url.isLocalFile()) targetDir = url.toLocalFile();
    }
    if (targetDir.isEmpty()) {
        d->status = "No folder is open to open a terminal.";
        emit stateChanged();
        return false;
    }
    const bool ok = glancelot::PlatformIntegration::openTerminal(targetDir);
    if (!ok) {
        d->status = "Could not open terminal.";
        emit stateChanged();
        return false;
    }
    return true;
}
QString ViewerController::status() const{return d->status;}
bool ViewerController::loading() const{return d->loading;}
bool ViewerController::imageReady() const{return d->imageReady;}
bool ViewerController::isCurrentImage(const QString& path) const {
    auto key=[](const QString& value){
        auto clean=QDir::cleanPath(QDir::fromNativeSeparators(value));
#ifdef Q_OS_WIN
        clean=clean.toCaseFolded();
#endif
        return clean;
    };
    return d->imageReady && key(path)==key(d->path);
}
int ViewerController::currentIndex() const{const auto& s=d->nav.snapshot(); const auto it=std::find(s.files.begin(),s.files.end(),s.current); return it==s.files.end()?0:int(std::distance(s.files.begin(),it))+1;}
int ViewerController::fileCount() const{return int(d->nav.snapshot().files.size());}
int ViewerController::imageWidth() const{return d->image.width();}
int ViewerController::imageHeight() const{return d->image.height();}
QVariantList ViewerController::commands() const{QVariantList rows;for(const auto& cmd:registry){const QString id=QString::fromStdString(cmd.id);rows.append(QVariantMap{{"id",id},{"label",QString::fromStdString(cmd.label)},{"binding",d->bindings.value(id).toString()}});}return rows;}
QString ViewerController::boundaryMode() const{return d->boundary;}
QString ViewerController::wheelMode() const{return d->wheel;}
void ViewerController::open(const QString& value){
    trace("open:begin");
    d->directoryPending=false;d->directoryError.clear();
    const QUrl url(value);const QString path=QFileInfo(url.isLocalFile()?url.toLocalFile():value).absoluteFilePath();
    if(!d->navigationOpen)d->navigationIntents.clear();
    d->removedPaths.clear();
    ++d->generation; d->cancel(); d->reload.stop();
    d->path=path;d->loading=true;d->imageReady=false;d->status="Opening "+QFileInfo(path).fileName()+"…";
    emit stateChanged(); d->scan(path,d->generation); d->decode(path,d->generation);
}
void ViewerController::next(){d->navigate(1,false);}void ViewerController::previous(){d->navigate(-1,false);}
void ViewerController::nextFolder(){d->navigate(1,true);}void ViewerController::previousFolder(){d->navigate(-1,true);}
void ViewerController::trigger(const QString& id){
    if(id=="next")next();else if(id=="previous")previous();else if(id=="next-folder")nextFolder();else if(id=="previous-folder")previousFolder();
    else if(id=="filmstrip")setFilmstripVisible(!d->filmstrip);
    else if(id=="sort"){const char* cycle[]={"name","modified","size","shuffle"};int at=0;for(int i=0;i<4;++i)if(sortKey()==cycle[i])at=i;setSortKey(cycle[(at+1)%4]);}
    else if(id=="sort-reverse")setSortDescending(!d->sortSpec.descending);
    else if(id=="tree")setTreeVisible(!d->tree);
    else if(id=="quit")QCoreApplication::quit();
    else if(id=="copy-path") {
        if(receivers(SIGNAL(viewCommand(QString))) == 0) copyFilePath();
        emit viewCommand(id);
    }
    else if(id=="copy-file") {
        if(receivers(SIGNAL(viewCommand(QString))) == 0) copyFileUrls();
        emit viewCommand(id);
    }
    else if(id=="copy-image") {
        if(receivers(SIGNAL(viewCommand(QString))) == 0) copyImageBitmap();
        emit viewCommand(id);
    }
    else if(d->bindings.contains(id))emit viewCommand(id);
}
QString ViewerController::bindingFor(const QString& id) const{return d->bindings.value(id).toString();}
bool ViewerController::setBinding(const QString& id,const QString& sequence){
    if(!d->bindings.contains(id))return false;
    const auto parsed=QKeySequence::fromString(sequence,QKeySequence::PortableText);
    const QString normalized=parsed.toString(QKeySequence::PortableText);
    if((!sequence.trimmed().isEmpty()&&normalized.isEmpty())||!usableShortcut(parsed)){d->status="That shortcut is invalid or uses Escape, which is reserved for closing views and dialogs.";emit stateChanged();return false;}
    for(auto it=d->bindings.begin();it!=d->bindings.end();++it){if(it.key()!=id&&shortcutOverlap(parsed,QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText))){d->status="Shortcut conflicts with "+it.key()+" (including chord prefixes). Clear it first.";emit stateChanged();return false;}}
    QJsonObject merged=d->bindings;
    merged[id]=normalized;
    QString conflict;
    const auto result=updateSettings([&](QJsonObject& object) {
        auto stored=object.value("bindings").toObject();
        // Validate the effective map while holding the writer lock: another window may have
        // assigned this shortcut since our settings were loaded.
        for(auto it=stored.begin();it!=stored.end();++it)
            if(it.key()!=id&&merged.contains(it.key())&&it.value().isString())merged[it.key()]=it.value();
        for(auto it=merged.begin();it!=merged.end();++it)
            if(it.key()!=id&&shortcutOverlap(parsed,QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText))){conflict=it.key();return;}
        if(!usableBindings(merged)){conflict="invalid saved shortcut map; reset default shortcuts";return;}
        stored[id]=normalized;object["bindings"]=stored;
    });
    if(!conflict.isEmpty()){d->status="Shortcut already assigned to "+conflict+" in another window. Clear it first.";emit stateChanged();return false;}
    d->bindings=result.ok()?merged:d->bindings;
    d->bindings[id]=normalized;
    d->status=result.ok()?"Shortcut saved.":"Shortcut changed for this session; settings could not be saved.";
    emit bindingsChanged();emit stateChanged();return true;
}
void ViewerController::resetBindings(){for(const auto& cmd:registry)d->bindings[QString::fromStdString(cmd.id)]=QString::fromStdString(cmd.defaultBinding);const bool saved=d->save({"bindings"});d->status=saved?"Default shortcuts restored.":"Defaults restored for this session; settings could not be saved.";emit bindingsChanged();emit stateChanged();}
bool ViewerController::treeVisible() const{return d->tree;}
bool ViewerController::treePinned() const{return d->treePin;}
void ViewerController::setTreeVisible(bool value){if(d->tree==value)return;d->tree=value;if(!d->save({"tree"}))d->status="Folder tree setting changed; settings could not be saved.";emit stateChanged();if(value)emit treeShown();}
void ViewerController::setTreePinned(bool value){if(d->treePin==value)return;d->treePin=value;if(!d->save({"treePinned"}))d->status="Folder tree pin changed; settings could not be saved.";emit stateChanged();}
void ViewerController::cancelDirectoryNavigation() {
    if(d->directoryPending) {
        d->navigationStop.request_stop();d->navigationStop=std::stop_source{};
        ++d->navigationGeneration;d->pendingNavigation.reset();d->navigationIntents.clear();
        if(!d->path.isEmpty())d->scan(d->path,d->generation);
    }
    d->directoryPending=false;d->directoryError.clear();emit stateChanged();
}
void ViewerController::navigateDirectory(const QString& value) {
    d->navigationIntents.clear();
    d->navigationStop.request_stop(); d->navigationStop=std::stop_source{};
    const auto cancellation=d->navigationStop.get_token();
    Navigator copy=d->nav; copy.setCancellation(cancellation);
    const auto sort=d->sortSpec;
    // Pure textual normalization: filesystem validation runs only on the navigation worker.
    const QString base=directory().isEmpty()?QDir::currentPath():directory();
    const QString folder=QDir::cleanPath(QDir(base).absoluteFilePath(QDir::fromNativeSeparators(value)));
    d->directoryPending=true;d->directoryError.clear();emit stateChanged();
    d->submitNavigation({d->generation,++d->navigationGeneration,false,[copy,folder,sort,cancellation,empty=value.isEmpty()]() mutable {
        Navigator target; target.setCancellation(cancellation); target.setSort(sort);
        std::error_code error;
        if(empty||!target.openDirectory(nativePath(folder),error))
            return Private::NavigationResult{std::move(copy),std::nullopt,"Cannot open that folder. Check the path and access permissions.",true,true};
        // Keep an explicitly widened traversal scope for visits inside it. Visits
        // outside it still start a bounded scope at the requested directory.
        if (!copy.snapshot().root.empty()) target.setRoot(copy.snapshot().root);
        const auto current=target.snapshot().current;
        return Private::NavigationResult{std::move(target),current.empty()?std::nullopt:std::optional(current),"No supported images in this folder.",true,false};
    },"No supported images in this folder."});
}
void ViewerController::openFolder(const QString& value){
    d->directoryPending=false;d->directoryError.clear();
    const QUrl url(value);const QString folder=QFileInfo(url.isLocalFile()?url.toLocalFile():value).absoluteFilePath();
    if(value.trimmed().isEmpty()||folder.isEmpty())return;
    d->navigationIntents.clear();
    // Newest folder request wins: stop whatever search or step is still running and start from a fresh token.
    d->navigationStop.request_stop(); d->navigationStop=std::stop_source{};
    Navigator copy=d->nav;
    const auto cancellation=d->navigationStop.get_token();
    copy.setCancellation(cancellation);
    d->status="Opening folder "+QFileInfo(folder).fileName()+"…";emit stateChanged();
    d->submitNavigation({d->generation,++d->navigationGeneration,false,[copy,directory=nativePath(folder),cancellation]() mutable {
        // Bounded preorder search (500 folders): a huge or hostile tree cannot occupy the navigation thread.
        const auto found=firstImageInSubtree(directory,cancellation);
        if(found.image) {
            const auto previousRoot=copy.snapshot().root;
            const bool preserveRoot=insideRoot(displayPath(previousRoot),displayPath(directory));
            // Publish the destination and its navigation snapshot together, including child-folder visits.
            std::error_code error;
            if(!copy.open(*found.image,error))
                return Private::NavigationResult{std::move(copy),std::nullopt,"Folder contents changed or cannot be read. Try opening the folder again."};
            copy.setRoot(preserveRoot ? previousRoot : directory);
        }
        Private::NavigationResult result{std::move(copy),found.image,{}};
        if(!found.image) {
            if(!found.accessible) result.message="That folder cannot be read (missing or no access).";
            else if(found.exhausted) result.message="No supported images found in the first 500 folders searched. Open a more specific folder.";
        }
        return result;
    },"No supported images in that folder or its subfolders."});
}
bool ViewerController::canRaiseRoot() const {
    const auto root=d->nav.snapshot().root; const auto parent=root.parent_path();
    return !root.empty() && !parent.empty() && parent!=root;
}
void ViewerController::raiseRoot(){
    const auto root=d->nav.snapshot().root;const auto parent=root.parent_path();
    if(root.empty()||parent.empty()||parent==root)return;
    Navigator copy=d->nav;
    const auto cancellation=d->navigationStop.get_token();
    copy.setCancellation(cancellation);
    d->submitNavigation({d->generation,++d->navigationGeneration,true,[copy,parent]() mutable {
        copy.setRoot(parent);
        // The tree lists the whole computer, so state the widened traversal root where the user can see it.
        return Private::NavigationResult{std::move(copy),std::nullopt,"Navigation now covers "+displayPath(parent)};
    }});
}
void ViewerController::reconcileRemovedPaths(const QStringList& paths) {
    if(paths.isEmpty())return;
    d->removedPaths=paths;
    // Thumbnails and cached pixels of removed paths are stale; the grid and filmstrip relist (newest generation wins).
    d->cache.clear();
    emit filesInvalidated();
    const QString folder=directory();
    const bool folderRemoved=d->removedCovers(folder);
    const bool imageRemoved=d->removedCovers(d->path);
    if(!folderRemoved&&!imageRemoved)return;
    // This window steps exactly once; a pending watcher reload or queued key repeat must not step again.
    d->reload.stop(); d->navigationIntents.clear();
    d->navigationStop.request_stop(); d->navigationStop=std::stop_source{};
    const auto cancellation=d->navigationStop.get_token();
    Navigator copy=d->nav; copy.setCancellation(cancellation);
    const auto sort=d->sortSpec;
    const bool anchored=imageRemoved&&!folderRemoved&&!d->nav.snapshot().current.empty()
        &&insideRoot(d->path,displayPath(d->nav.snapshot().current))&&insideRoot(displayPath(d->nav.snapshot().current),d->path);
    d->status=QStringLiteral("Updating after the move…");
    emit stateChanged();
    if(anchored) {
        // The removed photo keeps its sort position (deleted-current anchor): land on the next eligible image,
        // else the previous one, else show the folder itself.
        d->submitNavigation({d->generation,++d->navigationGeneration,false,[copy,sort,cancellation]() mutable {
            auto destination=copy.step(1);
            if(!destination)destination=copy.step(-1);
            if(destination)return Private::NavigationResult{std::move(copy),destination};
            Navigator target; target.setCancellation(cancellation); target.setSort(sort); std::error_code error;
            if(target.openDirectory(copy.snapshot().directory,error))
                return Private::NavigationResult{std::move(target),std::nullopt,"No photos are left in this folder.",true,false};
            return Private::NavigationResult{std::move(copy),std::nullopt,"No photos are left in this folder.",true,true};
        },"No photos are left in this folder."});
        return;
    }
    // The browsing folder (or an ancestor) is gone, or the image's navigation state is not settled: visit the nearest
    // existing ancestor. Existence is only ever tested here, on the navigation worker.
    const auto start=nativePath(folderRemoved ? folder : QFileInfo(d->path).absolutePath());
    d->submitNavigation({d->generation,++d->navigationGeneration,false,[copy,start,sort,cancellation,folderRemoved]() mutable {
        auto candidate=folderRemoved ? start.parent_path() : start;
        for(int depth=0;depth<256&&!candidate.empty()&&!cancellation.stop_requested();++depth) {
            Navigator target; target.setCancellation(cancellation); target.setSort(sort); std::error_code error;
            if(target.openDirectory(candidate,error)) {
                const auto current=target.snapshot().current;
                return Private::NavigationResult{std::move(target),current.empty()?std::nullopt:std::optional(current),"No supported images in this folder.",true,false};
            }
            const auto parent=candidate.parent_path();
            if(parent==candidate)break;
            candidate=parent;
        }
        return Private::NavigationResult{std::move(copy),std::nullopt,"The folder you were browsing is gone, and no folder above it can be opened.",true,true};
    },"No supported images in this folder."});
}
bool ViewerController::filmstripVisible() const{return d->filmstrip;}
const NavigationSnapshot& ViewerController::snapshot() const{return d->nav.snapshot();}
void ViewerController::setFilmstripVisible(bool value){if(d->filmstrip==value)return;d->filmstrip=value;if(!d->save({"filmstrip"}))d->status="Filmstrip setting changed; settings could not be saved.";emit stateChanged();}
QString ViewerController::sortKey() const{return sortKeyName(d->sortSpec.key);}
bool ViewerController::sortDescending() const{return d->sortSpec.descending;}
quint64 ViewerController::sortSeed() const{return d->sortSpec.seed;}
void ViewerController::setSortKey(const QString& key){
    auto next=d->sortSpec;
    if(key=="name")next.key=glancelot::SortKey::Name;
    else if(key=="modified")next.key=glancelot::SortKey::Modified;
    else if(key=="size")next.key=glancelot::SortKey::Size;
    else if(key=="shuffle"){if(next.key==glancelot::SortKey::Shuffle)return;next.key=glancelot::SortKey::Shuffle;next.seed=quint64(QDateTime::currentMSecsSinceEpoch())|1;}
    else return;
    d->changeSort(next);
}
void ViewerController::setSortDescending(bool descending){auto next=d->sortSpec;next.descending=descending;d->changeSort(next);}
void ViewerController::setBoundaryMode(const QString& value){if(value!="continue"&&value!="stop"&&value!="loop")return;d->boundary=value;d->nav.setBoundary(boundaryValue(value));if(!d->save({"boundary"}))d->status="Boundary mode changed; settings could not be saved.";emit stateChanged();}
void ViewerController::setWheelMode(const QString& value){if(value!="navigate"&&value!="zoom"&&value!="pan")return;d->wheel=value;if(!d->save({"wheel"}))d->status="Wheel behavior changed; settings could not be saved.";emit stateChanged();}
qreal ViewerController::textScale() const { return d->textScale; }
void ViewerController::setTextScale(qreal scale) {
    const qreal clamped = std::clamp(scale, 0.75, 2.0);
    if (qFuzzyCompare(clamped, d->textScale)) return;
    d->textScale = clamped;
    d->save({"textScale"});
    emit stateChanged();
}
bool ViewerController::reducedMotion() const { return d->reducedMotion; }
void ViewerController::setReducedMotion(bool reduced) {
    if (reduced == d->reducedMotion) return;
    d->reducedMotion = reduced;
    d->save({"reducedMotion"});
    emit stateChanged();
}
bool ViewerController::exportSettings(const QString& targetFilePath) {
    QString error;
    if (!glancelot::exportSettings(targetFilePath, &error)) {
        d->status = error.isEmpty() ? QStringLiteral("Could not export settings.") : error;
        emit stateChanged();
        return false;
    }
    d->status = QStringLiteral("Settings exported successfully.");
    emit stateChanged();
    return true;
}
bool ViewerController::importSettings(const QString& sourceFilePath) {
    QString error;
    if (!glancelot::importSettings(sourceFilePath, &error)) {
        d->status = error.isEmpty() ? QStringLiteral("Could not import settings.") : error;
        emit stateChanged();
        return false;
    }
    d->load();
    d->status = QStringLiteral("Settings imported successfully.");
    emit stateChanged();
    emit bindingsChanged();
    return true;
}
bool ViewerController::resetSection(const QString& sectionName) {
    QString error;
    if (!glancelot::resetSection(sectionName, &error)) {
        d->status = error.isEmpty() ? QStringLiteral("Could not reset section.") : error;
        emit stateChanged();
        return false;
    }
    d->load();
    d->status = QStringLiteral("Section reset to defaults.");
    emit stateChanged();
    emit bindingsChanged();
    return true;
}
bool ViewerController::resetAllSettings() {
    QString error;
    if (!glancelot::resetSection(QStringLiteral("all"), &error)) {
        d->status = error.isEmpty() ? QStringLiteral("Could not reset all settings.") : error;
        emit stateChanged();
        return false;
    }
    d->load();
    d->status = QStringLiteral("All settings reset to defaults.");
    emit stateChanged();
    emit bindingsChanged();
    return true;
}
