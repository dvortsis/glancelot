#include "glancelot/perf_trace.hpp"
#include "../core/perf_context.hpp"
#include "fs_monitor.hpp"
#include <QPointer>
#include "fs_source_token.hpp"
#include "io_job_lease.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include "glancelot/viewer_controller.hpp"
#include "decode_cache.hpp"
#include "glancelot/command_registry.hpp"
#include "binding_validation.hpp"
#include "recovery_session.hpp"
#include "window_claim_guard.hpp"
#include "glancelot/io_scheduler.hpp"
#include "glancelot/filter_state.hpp"
#include "glancelot/navigation.hpp"
#include "glancelot/settings_file.hpp"
#include "settings_retry.hpp"
#include "glancelot/platform_integration.hpp"
#include "glancelot/metadata_model.hpp"
#include "glancelot/metadata_parser.hpp"
#include <QtConcurrent/QtConcurrentRun>
#include <QColorSpace>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
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
#include <utility>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace glancelot;
namespace {
using glancelot::bindings::usableShortcut;
using glancelot::bindings::bindingsCollide;
using glancelot::bindings::findCommand;
using glancelot::bindings::usableBindings;
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
const auto registry=glancelot::commandRegistry();
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
bool validWheelAction(const QString& action) {
    return action=="navigate" || action=="zoom" || action=="pan" || action=="pan-horizontal";
}
BoundaryMode boundaryValue(const QString& value) {
    return value == "stop" ? BoundaryMode::Stop : value == "loop" ? BoundaryMode::LoopFolder : BoundaryMode::ContinueTree;
}
}

struct ViewerController::Private {
    ViewerController* q;
    QImage image, reference;
    QString referenceLabel;
    QString path, status{"Open a JPEG or PNG to begin."}, boundary{"continue"}, wheel{"navigate"};
    QString directoryError,monitorError;
    bool directoryPending{};
    bool loading{}, imageReady{}, filmstrip{true}, tree{}, treePin{};
    qreal textScale{1.0};
    bool reducedMotion{false};
    bool highContrast{false}, largeTargets{false};
    quint64 generation{}, navigationGeneration{};
    Navigator nav;
    glancelot::FilterSpec filter;
    QJsonObject bindings;
    QJsonObject confirmedBindings;
    // INPUT-02: explicit modifier -> action entries. "none" lives in `wheel`; an unset "alt" follows it.
    QJsonObject wheelModifiers{{"ctrl","zoom"},{"shift","pan-horizontal"}};
    QProcess* worker{};
    std::shared_ptr<glancelot::detail::IoJobLease> decodeLease,prefetchLease;
    quint64 workerGeneration{};
    qint64 perfQueued{-1}, perfDecoding{-1};
    quint64 perfGeneration{};
    bool perfFirstDecode{true},perfRevisionProbe{};
    bool lastDecodeCacheHit{false};
    std::uint8_t perfFlags{};
    void finishDecodeTrace(std::uint8_t flags, std::uint64_t bytes = 0) {
        using namespace glancelot::perf;
        glancelot::perf::detail::finish(perfRevisionProbe ? Stage::CacheLookup : perfDecoding >= 0 ? Stage::Decode : Stage::DecodeQueue, perfGeneration,
            perfDecoding >= 0 ? perfDecoding : perfQueued, std::uint8_t(perfFlags | flags), bytes);
        perfQueued = perfDecoding = -1;
    }
    QTimer deadline,decodeStart;
    bool decodeAwaitingBaseline{},decodeStartedWithoutBaseline{};
    std::unique_ptr<fs_watch::Monitor> monitor;
    std::optional<fs_watch::Observation> pendingObservation;
    quint64 observationRevision{};
#ifdef GLANCELOT_FS_WATCH_TESTING
    std::optional<fs_watch::ControllerTestHooks> testHooks=fs_watch::controllerTestHooks?
        std::optional{*fs_watch::controllerTestHooks}:std::nullopt;
#endif
    std::unique_ptr<QSharedMemory> transfer;
    qint64 admittedPixelBytes{};
    QByteArray output;
    glancelot::detail::DecodeCache cache;
    bool cacheNotificationPending{},ioNotificationPending{};
    QString storageProfile{"auto"};
    struct NavigationResult { Navigator navigator; std::optional<std::filesystem::path> destination; QString message; bool directoryVisit{}, directoryFailed{}, sortCompletion{}; };
    struct NavigationJob { quint64 token{}, sequence{}; bool scan{}; std::function<NavigationResult()> work; QString emptyMessage; };
    QThreadPool* navigationPool=new QThreadPool;
    // F002 path completion: one bounded folder listing at a time; a newer request cancels the older one.
    std::stop_source completionStop;
    quint64 completionGeneration{};
    bool navigationBusy{}, navigationReady{};
    std::deque<std::pair<int,bool>> navigationIntents;
    bool navigationOpen{},cacheReuseAllowed{};
    std::optional<NavigationJob> pendingNavigation;
    std::stop_source navigationStop;
    glancelot::SortSpec sortSpec;
    bool sortSaved{true};
    bool sortSavePending{};
    QString sortFeedbackPrefix, sortFeedback;
    QJsonObject pendingSortFields;
    int slot{};
    QString pendingFolder, pendingImage, lastDecodedPath;
    int positionRetries{};
    bool positionDirty{}, restoreRequested{};
    // F015 tabs: the inactive tabs' locations; the active tab's entry is refreshed from live state whenever it is read.
    struct TabRecord { QString folder, image; };
    QList<TabRecord> tabList;
    int activeTabIndex{0};
    TabRecord liveLocation() const { return {path.isEmpty()?displayPath(nav.snapshot().directory):QFileInfo(path).absolutePath(),path}; }
    QTimer positionTimer;
    std::unique_ptr<recovery::Session> recoverySession;
    WindowClaim recoveryClaim;
    std::optional<WindowClaim> pendingRecoveryClaim;
    recovery::ResumeState recoveryResume;
    QTimer recoveryPoll;
    quint64 checkpointSequence{},reportedCheckpointFailure{},recoveryEpoch{};
    bool recoveryResumeReady{},restorePending{},explicitPositionIntent{},safeMode{},recoveryFinalized{},beginFailureReported{},recoveryRetiring{};
    // Paths this window just moved to the trash (DELETE-01); cleared by the next explicit open.
    QStringList removedPaths;
    bool removedCovers(const QString& value) const {
        if (value.isEmpty()) return false;
        for (const auto& removed: removedPaths) if (insideRoot(removed,value)) return true;
        return false;
    }

    // Lookahead and asynchronous cross-folder prefetch (NAV-07)
    std::stop_source lookaheadStop;
    quint64 lookaheadGeneration{};
    QString lookaheadCandidate;
    QProcess* prefetchWorker{nullptr};
    std::unique_ptr<QSharedMemory> prefetchTransfer;
    qint64 prefetchAdmittedBytes{};
    QByteArray prefetchOutput;
    QTimer prefetchDeadline;
    struct LookaheadRequest { Navigator navigator; int direction{}; quint64 token{}; std::stop_token stop; };
    bool lookaheadBusy{};
    std::optional<LookaheadRequest> pendingLookahead;
    MetadataModel metadataModel;
    bool metadataVisible{false};
    quint64 metadataGeneration{0};
    struct MetadataJob {QString path;quint64 sourceGeneration{},revision{};};
    bool metadataBusy{};
    std::optional<MetadataJob> pendingMetadata;
    glancelot::AnimationController animationController;
    glancelot::MutationService mutationService;
    glancelot::CopyMoveService copyMoveService;
    glancelot::DisplayManager displayManager;

    void loadMetadata(const QString& targetPath, quint64 gen) {
        if(gen!=generation)return;
        animationController.load(targetPath, gen);
        if(gen!=generation)return;
        const quint64 metaGen=++metadataGeneration;
        pendingMetadata.reset();
        if (targetPath.isEmpty()) {
            metadataModel.clear();
            return;
        }
        metadataModel.setLoading(true);
        if(gen!=generation||metaGen!=metadataGeneration)return;
        MetadataJob job{targetPath,gen,metaGen};
        if(metadataBusy){pendingMetadata=std::move(job);return;}
        startMetadata(std::move(job));
    }
    void startMetadata(MetadataJob job) {
        metadataBusy=true;
        auto* future=new QFutureWatcher<MetadataRecord>(q);
        QObject::connect(future,&QFutureWatcher<MetadataRecord>::finished,q,[this,future,job] {
            auto record=future->future().takeResult();future->deleteLater();metadataBusy=false;
            if(job.sourceGeneration==generation&&job.revision==metadataGeneration)
                metadataModel.setRecord(record);
            if(!metadataBusy&&pendingMetadata) {
                auto next=std::move(*pendingMetadata);pendingMetadata.reset();
                startMetadata(std::move(next));
            }
        });
        std::function<void(const QString&)> beforeParse;
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks)beforeParse=testHooks->beforeMetadataParse;
#endif
        future->setFuture(QtConcurrent::run([job=std::move(job),beforeParse=std::move(beforeParse)] {
            if(beforeParse)beforeParse(job.path);
            return MetadataParser::parse(job.path,job.revision);
        }));
    }

    explicit Private(ViewerController* owner):q(owner) {
        trace("controller:construct");
        navigationPool->setMaxThreadCount(1);
        for (const auto& command: registry) bindings[QString::fromStdString(command.id)] = QString::fromStdString(command.defaultBinding);
        confirmedBindings=bindings;
        load();
        trace("controller:settings-loaded");
        positionTimer.setSingleShot(true); positionTimer.setInterval(500);
        QObject::connect(&positionTimer,&QTimer::timeout,q,[this]{ writePosition(); });
        recoveryPoll.setInterval(25);
        QObject::connect(&recoveryPoll,&QTimer::timeout,q,[this]{
            if(!recoverySession){recoveryPoll.stop();return;}
            const QPointer<ViewerController> alive(q);const auto epoch=recoveryEpoch;
            const auto state=recoverySession->snapshot();
            if(recoveryRetiring) {
                if(state.busy)return;
                auto pending=std::move(pendingRecoveryClaim);pendingRecoveryClaim.reset();
                recoverySession.reset();recoveryClaim={};recoveryRetiring=false;recoveryResumeReady=false;recoveryFinalized=false;
                recoveryPoll.stop();
                if(pending)q->setWindowClaim(*pending);
                else if(positionDirty)writePosition();
                return;
            }
            if(state.begun&&(!recoveryResumeReady||beginFailureReported)) {
                beginFailureReported=false;
                recoveryResumeReady=true;recoveryResume=state.beginning.previous;
                if(status=="Browsing recovery is unavailable. You can keep viewing images.") {
                    status="Browsing recovery is available.";emit q->stateChanged();
                    if(!alive||epoch!=recoveryEpoch)return;
                }
                if(restorePending&&!explicitPositionIntent)q->restoreWindow();
                if(!alive||epoch!=recoveryEpoch)return;
            }
            if(state.beginFinished&&!state.begun&&!state.busy&&!beginFailureReported) {
                beginFailureReported=true;
                recoveryResumeReady=true;
                recoveryResume=state.readOnlyResume.value_or(recovery::ResumeState{});
                status="Browsing recovery is unavailable. You can keep viewing images.";emit q->stateChanged();
                if(!alive||epoch!=recoveryEpoch)return;
                if(restorePending&&!explicitPositionIntent)q->restoreWindow();
                if(!alive||epoch!=recoveryEpoch)return;
            }
            if(!state.busy&&state.acceptedSequence>state.persistedSequence&&reportedCheckpointFailure!=state.acceptedSequence) {
                reportedCheckpointFailure=state.acceptedSequence;
                status="This browsing position could not be saved. Your previous saved position is kept.";emit q->stateChanged();
                if(!alive||epoch!=recoveryEpoch)return;
            }
            if(!state.busy&&!positionDirty&&state.beginFinished)recoveryPoll.stop();
        });
        deadline.setSingleShot(true); deadline.setInterval(15000);
        QObject::connect(&deadline,&QTimer::timeout,q,[this]{ fail("Decoder timed out. You can open another image or retry."); });
        prefetchDeadline.setSingleShot(true);prefetchDeadline.setInterval(15000);
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks) {
            if(testHooks->foregroundDeadlineMs>0)deadline.setInterval(testHooks->foregroundDeadlineMs);
            if(testHooks->prefetchDeadlineMs>0)prefetchDeadline.setInterval(testHooks->prefetchDeadlineMs);
            if(testHooks->captureLookaheadRequest) {
                QPointer<ViewerController> guard(q);
                testHooks->captureLookaheadRequest([this,guard]{if(guard)scheduleLookahead();});
            }
        }
#endif
        QObject::connect(&prefetchDeadline,&QTimer::timeout,q,[this]{cancelLookahead();});
        // Normally decode starts after native registration/baseline, closing the
        // open-to-watch race. A blocked filesystem never delays explicit decoding
        // indefinitely: the 100ms fallback starts it and a late baseline validates
        // it with one fresh decode after registration.
        decodeStart.setSingleShot(true);decodeStart.setInterval(100);
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks&&testHooks->baselineDeadlineMs>0)decodeStart.setInterval(testHooks->baselineDeadlineMs);
#endif
        QObject::connect(&decodeStart,&QTimer::timeout,q,[this]{
            if(!decodeAwaitingBaseline||path.isEmpty())return;
            decodeAwaitingBaseline=false;decodeStartedWithoutBaseline=true;decode(path,generation);
        });
        fs_watch::MonitorOptions monitorOptions;
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks)monitorOptions=testHooks->options;
#endif
        monitorOptions.manualBaseline=true;
        monitor=std::make_unique<fs_watch::Monitor>(q,[this](fs_watch::Observation value){
            if(value.scope!=monitor->generation()||value.revision<=observationRevision)return;
            observationRevision=value.revision;
            if(!path.isEmpty()&&value.image==path&&value.baseline) {
                if(decodeAwaitingBaseline) {
                    decodeStart.stop();decodeAwaitingBaseline=false;decode(path,generation);
                } else if(decodeStartedWithoutBaseline) {
                    decodeStartedWithoutBaseline=false;++generation;cancel();cacheReuseAllowed=false;scan(path,generation);decode(path,generation);
                }
            }
            if(pendingObservation&&pendingObservation->scope==value.scope) {
                const auto earliestComplete=[](const auto& pendingBefore,const auto& workerBefore,const auto& pendingAfter) {
                    if(pendingBefore&&pendingBefore->complete)return pendingBefore;
                    if(workerBefore&&workerBefore->complete)return workerBefore;
                    return pendingAfter&&pendingAfter->complete?pendingAfter:workerBefore;
                };
                value.before=earliestComplete(pendingObservation->before,value.before,pendingObservation->after);
                value.parentBefore=earliestComplete(pendingObservation->parentBefore,value.parentBefore,pendingObservation->parentAfter);
                value.fileEvent=value.fileEvent||pendingObservation->fileEvent;
                value.forced=value.forced||pendingObservation->forced;
                value.baseline=value.baseline||pendingObservation->baseline;
                value.subsequent=value.subsequent||pendingObservation->subsequent;
                if(pendingObservation->imageBefore.known)value.imageBefore=pendingObservation->imageBefore;
            }
            pendingObservation=std::move(value);
#ifdef GLANCELOT_FS_WATCH_TESTING
            if(testHooks&&testHooks->pending)testHooks->pending(*pendingObservation);
#endif
            reconcileObservation();
        },std::move(monitorOptions));

        QObject::connect(&mutationService, &glancelot::MutationService::pathRenamed, q, &ViewerController::reconcileRenamedPath);
        QObject::connect(&mutationService, &glancelot::MutationService::folderCreated, q, &ViewerController::reconcileCreatedFolder);
        QObject::connect(&copyMoveService, &glancelot::CopyMoveService::pathsMutated, q, [this](const QStringList& sources, const QStringList& destinations, bool isMove) {
            auto pathMatches = [](const QString& a, const QString& b) {
#ifdef Q_OS_WIN
                return a.compare(b, Qt::CaseInsensitive) == 0;
#else
                return a == b;
#endif
            };
            if (isMove && !path.isEmpty()) {
                for (int i = 0; i < sources.size(); ++i) {
                    if (pathMatches(path, sources[i])) {
                        q->reconcileRenamedPath(sources[i], destinations.value(i));
                        break;
                    }
                }
            }
            cache.clear();
            emit q->cacheChanged();
            emit q->filesInvalidated();
        });
        QObject::connect(&displayManager, &glancelot::DisplayManager::presentationActiveChanged, q, [this](bool) {
            emit q->presentationChanged();
        });
        displayManager.setToggleBindingProvider([this] { return q->bindingFor(QStringLiteral("presentation-toggle")); });
        QObject::connect(&displayManager,&glancelot::DisplayManager::currentImageRequested,q,[this]{
            if(imageReady&&!loading&&!directoryPending)displayManager.updateImage(path,image,generation);
        });
        displayManager.setOutputBindingProvider([this](const QString& id) { return q->bindingFor(id); });
        QObject::connect(&displayManager, &glancelot::DisplayManager::presentationModeChanged, q, [this](glancelot::PresentationMode) {
            emit q->presentationChanged();
        });
        QObject::connect(q,&ViewerController::stateChanged,q,[this]{ if(tabList.size()>1) emit q->tabsChanged(); });
    }
    ~Private() {
        monitor.reset();writePosition(true);navigationStop.request_stop();cancel();
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
        if(recoverySession&&!recoveryRetiring) {
            if(positionDirty&&slot>0&&!recoveryFinalized) {
                if(checkpointSequence<std::numeric_limits<quint64>::max()&&
                   recoverySession->accept({pendingFolder,pendingImage,checkpointSequence+1})) {
                    ++checkpointSequence;positionDirty=false;recoveryPoll.start();
                }
            }
            if(final&&!recoveryFinalized) {
                recoveryFinalized=true;recoveryPoll.stop();
                return recoverySession->close(1000,!positionDirty);
            }
            return !positionDirty;
        }
        if (!positionDirty || slot<=0) return true;
        const QString folder=pendingFolder,image=pendingImage;
        const int target=slot;
        QJsonArray tabRows;
        if(tabList.size()>1)for(int i=0;i<tabList.size();++i){const auto record=i==activeTabIndex?liveLocation():tabList[i];tabRows.append(QJsonObject{{"folder",record.folder},{"image",record.image}});}
        const auto result=updateSettings([&](QJsonObject& object) {
            // Re-read under the lock: only this slot's entry changes; other windows' slots, bindings, themes
            // and unknown keys stay as they are on disk.
            auto windows=object.value("windows").toObject();
            QJsonObject saved{{"folder",folder},{"image",image},{"saved",QDateTime::currentSecsSinceEpoch()}};
            if(!tabRows.isEmpty()){saved["tabs"]=tabRows;saved["activeTab"]=activeTabIndex;}
            windows[QString::number(target)]=saved;
            object["windows"]=windows;
        },final);
        positionDirty=!result.ok();
        // Retry transient contention without waiting in the UI or requiring another navigation.
        if (!final && positionDirty && (result.code==SettingsStatus::Busy || result.code==SettingsStatus::WriteFailed) && ++positionRetries<=8)
            positionTimer.start(500);
        return result.ok();
    }
    void retireRecovery() {
        if(!recoverySession||recoveryRetiring)return;
        writePosition();recoveryFinalized=true;recoverySession->close(0,!positionDirty);
    }
    QString cacheKey(const QString& file) const {
        return fs_watch::key(file); // Pure text; worker tokens validate the source.
    }
    void notifyCacheChanged() {
        if(cacheNotificationPending)return;
        cacheNotificationPending=true;
        QMetaObject::invokeMethod(q,[this]{cacheNotificationPending=false;emit q->cacheChanged();},Qt::QueuedConnection);
    }
    void notifyIoStatsChanged() {
        if(ioNotificationPending)return;ioNotificationPending=true;
        QMetaObject::invokeMethod(q,[this]{ioNotificationPending=false;emit q->ioStatsChanged();},Qt::QueuedConnection);
    }
    static std::optional<QString> responseSourceToken(const QJsonObject& message) {
        const auto value=message.value("sourceToken");
        if(!value.isString())return std::nullopt;
        const auto token=value.toString();
        if(!token.isEmpty()&&!fs_watch::validSourceToken(token))return std::nullopt;
        return token;
    }
    void load() {
        QJsonObject object;
        const auto result=readSettings(object);
        if (result.code==SettingsStatus::Unreadable) { status=QCoreApplication::translate("Status","Settings could not be read; defaults are active."); return; }
        if (!result.ok()) { status=QCoreApplication::translate("Status","Settings are invalid or from a newer version; defaults are active. Start Glancelot with --reset-settings to set a damaged file aside."); return; }
        if (result.recovered) status=QCoreApplication::translate("Status","Settings were damaged; your last saved copy was restored.");
        const auto resolvedBindings=glancelot::resolveCommandBindings(object.value("bindings").toObject());
        if(resolvedBindings.valid)confirmedBindings=bindings=resolvedBindings.effective;
        else status=resolvedBindings.error;
        const auto savedBoundary=object.value("boundary").toString();
        if (savedBoundary=="continue" || savedBoundary=="stop" || savedBoundary=="loop") boundary=savedBoundary;
        else boundary="continue";
        const auto savedWheel=object.value("wheel").toString();
        if (savedWheel=="navigate" || savedWheel=="zoom" || savedWheel=="pan") wheel=savedWheel;
        else wheel="navigate";
        // Each modifier falls back on its own: one invalid entry never discards the others.
        wheelModifiers=QJsonObject{{"ctrl","zoom"},{"shift","pan-horizontal"}};
        const auto savedModifiers=object.value("wheelModifiers").toObject();
        for (const char* modifier:{"ctrl","shift","alt"}) {
            const auto action=savedModifiers.value(QLatin1String(modifier)).toString();
            if (validWheelAction(action)) wheelModifiers[QLatin1String(modifier)]=action;
        }
        if (object.value("filmstrip").isBool()) filmstrip=object.value("filmstrip").toBool();
        else filmstrip=true;
        const auto savedSort=object.value("sort").toObject();
        {
            const auto key=savedSort.value("key").toString();
            if (key=="name") sortSpec.key=glancelot::SortKey::Name;
            else if (key=="modified") sortSpec.key=glancelot::SortKey::Modified;
            else if (key=="size") sortSpec.key=glancelot::SortKey::Size;
            else if (key=="shuffle") sortSpec.key=glancelot::SortKey::Shuffle;
            else sortSpec.key=glancelot::SortKey::Name;
            if (savedSort.value("descending").isBool()) sortSpec.descending=savedSort.value("descending").toBool();
            else sortSpec.descending=false;
            if (savedSort.value("seed").isDouble() && savedSort.value("seed").toDouble()>=1 && savedSort.value("seed").toDouble()<9007199254740992.0) sortSpec.seed=quint64(savedSort.value("seed").toDouble());
            else sortSpec.seed=0;
        }
        if (object.value("tree").isBool()) tree=object.value("tree").toBool();
        else tree=false;
        if (object.value("treePinned").isBool()) treePin=object.value("treePinned").toBool();
        else treePin=false;
        if (object.value("textScale").isDouble()) {
            const double s = object.value("textScale").toDouble();
            if (s >= 0.75 && s <= 2.0) textScale = s;
            else textScale = 1.0;
        } else textScale = 1.0;
        if (object.value("reducedMotion").isBool()) reducedMotion = object.value("reducedMotion").toBool();
        else reducedMotion = false;
        highContrast = object.value("highContrast").toBool(false);
        largeTargets = object.value("largeTargets").toBool(false);
        if (object.value("storageProfile").isString()) {
            storageProfile = object.value("storageProfile").toString();
            IoScheduler::instance().setProfile(parseStorageProfile(storageProfile));
        } else {
            storageProfile = "auto";
            IoScheduler::instance().setProfile(StorageProfile::Auto);
        }
    }
    // Writes only the named preferences through the shared locked writer, so another window's changes to other
    // keys (and unknown keys, never a future schema) survive.
    bool save(std::initializer_list<const char*> keys,const QString& bindingId=QString()) {
        QJsonObject values;QStringList channels;
        for(const char* key:keys) {
            const QString name=QString::fromLatin1(key);channels.append(name);
            if(name=="bindings")values[name]=bindingId.isEmpty()?QJsonValue(bindings):bindings.value(bindingId);
            else if(name=="boundary")values[name]=boundary;
            else if(name=="wheel")values[name]=wheel;
            else if(name=="filmstrip")values[name]=filmstrip;
            else if(name=="tree")values[name]=tree;
            else if(name=="treePinned")values[name]=treePin;
            else if(name=="textScale")values[name]=textScale;
            else if(name=="reducedMotion")values[name]=reducedMotion;
            else if(name=="highContrast")values[name]=highContrast;
            else if(name=="largeTargets")values[name]=largeTargets;
            else if(name=="storageProfile")values[name]=storageProfile;
        }
        const auto deferred=std::make_shared<bool>(false);
        const bool accepted=settings_detail::saveWithRetry(q,channels.join('/')+"/"+bindingId,[values,bindingId](QJsonObject& object) {
            for (auto it=values.begin();it!=values.end();++it) {
                const auto name=it.key();
                // One changed shortcut is merged into the stored map, so another window's other edits survive.
                if (name=="bindings" && !bindingId.isEmpty()) { auto stored=object.value(name).toObject(); stored[bindingId]=it.value(); object[name]=stored; }
                else object[name]=it.value();
            }
        },[this,deferred,values,bindingId](SettingsStatus result){
            if(result.ok()&&values.contains("bindings")) {
                if(bindingId.isEmpty())confirmedBindings=values.value("bindings").toObject();
                else confirmedBindings[bindingId]=values.value("bindings");
            }
            if(*deferred&&!result.ok()){status="Preferences changed for this session; settings could not be saved.";emit q->stateChanged();}
        });
        *deferred=true;return accepted;
    }
    void cancelLookahead() {
        lookaheadStop.request_stop();
        pendingLookahead.reset();prefetchDeadline.stop();
        lookaheadCandidate.clear();
        if (prefetchWorker) {
            glancelot::detail::retireIoProcess(std::exchange(prefetchWorker,nullptr),std::move(prefetchLease),q,[this]{notifyIoStatsChanged();});
        }
        else prefetchLease.reset();
        prefetchTransfer.reset();
        prefetchAdmittedBytes = 0;
        prefetchOutput.clear();
        notifyIoStatsChanged();
    }
    void cancel(std::uint8_t traceFlags = glancelot::perf::Cancelled) {
        ++metadataGeneration;pendingMetadata.reset();metadataModel.clear();
        animationController.clear();
        cancelLookahead();
        finishDecodeTrace(std::uint8_t(traceFlags | (perfGeneration != generation ? glancelot::perf::Stale : 0)));
        deadline.stop();
        decodeStart.stop();
        if (worker) {
            const auto retiredGeneration=workerGeneration;
            glancelot::detail::retireIoProcess(std::exchange(worker,nullptr),std::move(decodeLease),q,[this,retiredGeneration]{
                notifyIoStatsChanged();
                if(retiredGeneration==generation&&!loading&&imageReady&&navigationReady)scheduleLookahead();
            });
        }
        else decodeLease.reset();
        transfer.reset(); admittedPixelBytes=0; output.clear();
        notifyIoStatsChanged();
    }

    void startPrefetch(const QString& file, quint64 token) {
        if (loading || worker || prefetchWorker) return;
        if (!IoScheduler::instance().isPrefetchPermitted(file)) return;
        QString program = QCoreApplication::applicationDirPath() + "/glancelot-decoder";
#ifdef Q_OS_WIN
        program += ".exe";
#endif
        const auto key = cacheKey(file);
        prefetchWorker = new QProcess(q);
        prefetchWorker->setStandardErrorFile(QProcess::nullDevice());
        auto* process = prefetchWorker;

        QObject::connect(process, &QProcess::readyReadStandardOutput, q, [this, token, process, key, file] {
            if (token != lookaheadGeneration || process != prefetchWorker) return;
            prefetchOutput += process->readAllStandardOutput();
            if (prefetchOutput.size() > 65536) { cancelLookahead(); return; }
            while (prefetchOutput.contains('\n')) {
                const auto newline = prefetchOutput.indexOf('\n');
                const auto line = prefetchOutput.left(newline);
                prefetchOutput.remove(0, newline + 1);
                QJsonParseError parse;
                const auto message = QJsonDocument::fromJson(line, &parse).object();
                if (parse.error != QJsonParseError::NoError) { cancelLookahead(); return; }
                const auto phase = message.value("phase").toString();
                if (phase == "error") { cancelLookahead(); return; }
                if (phase == "probe") {
                    const qint64 width = message.value("width").toInteger();
                    const qint64 height = message.value("height").toInteger();
                    const qint64 bytes = message.value("bytes").toInteger();
                    if (prefetchTransfer || width <= 0 || height <= 0 || width > 32768 || height > 32768 || bytes != width * height * 4 || bytes > 256LL * 1024 * 1024) {
                        cancelLookahead(); return;
                    }
                    quint64 allowance = 1024ULL * 1024 * 1024;
#ifdef Q_OS_WIN
                    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
                    if (GlobalMemoryStatusEx(&memory)) allowance = std::min(allowance, memory.ullAvailPhys * 4 / 10);
#endif
                    const quint64 admitted = quint64(bytes) * 4 + quint64(image.sizeInBytes()) + quint64(cache.bytes());
                    if (admitted > allowance) { cancelLookahead(); return; }

                    auto admission=glancelot::detail::admitIoJob(JobPriority::Prefetch,bytes,file);
                    if(token!=lookaheadGeneration||process!=prefetchWorker)return;
                    if (!admission) {
                        cancelLookahead(); return;
                    }
                    prefetchLease=std::move(admission);notifyIoStatsChanged();
                    prefetchTransfer = std::make_unique<QSharedMemory>("Glancelot-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
                    if (!prefetchTransfer->create(bytes)) { cancelLookahead(); return; }
                    prefetchAdmittedBytes = bytes;
                    process->write(QJsonDocument(QJsonObject{{"key", prefetchTransfer->key()}}).toJson(QJsonDocument::Compact) + '\n');
                } else if (phase == "decoded") {
                    const auto source=responseSourceToken(message);
                    if(!source){cancelLookahead();return;}
                    const int width = message.value("width").toInt();
                    const int height = message.value("height").toInt();
                    const int stride = message.value("stride").toInt();
                    if (!prefetchTransfer || width <= 0 || height <= 0 || width > 32768 || height > 32768 || stride != width * 4 || qint64(stride) * height != prefetchAdmittedBytes || prefetchAdmittedBytes > prefetchTransfer->size()) {
                        cancelLookahead(); return;
                    }
                    if (!prefetchTransfer->lock()) { cancelLookahead(); return; }
                    QImage result(static_cast<const uchar*>(prefetchTransfer->constData()), width, height, stride, QImage::Format_RGBA8888);
                    QImage owned = result.copy();
                    prefetchTransfer->unlock();
                    if (!owned.isNull()) {
                        owned.setColorSpace(QColorSpace::SRgb);
                        if (cache.insert(key, std::move(owned), *source)) notifyCacheChanged();
                    }
                    process->write("ok\n");
                    cancelLookahead();
                    return;
                } else {cancelLookahead();return;}
            }
        });

        QObject::connect(process, &QProcess::errorOccurred, q, [this, token,process](QProcess::ProcessError) {
            if (token == lookaheadGeneration&&process==prefetchWorker) cancelLookahead();
        });
        QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), q, [this, token,process](int, QProcess::ExitStatus) {
            if (token == lookaheadGeneration&&process==prefetchWorker) cancelLookahead();
        });

        QStringList arguments{file};
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks) {
            if(!testHooks->decoderProgram.isEmpty())program=testHooks->decoderProgram;
            arguments=testHooks->decoderArguments+arguments;
            QObject::connect(process,&QProcess::started,q,[this,token,process,file]{
                if(token==lookaheadGeneration&&process==prefetchWorker&&testHooks->workerStarted)
                    testHooks->workerStarted(process,"prefetch",file);
            });
        }
#endif
        prefetchDeadline.start();process->start(program,arguments);
    }

    void scheduleLookahead() {
        cancelLookahead();
        if (nav.snapshot().boundary != BoundaryMode::ContinueTree) return;
        if (loading || worker) return;

        const auto snapshot = nav.snapshot();
        const QString currentDir = q->directory().isEmpty() ? QDir::currentPath() : q->directory();
        if (!IoScheduler::instance().isPrefetchPermitted(currentDir)) return;
        const auto total = int(snapshot.files.size());
        const auto it = std::find(snapshot.files.begin(), snapshot.files.end(), snapshot.current);
        const auto current = (it == snapshot.files.end()) ? -1 : int(std::distance(snapshot.files.begin(), it));
        if (total <= 0 || current < 0) return;

        int direction = 0;
        if (current >= total - 3) {
            direction = 1;
        } else if (current < 3) {
            direction = -1;
        }
        if (direction == 0) return;

        lookaheadStop = std::stop_source{};
        const auto token = ++lookaheadGeneration;
        auto cancelToken = lookaheadStop.get_token();

        LookaheadRequest request{nav,direction,token,cancelToken};
        if(lookaheadBusy){pendingLookahead=std::move(request);notifyLookaheadState();return;}
        startLookahead(std::move(request));
    }
    void notifyLookaheadState() {
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks&&testHooks->lookaheadState)testHooks->lookaheadState(lookaheadBusy?1:0,pendingLookahead?1:0);
#endif
    }
    void startLookahead(LookaheadRequest request) {
        lookaheadBusy=true;
        notifyLookaheadState();
        const auto token=request.token;const auto cancelToken=request.stop;
        auto* futureWatcher = new QFutureWatcher<std::optional<std::filesystem::path>>(q);
        QObject::connect(futureWatcher, &QFutureWatcher<std::optional<std::filesystem::path>>::finished, q, [this, futureWatcher, token, cancelToken] {
            const auto result = futureWatcher->result();
            futureWatcher->deleteLater();
            lookaheadBusy=false;
            notifyLookaheadState();
            if(!cancelToken.stop_requested()&&token==lookaheadGeneration&&result&&!result->empty()&&!loading&&!worker) {
                const QString targetPath=displayPath(*result);
                lookaheadCandidate=targetPath;
                if(!cache.contains(cacheKey(targetPath)))startPrefetch(targetPath,token);
            }
            if(!lookaheadBusy&&pendingLookahead) {
                auto next=std::move(*pendingLookahead);pendingLookahead.reset();
                startLookahead(std::move(next));
            }
        });

        std::function<std::optional<std::filesystem::path>(int,std::stop_token)> discover;
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks)discover=testHooks->discover;
#endif
        QFuture<std::optional<std::filesystem::path>> future = QtConcurrent::run(navigationPool, [navCopy = std::move(request.navigator), direction=request.direction, cancelToken,discover=std::move(discover)]() mutable -> std::optional<std::filesystem::path> {
            if (cancelToken.stop_requested()) return std::nullopt;
            if(discover)return discover(direction,cancelToken);
            navCopy.setCancellation(cancelToken);
            return navCopy.folder(direction);
        });
        futureWatcher->setFuture(future);
    }
    void fail(const QString& message) { trace(qPrintable("failure: "+message)); cancel(glancelot::perf::Failed); loading=false; imageReady=false; metadataModel.clear(); status=message; emit q->stateChanged(); }
    void observe(bool restart=false) {
        monitor->setScope(path.isEmpty()?displayPath(nav.snapshot().directory):QFileInfo(path).absolutePath(),path,restart);
    }
    void reconcileObservation() {
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks&&testHooks->deferReconciliation&&testHooks->deferReconciliation())return;
#endif
        if(!pendingObservation||navigationBusy||pendingNavigation||!navigationIntents.empty())return;
        auto value=std::move(*pendingObservation);pendingObservation.reset();
        if(value.scope!=monitor->generation())return;
        monitor->acknowledge(value);
        const auto& current=*value.after;
        // A proven original identity wins even when a new object already reused
        // its former pathname. Matching was performed on the monitor worker.
        if(!value.renamedDirectory.isEmpty()) {
            if(path.isEmpty())q->navigateDirectory(value.renamedDirectory);
            else q->open(QDir(value.renamedDirectory).filePath(QFileInfo(path).fileName()));
            return;
        }
        if(!current.complete) {
            // A parent identity match can prove a renamed directory even when its
            // previous spelling is now missing. Unknown/incomplete evidence cannot.
            if(current.missing&&value.parentBefore&&value.parentAfter) {
                if(value.before&&value.before->complete&&value.parentAfter->complete&&
                   !value.parentAfter->entries.contains(fs_watch::key(value.directory))) {
                    if(!removedCovers(value.directory))q->reconcileRemovedPaths({value.directory});
                    return;
                }
            }
            setMonitorError(current.missing?"The watched folder is missing. Press F5 to retry or open another folder.":
                "Folder monitoring could not obtain a complete snapshot. Press F5 to retry.");
            // A bounded/failed membership scan cannot prove deletion or rename.
            // The independent current-file probe can still establish a revision,
            // and explicit refresh must remain useful in a larger directory.
            if(value.subsequent||value.forced)emit q->filesInvalidated();
            const bool changed=value.imageBefore.known&&value.imageAfter.known&&!(value.imageBefore==value.imageAfter);
            if(!path.isEmpty()&&value.imageAfter.known&&(value.forced||value.fileEvent||changed)) {
                cache.clear();notifyCacheChanged();const auto target=path;q->open(target);
            }
            return;
        }
        setMonitorError(value.coverage?QString{}:QStringLiteral("Folder monitoring is degraded; periodic refresh and F5 remain available."));
        if(!value.before&&!value.forced&&!value.fileEvent)return;
        const bool changed=value.before&&value.before->entries!=current.entries;
        if(!changed&&!value.fileEvent&&!value.forced)return;
        cache.clear();notifyCacheChanged();emit q->filesInvalidated();
        if(removedCovers(path)||removedCovers(path.isEmpty()?value.directory:QString{}))return;
        if(!path.isEmpty()) {
            const auto oldKey=fs_watch::key(path);
            if(!value.renamedImage.isEmpty()) {q->open(value.renamedImage);return;}
            const auto at=current.entries.constFind(oldKey);
            if(at==current.entries.cend()) {
                q->reconcileRemovedPaths({path});return;
            }
            // Windows case-only renames keep the same lookup key. Publish the
            // directory's actual spelling so navigation still finds the current row.
            if(path!=at->path&&fs_watch::key(path)==fs_watch::key(at->path)) {
                q->open(at->path);return;
            }
            const bool revisionChanged=value.before&&value.before->entries.contains(oldKey)&&
                !(value.before->entries[oldKey].revision==at->revision);
            if(revisionChanged||value.fileEvent||value.forced) {
                const auto target=path;q->open(target);return;
            }
        }
        // Unrelated membership changes refresh navigation without reopening the
        // unchanged current image or widening its traversal root.
        Navigator copy=nav;const auto folder=value.directory;
        const auto stop=navigationStop.get_token();copy.setCancellation(stop);
        submitNavigation({generation,++navigationGeneration,true,[copy,folder,stop]() mutable {
            const auto check=fs_watch::snapshot(folder,stop);
            if(!check.complete)return NavigationResult{std::move(copy),std::nullopt,"Folder refresh was incomplete; retaining the previous location."};
            copy.refresh();return NavigationResult{std::move(copy),std::nullopt};
        }});
    }
    void setMonitorError(const QString& message) {
        if(message==monitorError&&(message.isEmpty()||directoryError==message))return;
        const bool owns=directoryError.isEmpty()||directoryError==monitorError;
        monitorError=message;
        // A successful background observation must not erase an explicit failed
        // address visit, which belongs to the user's navigation request.
        if(owns) {
            directoryError=message;if(!message.isEmpty())status=message;
            emit q->stateChanged();
        }
    }
    void decode(QString file,quint64 token,bool fresh=false) {
        if(token!=generation||file!=path)return;
        trace("decode:begin");
        cancelLookahead();
        const auto key=[&] { glancelot::perf::ScopedSpan lookup(glancelot::perf::Stage::CacheLookup,token); return cacheKey(file); }();

        const bool revisionOnly=!fresh&&cacheReuseAllowed&&cache.contains(key);
        lastDecodeCacheHit = false;
        QString program=QCoreApplication::applicationDirPath()+"/glancelot-decoder";
#ifdef Q_OS_WIN
        program+=".exe";
#endif
        perfGeneration = token; perfQueued = glancelot::perf::detail::begin();
        perfRevisionProbe=revisionOnly;
        perfDecoding = -1; perfFlags = perfFirstDecode ? glancelot::perf::Cold : glancelot::perf::None;
        perfFirstDecode = false;
        worker=new QProcess(q);
        worker->setStandardErrorFile(QProcess::nullDevice()); // bound memory even if a codec floods diagnostics
        auto* process=worker;
        workerGeneration=token;
        auto initialAdmission=glancelot::detail::admitIoJob(JobPriority::Foreground,0,file);
        if(token!=generation||process!=worker)return;
        if(!initialAdmission){fail("Storage admission limit exceeded.");return;}
        decodeLease=std::move(initialAdmission);
        notifyIoStatsChanged();
        QObject::connect(process,&QProcess::started,q,[this,token,process,revisionOnly,file] {
            if (token != generation || process != worker) return;
#ifdef GLANCELOT_FS_WATCH_TESTING
            if(testHooks&&testHooks->workerStarted)testHooks->workerStarted(process,revisionOnly?"revision":"decode",file);
            if(token!=generation||process!=worker)return;
#endif
            if(revisionOnly){perfDecoding=perfQueued;perfQueued=-1;return;}
            glancelot::perf::detail::finish(glancelot::perf::Stage::DecodeQueue,token,perfQueued,perfFlags);
            perfQueued = -1; perfDecoding = glancelot::perf::detail::begin();
        });
        QObject::connect(process,&QProcess::readyReadStandardOutput,q,[this,token,process,key,file,revisionOnly]{
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
                if (phase=="error") { fail(QCoreApplication::translate("Decoder",message.value("error").toString().toUtf8().constData())); return; } // the decoder process speaks English; the viewer translates its fixed messages
                if(revisionOnly) {
                    const auto source=responseSourceToken(message);
                    if(phase!="revision"||!source){fail("Invalid source revision response.");return;}
                    const QImage reused=cache.reuse(key,*source);
                    finishDecodeTrace(glancelot::perf::None);
                    cancel(); // Retire this process before callbacks can select another image.
                    if(reused.isNull()) {
                        if(cache.remove(key))notifyCacheChanged();
                        if(token==generation)decode(file,token,true);
                        return;
                    }
                    const auto hitStart=glancelot::perf::detail::begin();
                    image=reused;lastDecodeCacheHit=true;loading=false;imageReady=true;
                    status="Cached · display profile not yet managed";notePosition(path);
                    glancelot::perf::detail::finish(glancelot::perf::Stage::Decode,token,hitStart,
                        glancelot::perf::Flag::CacheHit,std::uint64_t(image.sizeInBytes()));
                    displayManager.updateImage(path, image, generation);
                    observe();notifyCacheChanged();emit q->imageChanged();emit q->stateChanged();
                    if(token==generation)scheduleLookahead();
                    if(token==generation)loadMetadata(path,token);
                    return;
                }
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
                    const quint64 admitted=quint64(bytes)*4+quint64(image.sizeInBytes())+quint64(cache.bytes());
                    if (admitted>allowance) { cache.clear();notifyCacheChanged(); if (quint64(bytes)*4+quint64(image.sizeInBytes())>allowance) { fail("Insufficient memory headroom for this image. Close other images or try a smaller file."); return; } }
                    auto admission=glancelot::detail::admitIoJob(JobPriority::Foreground,bytes,file);
                    if(token!=generation||process!=worker)return;
                    if (!admission) {
                        fail("Storage admission limit exceeded."); return;
                    }
                    decodeLease=std::move(admission);notifyIoStatsChanged();
                    transfer=std::make_unique<QSharedMemory>("Glancelot-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
                    if (!transfer->create(bytes)) { fail("Cannot reserve the image transfer buffer: "+transfer->errorString()); return; }
                    admittedPixelBytes=bytes;
                    process->write(QJsonDocument(QJsonObject{{"key",transfer->key()}}).toJson(QJsonDocument::Compact)+'\n');
                } else if (phase=="decoded") {
                    const auto source=responseSourceToken(message);
                    if(!source){fail("Invalid decoded source revision.");return;}
                    const int width=message.value("width").toInt(),height=message.value("height").toInt(),stride=message.value("stride").toInt();
                    if (!transfer || width<=0 || height<=0 || width>32768 || height>32768 || stride!=width*4 || qint64(stride)*height!=admittedPixelBytes || admittedPixelBytes>transfer->size()) { fail("Decoder returned invalid image geometry."); return; }
                    if (!transfer->lock()) { fail("Cannot read decoded pixels."); return; }
                    QImage result(static_cast<const uchar*>(transfer->constData()),width,height,stride,QImage::Format_RGBA8888);
                    QImage owned=result.copy(); transfer->unlock();
                    if (owned.isNull()) { fail("Cannot allocate the current image."); return; }
                    image=std::move(owned);
                    image.setColorSpace(QColorSpace::SRgb);
                    if (!cache.insert(key,image,*source))cache.remove(key);
                    notifyCacheChanged();
                    finishDecodeTrace(glancelot::perf::None,std::uint64_t(image.sizeInBytes()));
                    loading=false; imageReady=true; deadline.stop();
                    status=message.value("color").toString()+" · display profile not yet managed";
                    notePosition(path);
                    process->write("ok\n");
                    displayManager.updateImage(path, image, generation);
                    observe(); emit q->imageChanged(); emit q->stateChanged();
                    if(token==generation)scheduleLookahead();
                    if(token==generation)loadMetadata(path,token);
                    return;
                } else { fail("Unknown decoder response."); return; }
            }
        });
        QObject::connect(process,&QProcess::errorOccurred,q,[this,token,process](QProcess::ProcessError error){ if(token==generation&&process==worker&&loading) fail(error==QProcess::FailedToStart ? "Decoder is missing. Keep glancelot-decoder next to the viewer." : "Decoder process failed; try another image."); });
        QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),q,[this,process,token](int code,QProcess::ExitStatus exit){
            if (token!=generation || process!=worker) return;
            decodeLease.reset();notifyIoStatsChanged();
            worker=nullptr; transfer.reset(); process->deleteLater();
            if (loading) { finishDecodeTrace(glancelot::perf::Failed); deadline.stop(); loading=false; status=(exit==QProcess::CrashExit || code!=0) ? "Decoder failed; the previous valid image is preserved." : "Decoder returned no image."; emit q->stateChanged(); }
            else if (imageReady && navigationReady) { scheduleLookahead(); }
        });
        QStringList arguments=revisionOnly?QStringList{"--source-revision",file}:QStringList{file};
#ifdef GLANCELOT_FS_WATCH_TESTING
        if(testHooks) {
            if(!testHooks->decoderProgram.isEmpty())program=testHooks->decoderProgram;
            arguments=testHooks->decoderArguments+arguments;
        }
#endif
        deadline.start();process->start(program,arguments);
        trace("decode:started");
    }
    void submitNavigation(NavigationJob job) {
        trace("navigation:submit");
        cancelLookahead();
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
                        ++generation; cancel();
                        path.clear(); image=QImage{}; imageReady=false; loading=false; metadataModel.clear();
                        displayManager.updateImage(QString(), QImage(), generation);
                        lastDecodedPath.clear(); pendingImage.clear();
                        pendingFolder=displayPath(result.navigator.snapshot().directory);
                        positionRetries=0;positionDirty=slot>0;
                        if(positionDirty)positionTimer.start();
                    }
                }
                const bool clearedImage=result.directoryVisit&&!result.directoryFailed&&!result.destination;
                const auto oldDir = displayPath(nav.snapshot().directory);
                nav=std::move(result.navigator); nav.setBoundary(boundaryValue(boundary));
                observe();
                const auto newDir = displayPath(nav.snapshot().directory);
                const bool folderCrossed = !oldDir.isEmpty() && !newDir.isEmpty() && QDir(oldDir) != QDir(newDir);
                if (folderCrossed && !filter.isPersistent() && filter.isActive()) {
                    filter.clear();
                    emit q->filterChanged();
                }
                if(clearedImage)emit q->imageChanged();
                navigationReady=true; emit q->stateChanged(); emit q->currentStorageTypeChanged();
                if(!loading && imageReady) scheduleLookahead();
                if(result.destination) { navigationOpen=true; q->open(displayPath(*result.destination)); navigationOpen=false; }
                else if(result.sortCompletion) { setSortFeedback(QString("Sorted by %1%2").arg(sortKeyName(nav.sort().key),nav.sort().descending?" (reversed)":"")); emit q->stateChanged(); }
                else if(scanJob && !result.message.isEmpty()) { status=result.message; emit q->stateChanged(); }
                else if(!scanJob) { status=!result.message.isEmpty() ? result.message : emptyMessage.isEmpty() ? QString("Folder boundary reached. Change the boundary mode or choose another image.") : emptyMessage; emit q->stateChanged(); }
                if(result.directoryFailed&&!path.isEmpty())scan(path,generation);
                if(!result.destination && !pendingNavigation && needsResort()) startResort();
                if(!navigationBusy && navigationReady && !navigationIntents.empty()) { const auto intent=navigationIntents.front(); navigationIntents.pop_front(); navigate(intent.first,intent.second); }
            }
            if(!navigationBusy && pendingNavigation) { auto pending=std::move(*pendingNavigation); pendingNavigation.reset(); submitNavigation(std::move(pending)); }
            if(!navigationBusy)reconcileObservation();
        });
        future->setFuture(QtConcurrent::run(navigationPool,[work=std::move(job.work),sequence]() mutable { glancelot::perf::detail::GenerationContext context(sequence); return work(); }));
    }
    // Applies a new file order on the navigation worker (never the GUI thread); the current image stays current.
    // The navigator the UI shows can be older than a job that is still in flight, so a re-sort is only ever built
    // from an accepted navigator: immediately when idle, otherwise by the completion of the job already running.
    bool needsResort() const { return !nav.snapshot().current.empty() && !(nav.sort()==sortSpec); }
    void setSortFeedback(const QString& prefix) {
        sortFeedbackPrefix=prefix;
        sortFeedback=prefix+(sortSavePending?" for this session; saving settings is pending.":sortSaved?".":" for this session; settings could not be saved.");
        status=sortFeedback;
    }
    void startResort() {
        navigationReady=false;
        Navigator copy=nav;
        copy.setCancellation(navigationStop.get_token());
        const auto next=sortSpec;
        submitNavigation({generation,++navigationGeneration,true,[copy,next]() mutable {
            copy.setSort(next);
            return NavigationResult{std::move(copy),std::nullopt,{},false,false,true};
        }});
    }
    void changeSort(const glancelot::SortSpec& next) {
        if (next==sortSpec) return;
        const auto previous=sortSpec;
        sortSpec=next;
        QJsonObject fields;
        if(next.key!=previous.key)fields["key"]=sortKeyName(next.key);
        if(next.descending!=previous.descending)fields["descending"]=next.descending;
        if(next.seed!=previous.seed || (next.key==SortKey::Shuffle && next.key!=previous.key))fields["seed"]=double(next.seed);
        for(auto it=fields.begin();it!=fields.end();++it)pendingSortFields[it.key()]=it.value();
        const auto pending=pendingSortFields;
        const auto deferred=std::make_shared<bool>(false);
        sortSaved=false;sortSavePending=true;
        const bool accepted=settings_detail::saveWithRetry(q,"sort",[pending](QJsonObject& object){
            auto stored=object.value("sort").toObject();for(auto it=pending.begin();it!=pending.end();++it)stored[it.key()]=it.value();object["sort"]=stored;
        },[this,deferred](SettingsStatus result){
            sortSavePending=false;sortSaved=result.ok();if(result.ok())pendingSortFields={};
            if(*deferred&&!sortFeedback.isEmpty()&&status==sortFeedback){setSortFeedback(sortFeedbackPrefix);emit q->stateChanged();}
        });
        if(!accepted)sortSavePending=false;
        *deferred=true;
        if (nav.snapshot().current.empty()) {
            setSortFeedback("File order updated");
            emit q->stateChanged(); return;
        }
        status=QCoreApplication::translate("Status","Sorting by %1%2…").arg(sortKeyName(next.key),next.descending?QCoreApplication::translate("Status"," (reversed)"):QString());
        emit q->stateChanged();
        if (!navigationBusy && navigationReady && !pendingNavigation) startResort();
    }
    void scan(const QString& file,quint64 token) {
        navigationReady=false;
        const auto oldRoot=nav.snapshot().root;
        navigationStop.request_stop(); navigationStop=std::stop_source{};
        const auto cancellation=navigationStop.get_token();
        Navigator fallback=nav;fallback.setCancellation(cancellation);
        submitNavigation({token,++navigationGeneration,true,[file,oldRoot,cancellation,spec=sortSpec,fallback=std::move(fallback)]() mutable {
            Navigator result; std::error_code error;
            result.setCancellation(cancellation);
            result.setSort(spec); // nothing is listed yet, so this is free
            if(!result.open(nativePath(file),error))return NavigationResult{std::move(fallback),std::nullopt};
            if(!oldRoot.empty())result.setRoot(oldRoot);
            return NavigationResult{std::move(result),std::nullopt};
        }});
    }
    void navigate(int direction,bool folderOnly) {
        if(!navigationReady || navigationBusy) {
            if(navigationIntents.size()<32) navigationIntents.emplace_back(direction,folderOnly);
            else { status=QCoreApplication::translate("Status","Navigation queue full; release the key or wheel to catch up."); emit q->stateChanged(); }
            return;
        }
        cancelLookahead();
        Navigator copy=nav;
        const auto cancellation=navigationStop.get_token();
        copy.setCancellation(cancellation);
        submitNavigation({generation,++navigationGeneration,false,[copy,direction,folderOnly,filter=filter]() mutable {
            if (folderOnly || !filter.isActive()) {
                const auto destination=folderOnly ? copy.folder(direction) : copy.step(direction);
                return NavigationResult{std::move(copy),destination};
            }
            // Filter is active: intra-folder stepping obeys the filter (grid and traversal agreement)
            copy.refresh();
            const auto& allFiles = copy.snapshot().files;
            std::vector<std::filesystem::path> matchedFiles;
            matchedFiles.reserve(allFiles.size());
            for (const auto& f : allFiles) {
                const QString name = displayPath(f.filename());
                if (filter.matches(name)) {
                    matchedFiles.push_back(f);
                }
            }
            if (matchedFiles.empty()) {
                if (copy.snapshot().boundary == BoundaryMode::ContinueTree) {
                    const auto destination = copy.folder(direction);
                    return NavigationResult{std::move(copy), destination};
                }
                return NavigationResult{std::move(copy), std::nullopt};
            }
            auto found = std::find(matchedFiles.begin(), matchedFiles.end(), copy.snapshot().current);
            std::optional<std::filesystem::path> destination;
            if (found != matchedFiles.end()) {
                if (direction > 0) {
                    if (found + 1 != matchedFiles.end()) destination = *(found + 1);
                } else {
                    if (found != matchedFiles.begin()) destination = *(found - 1);
                }
            } else {
                if (direction > 0) destination = matchedFiles.front();
                else destination = matchedFiles.back();
            }
            if (!destination) {
                if (copy.snapshot().boundary == BoundaryMode::LoopFolder) {
                    destination = direction > 0 ? matchedFiles.front() : matchedFiles.back();
                } else if (copy.snapshot().boundary == BoundaryMode::ContinueTree) {
                    // Tree boundary crossed: candidate search evaluates directories without temporary filter (NAV-05)
                    const auto crossDest = copy.folder(direction);
                    return NavigationResult{std::move(copy), crossDest};
                }
            }
            return NavigationResult{std::move(copy), destination};
        }});
    }
};

ViewerController::ViewerController(QObject* parent):QObject(parent),d(std::make_unique<Private>(this)){}
ViewerController::~ViewerController()=default; // ~Private flushes the pending window position
int ViewerController::windowSlot() const{return d->slot;}
void ViewerController::setWindowSlot(int slot){
    if(slot<0||slot==d->slot)return;
    ++d->recoveryEpoch;
    if(d->recoverySession)d->retireRecovery();else d->writePosition();
    if(d->recoverySession&&d->recoverySession->snapshot().busy) {
        d->recoveryRetiring=true;d->pendingRecoveryClaim.reset();d->recoveryClaim={};
        d->slot=slot;d->positionDirty=false;d->positionTimer.stop();d->recoveryPoll.start();
        if(slot>0&&!d->lastDecodedPath.isEmpty())d->notePosition(d->lastDecodedPath);
        emit stateChanged();return;
    }
    d->recoverySession.reset();d->recoveryClaim={};d->recoveryPoll.stop();
    d->recoveryRetiring=false;d->pendingRecoveryClaim.reset();
    d->recoveryResumeReady=false;d->recoveryFinalized=false;d->checkpointSequence=0;
    d->positionTimer.stop(); d->positionDirty=false;
    d->slot=slot;
    if(slot>0&&!d->lastDecodedPath.isEmpty())d->notePosition(d->lastDecodedPath);
    emit stateChanged();
}
void ViewerController::setWindowClaim(const glancelot::WindowClaim& identity){
    const auto claim=glancelot::detail::WindowClaimAccess::get(identity);
    if(!claim||!claim->active.load()){d->pendingRecoveryClaim.reset();setWindowSlot(0);return;}
    if(!d->recoveryRetiring&&glancelot::detail::WindowClaimAccess::get(d->recoveryClaim)==claim)return;
    ++d->recoveryEpoch;
    if(d->recoverySession)d->retireRecovery();else d->writePosition();
    d->positionTimer.stop();d->recoveryPoll.stop();
    if(d->recoverySession&&d->recoverySession->snapshot().busy) {
        d->pendingRecoveryClaim=identity;d->recoveryRetiring=true;d->slot=0;d->positionDirty=false;
        d->recoveryPoll.start();emit stateChanged();return;
    }
    d->recoverySession.reset();d->recoveryClaim=identity;d->slot=claim->slot;d->checkpointSequence=0;
    d->recoveryRetiring=false;d->pendingRecoveryClaim.reset();
    d->positionDirty=false;d->recoveryResumeReady=false;d->recoveryFinalized=false;d->restoreRequested=false;
    d->beginFailureReported=false;d->reportedCheckpointFailure=0;
    d->recoverySession=std::make_unique<recovery::Session>(identity);d->recoveryPoll.start();
    if(!d->lastDecodedPath.isEmpty())d->notePosition(d->lastDecodedPath);
    else if(d->navigationReady&&!d->directoryPending&&!d->nav.snapshot().directory.empty()) {
        d->pendingImage.clear();d->pendingFolder=displayPath(d->nav.snapshot().directory);
        d->positionDirty=true;d->positionTimer.start();
    }
    emit stateChanged();
}
void ViewerController::setSafeMode(bool enabled){if(d->safeMode==enabled)return;d->safeMode=enabled;emit stateChanged();}
void ViewerController::flushWindowPosition(){d->writePosition();}
void ViewerController::restoreWindow(){
    if(d->safeMode||d->explicitPositionIntent||d->slot<=0||!d->path.isEmpty()||d->restoreRequested)return;
    QString folder,image;
    QJsonObject entry; // the saved settings entry (F015 tabs); a recovery session only carries folder and image
    if(d->recoverySession) {
        if(!d->recoveryResumeReady){d->restorePending=true;d->recoveryPoll.start();return;}
        folder=d->recoveryResume.folder;image=d->recoveryResume.image;
    }else {
        QJsonObject settings;if(!readSettings(settings).ok())return;
        entry=settings.value("windows").toObject().value(QString::number(d->slot)).toObject();
        folder=entry.value("folder").toString();image=entry.value("image").toString();
    }
    d->restorePending=false;
    if(folder.isEmpty()&&image.isEmpty())return;
    {   // F015: a saved tab set returns with its active tab; only that tab navigates now
        const auto rows=entry.value("tabs").toArray();
        if(rows.size()>=2&&rows.size()<=8){
            QList<Private::TabRecord> restored;
            for(const auto& row:rows)restored.append({row.toObject().value("folder").toString(),row.toObject().value("image").toString()});
            d->tabList=restored;d->activeTabIndex=qBound(0,entry.value("activeTab").toInt(),int(restored.size())-1);
            emit tabsChanged();
        }
    }
    d->restoreRequested=true;
    const auto token=d->generation,navigation=d->navigationGeneration,epoch=d->recoveryEpoch;
    const QPointer<ViewerController> alive(this);
    d->status=QCoreApplication::translate("Status","Restoring your last folder…");emit stateChanged();
    if(!alive||d->safeMode||d->explicitPositionIntent||epoch!=d->recoveryEpoch||token!=d->generation||navigation!=d->navigationGeneration)return;
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
QImage ViewerController::referenceImage() const{return d->reference;}
QString ViewerController::referenceName() const{return d->referenceLabel;}
bool ViewerController::pinReference(){
    if(!d->imageReady||d->image.isNull())return false;
    d->reference=d->image; // implicitly shared: no pixel copy
    d->referenceLabel=QFileInfo(d->path).fileName();
    emit referenceChanged();return true;
}
void ViewerController::clearReference(){
    if(d->reference.isNull())return;
    d->reference=QImage{};d->referenceLabel.clear();emit referenceChanged();
}
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
    d->status = count == 1 ? QCoreApplication::translate("Status","Copied path to clipboard.") : QCoreApplication::translate("Status","Copied %1 paths to clipboard.").arg(count);
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
    d->status = count == 1 ? QCoreApplication::translate("Status","Copied file to clipboard.") : QCoreApplication::translate("Status","Copied %1 files to clipboard.").arg(count);
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
        for (int retry = 0; retry < 6; ++retry) {
            clipboard->setImage(d->image);
            if (!clipboard->image().isNull()) break;
            QThread::msleep(40);
        }
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
        d->status = QCoreApplication::translate("Status","No photo or folder is open to reveal.");
        emit stateChanged();
        return false;
    }
    const bool ok = glancelot::PlatformIntegration::revealInFileManager(target);
    if (!ok) {
        d->status = QCoreApplication::translate("Status","Could not open file manager.");
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
        d->status = QCoreApplication::translate("Status","No folder is open to open a terminal.");
        emit stateChanged();
        return false;
    }
    const bool ok = glancelot::PlatformIntegration::openTerminal(targetDir);
    if (!ok) {
        d->status = QCoreApplication::translate("Status","Could not open terminal.");
        emit stateChanged();
        return false;
    }
    return true;
}
QString ViewerController::status() const{return d->safeMode?QStringLiteral("Safe mode · ")+d->status:d->status;}
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
// ---- F015 tabs (shape A)
QVariantList ViewerController::tabs() const {
    QVariantList rows;
    if(d->tabList.size()<2)return rows;
    for(int i=0;i<d->tabList.size();++i){
        const auto record=i==d->activeTabIndex?d->liveLocation():d->tabList[i];
        QString title=QFileInfo(record.folder).fileName();
        if(title.isEmpty())title=record.folder;
        rows.append(QVariantMap{{"title",title},{"folder",record.folder},{"image",record.image},{"active",i==d->activeTabIndex}});
    }
    return rows;
}
int ViewerController::activeTab() const{return d->tabList.size()<2?0:d->activeTabIndex;}
bool ViewerController::newTab() {
    if(d->tabList.size()>=8)return false;
    if(d->tabList.isEmpty())d->tabList.append(d->liveLocation());
    else d->tabList[d->activeTabIndex]=d->liveLocation();
    d->tabList.insert(d->activeTabIndex+1,d->liveLocation()); // a copy of where you are, next to it
    ++d->activeTabIndex;
    d->positionDirty=d->slot>0;if(d->positionDirty)d->positionTimer.start();
    emit tabsChanged();return true;
}
bool ViewerController::activateTab(int index) {
    if(d->tabList.size()<2||index<0||index>=d->tabList.size()||index==d->activeTabIndex)return false;
    d->tabList[d->activeTabIndex]=d->liveLocation();
    d->activeTabIndex=index;
    const auto target=d->tabList[index];
    d->positionDirty=d->slot>0;if(d->positionDirty)d->positionTimer.start();
    emit tabsChanged();
    if(!target.image.isEmpty()&&QFileInfo::exists(target.image))open(target.image);
    else navigateDirectory(target.folder);
    return true;
}
bool ViewerController::closeTab(int index) {
    if(d->tabList.size()<2||index<0||index>=d->tabList.size())return false;
    d->tabList[d->activeTabIndex]=d->liveLocation();
    const bool closingActive=index==d->activeTabIndex;
    d->tabList.removeAt(index);
    if(index<d->activeTabIndex)--d->activeTabIndex;
    else if(closingActive)d->activeTabIndex=qMin(index,int(d->tabList.size())-1);
    const auto target=d->tabList[d->activeTabIndex];
    if(d->tabList.size()<2){d->tabList.clear();d->activeTabIndex=0;}
    d->positionDirty=d->slot>0;if(d->positionDirty)d->positionTimer.start();
    emit tabsChanged();
    if(closingActive){
        if(!target.image.isEmpty()&&QFileInfo::exists(target.image))open(target.image);
        else navigateDirectory(target.folder);
    }
    return true;
}QVariantList ViewerController::commands() const{
    QVariantList rows;
    for(const auto& cmd:registry){
        const QString id=QString::fromStdString(cmd.id),binding=d->bindings.value(id).toString();
        // `steps` are the portable key texts of the binding, one per press, so the UI can resolve chords itself.
        QStringList steps;
        const auto parsed=QKeySequence::fromString(binding,QKeySequence::PortableText);
        for(int i=0;i<parsed.count();++i)steps.append(QKeySequence(parsed[i]).toString(QKeySequence::PortableText));
        rows.append(QVariantMap{{"id",id},{"label",QCoreApplication::translate("Commands",cmd.label.c_str())},{"binding",binding},
            {"scope",QString::fromStdString(cmd.scope)},{"hold",cmd.hold},{"repeat",cmd.repeat},{"steps",steps}});
    }
    return rows;
}
QString ViewerController::boundaryMode() const{return d->boundary;}
QString ViewerController::wheelMode() const{return d->wheel;}
QVariantMap ViewerController::wheelModifiers() const{
    QVariantMap map{{"none",d->wheel},{"ctrl",d->wheelModifiers.value("ctrl").toString()},{"shift",d->wheelModifiers.value("shift").toString()}};
    const auto alt=d->wheelModifiers.value("alt").toString();
    map.insert("alt",validWheelAction(alt)?alt:d->wheel); // unset: Alt keeps behaving like the plain wheel
    return map;
}
bool ViewerController::setWheelModifier(const QString& modifier,const QString& action){
    if(!validWheelAction(action))return false;
    if(modifier=="none"){ // the plain wheel keeps its three-value setting
        if(action=="pan-horizontal")return false;
        setWheelMode(action);return true;
    }
    if(modifier!="ctrl"&&modifier!="shift"&&modifier!="alt")return false;
    d->wheelModifiers[modifier]=action;
    // Only this modifier is merged into the stored object, so another window's other modifiers and unknown keys survive.
    const QPointer<ViewerController> alive(this);
    const bool saved=settings_detail::saveWithRetry(this,"wheelModifiers/"+modifier,[modifier,action](QJsonObject& object){
        auto stored=object.value("wheelModifiers").toObject();
        stored[modifier]=action;object["wheelModifiers"]=stored;
    },[this,modifier,action](SettingsStatus result){
        if(!result.ok()&&d->wheelModifiers.value(modifier)==QJsonValue(action)) {
            d->status=QCoreApplication::translate("Status","Wheel behavior changed for this session; settings could not be saved.");
            emit stateChanged();
        }
    });
    if(!alive)return true;
    if(!saved)d->status=QCoreApplication::translate("Status","Wheel behavior changed for this session; settings could not be saved.");
    emit stateChanged();return true;
}
QString ViewerController::keyStep(int key,int modifiers) const{
    return QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers)&~Qt::KeypadModifier,Qt::Key(key))).toString(QKeySequence::PortableText);
}
QString ViewerController::bindingProblem(const QString& id,const QString& sequence) const{
    if(!d->bindings.contains(id)||sequence.trimmed().isEmpty())return {};
    const auto parsed=QKeySequence::fromString(sequence,QKeySequence::PortableText);
    if(parsed.isEmpty())return QStringLiteral("That is not a key combination.");
    if(parsed.count()>2)return QStringLiteral("A shortcut is at most two key presses.");
    for(int i=0;i<parsed.count();++i)if(parsed[i]==QKeyCombination(Qt::NoModifier,Qt::Key_Escape))return QStringLiteral("Escape is reserved for closing views and cancelling a pending shortcut.");
    if(const auto* command=findCommand(id);command&&command->hold&&parsed.count()>1)return QStringLiteral("A hold shortcut is one key: it is released to restore the view.");
    const auto other=bindingConflict(id,sequence);
    if(!other.isEmpty()){
        const auto* command=findCommand(other);
        const QString label=command?QString::fromStdString(command->label):other;
        return QCoreApplication::translate("Status","Already used by “%1”. Clear that shortcut first, or choose another key.").arg(label);
    }
    return {};
}QString ViewerController::bindingConflict(const QString& id,const QString& sequence) const{
    if(!d->bindings.contains(id))return {};
    const auto parsed=QKeySequence::fromString(sequence,QKeySequence::PortableText);
    if(parsed.isEmpty())return {};
    for(auto it=d->bindings.begin();it!=d->bindings.end();++it)
        if(it.key()!=id&&bindingsCollide(id,parsed,it.key(),QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText)))return it.key();
    return {};
}
void ViewerController::open(const QString& value){
    trace("open:begin");
    // Consume the caller's intent once: decode may wait for registration, while
    // a nested explicit open must not inherit the navigation-only cache policy.
    const bool fromNavigation=std::exchange(d->navigationOpen,false);
    if(!fromNavigation){d->explicitPositionIntent=true;d->restorePending=false;}
    d->directoryPending=false;d->directoryError.clear();
    const QUrl url(value);const QString path=QFileInfo(url.isLocalFile()?url.toLocalFile():value).absoluteFilePath();
    if(!fromNavigation) {
        d->navigationIntents.clear();
        const QString oldDir = directory();
        const QString newDir = QFileInfo(path).absolutePath();
        if (!oldDir.isEmpty() && !newDir.isEmpty() && QDir(oldDir) != QDir(newDir)) {
            if (!d->filter.isPersistent() && d->filter.isActive()) {
                d->filter.clear();
                emit filterChanged();
            }
        }
    }
    d->removedPaths.clear();
    ++d->generation; d->cancel();
    d->cacheReuseAllowed=fromNavigation;
    d->path=path;d->loading=true;d->imageReady=false;d->status=QCoreApplication::translate("Status","Opening %1…").arg(QFileInfo(path).fileName());
    d->observe(true);d->pendingObservation.reset();
    d->decodeAwaitingBaseline=true;d->decodeStartedWithoutBaseline=false;d->decodeStart.start();
    emit stateChanged(); d->scan(path,d->generation);
}
void ViewerController::next(){d->explicitPositionIntent=true;d->navigate(1,false);}void ViewerController::previous(){d->explicitPositionIntent=true;d->navigate(-1,false);}
void ViewerController::nextFolder(){d->explicitPositionIntent=true;d->navigate(1,true);}void ViewerController::previousFolder(){d->explicitPositionIntent=true;d->navigate(-1,true);}
void ViewerController::trigger(const QString& id){
    if(id=="next")next();else if(id=="previous")previous();else if(id=="next-folder")nextFolder();else if(id=="previous-folder")previousFolder();
    else if(id=="refresh"){d->observe();d->monitor->refresh();}
    else if(id=="filmstrip")setFilmstripVisible(!d->filmstrip);
    else if(id=="sort"){const char* cycle[]={"name","modified","size","shuffle"};int at=0;for(int i=0;i<4;++i)if(sortKey()==cycle[i])at=i;setSortKey(cycle[(at+1)%4]);}
    else if(id=="sort-reverse")setSortDescending(!d->sortSpec.descending);
    else if(id=="tree")setTreeVisible(!d->tree);
    else if(id=="tab-new")newTab();else if(id=="tab-close")closeTab(d->tabList.size()<2?-1:d->activeTabIndex);
    else if(id=="tab-next"){if(d->tabList.size()>1)activateTab((d->activeTabIndex+1)%d->tabList.size());}
    else if(id=="tab-previous"){if(d->tabList.size()>1)activateTab((d->activeTabIndex+d->tabList.size()-1)%d->tabList.size());}
    else if(id=="clear-filter")clearFilter();
    else if(id=="toggle-filter-persistence")toggleFilterPersistence();
    // Storage profiles are I/O scheduler state with nothing for the view to do, so they are not re-broadcast.
    else if(id=="storage-profile-auto")setStorageProfile("auto");
    else if(id=="storage-profile-ssd")setStorageProfile("ssd");
    else if(id=="storage-profile-hdd")setStorageProfile("hdd");
    else if(id=="storage-profile-network")setStorageProfile("network");
    // Executed here only; the emit lets the view end the loupe as before any command (Main.qml has no branch for these).
    else if(id=="toggle-metadata") { toggleMetadata(); emit viewCommand(id); }
    else if(id=="animation-toggle-play-pause") { d->animationController.togglePlayPause(); emit viewCommand(id); }
    else if(id=="animation-next-frame") { d->animationController.nextFrame(); emit viewCommand(id); }
    else if(id=="animation-previous-frame") { d->animationController.previousFrame(); emit viewCommand(id); }
    else if(id=="animation-speed-up") { d->animationController.setSpeed(d->animationController.speed() * 1.5); emit viewCommand(id); }
    else if(id=="animation-speed-down") { d->animationController.setSpeed(d->animationController.speed() / 1.5); emit viewCommand(id); }
    else if(id=="animation-toggle-loop") { d->animationController.setLoop(!d->animationController.isLoop()); emit viewCommand(id); }
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
    else if(id=="presentation-toggle") { togglePresentation(); emit viewCommand(id); }
    else if(id=="presentation-mirror") { d->displayManager.setPresentationMode("mirror"); emit viewCommand(id); }
    else if(id=="presentation-pin") { pinPresentationImage(); emit viewCommand(id); }
    else if(id=="presentation-compare") { d->displayManager.compareWithPinnedOrPrevious(); emit viewCommand(id); }
    else if(id=="presentation-fit") { cyclePresentationFit(); emit viewCommand(id); }
    else if(id.startsWith("presentation-view-")) { d->displayManager.outputViewportCommand(id.mid(18)); emit viewCommand(id); }
    else if(id=="presentation-close") { closePresentation(); emit viewCommand(id); }
    else if(d->bindings.contains(id))emit viewCommand(id);
}
QString ViewerController::bindingFor(const QString& id) const{return d->bindings.value(id).toString();}
bool ViewerController::setBinding(const QString& id,const QString& sequence){
    if(!d->bindings.contains(id))return false;
    const auto parsed=QKeySequence::fromString(sequence,QKeySequence::PortableText);
    const QString normalized=parsed.toString(QKeySequence::PortableText);
    if((!sequence.trimmed().isEmpty()&&normalized.isEmpty())||!usableShortcut(parsed)){d->status=QCoreApplication::translate("Status","That shortcut is invalid or uses Escape, which is reserved for closing views and dialogs.");emit stateChanged();return false;}
    if(const auto* command=findCommand(id);command&&command->hold&&parsed.count()>1){d->status=QCoreApplication::translate("Status","A hold shortcut is a single key: it is released to restore the view.");emit stateChanged();return false;}
    if(const auto other=bindingConflict(id,normalized);!other.isEmpty()){d->status=QCoreApplication::translate("Status","Shortcut conflicts with %1 (including chord prefixes). Clear it first.").arg(other);emit stateChanged();return false;}
    struct BindingSave {QJsonObject merged;QJsonValue rollback;QString conflict;std::optional<SettingsStatus> result;bool deferred{};};
    const auto pending=std::make_shared<BindingSave>();const auto before=d->bindings;
    pending->rollback=d->confirmedBindings.value(id);
    pending->merged=before;pending->merged[id]=normalized;
    settings_detail::saveWithRetry(this,"bindings/"+id,[pending,id,normalized,parsed](QJsonObject& object) {
        auto& merged=pending->merged;auto& conflict=pending->conflict;conflict.clear();
        auto stored=object.value("bindings").toObject();
        const auto persisted=glancelot::resolveCommandBindings(stored);
        if(persisted.valid)pending->rollback=persisted.effective.value(id);
        // Validate the effective map while holding the writer lock: another window may have
        // assigned this shortcut since our settings were loaded.
        for(auto it=stored.begin();it!=stored.end();++it)
            if(it.key()!=id&&merged.contains(it.key())&&it.value().isString())merged[it.key()]=it.value();
        for(auto it=merged.begin();it!=merged.end();++it)
            if(it.key()!=id&&bindingsCollide(id,parsed,it.key(),QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText))){conflict=it.key();return;}
        if(!usableBindings(merged)){conflict="invalid saved shortcut map; reset default shortcuts";return;}
        stored[id]=normalized;object["bindings"]=stored;
    },[this,pending,before,id,normalized](SettingsStatus result){
        pending->result=result;
        if(!pending->conflict.isEmpty())d->confirmedBindings[id]=pending->rollback;
        else if(result.ok())d->confirmedBindings[id]=normalized;
        if(!pending->deferred||d->bindings.value(id)!=QJsonValue(normalized))return;
        if(!pending->conflict.isEmpty()) {
            d->bindings[id]=pending->rollback;
            d->status="Shortcut already assigned to "+pending->conflict+" in another window. Clear it first.";
        }
        else if(result.ok()) {
            // Bring in foreign changes only where no newer local edit exists.
            for(auto it=pending->merged.begin();it!=pending->merged.end();++it)
                if(it.key()!=id&&d->bindings.value(it.key())==before.value(it.key()))d->bindings[it.key()]=it.value();
            d->status="Shortcut saved.";
        }else d->status="Shortcut changed for this session; settings could not be saved.";
        const QPointer<ViewerController> alive(this);
        emit bindingsChanged();if(alive)emit stateChanged();
    });
    if(!pending->conflict.isEmpty()){d->status=QCoreApplication::translate("Status","Shortcut already assigned to %1 in another window. Clear it first.").arg(pending->conflict);emit stateChanged();return false;}
    if(pending->result&&pending->result->ok())d->bindings=pending->merged;
    d->bindings[id]=normalized;
    d->status=!pending->result?"Saving shortcut…":pending->result->ok()?"Shortcut saved.":"Shortcut changed for this session; settings could not be saved.";
    pending->deferred=true;
    emit bindingsChanged();emit stateChanged();return true;
}
void ViewerController::resetBindings(){for(const auto& cmd:registry)d->bindings[QString::fromStdString(cmd.id)]=QString::fromStdString(cmd.defaultBinding);const bool saved=d->save({"bindings"});d->status=saved?"Default shortcuts restored.":"Defaults restored for this session; settings could not be saved.";emit bindingsChanged();emit stateChanged();}
bool ViewerController::treeVisible() const{return d->tree;}
bool ViewerController::treePinned() const{return d->treePin;}
void ViewerController::setTreeVisible(bool value){if(d->tree==value)return;d->tree=value;if(!d->save({"tree"}))d->status=QCoreApplication::translate("Status","Folder tree setting changed; settings could not be saved.");emit stateChanged();if(value)emit treeShown();}
void ViewerController::setTreePinned(bool value){if(d->treePin==value)return;d->treePin=value;if(!d->save({"treePinned"}))d->status=QCoreApplication::translate("Status","Folder tree pin changed; settings could not be saved.");emit stateChanged();}
void ViewerController::cancelDirectoryNavigation() {
    d->explicitPositionIntent=true;d->restorePending=false;
    if(d->directoryPending) {
        d->navigationStop.request_stop();d->navigationStop=std::stop_source{};
        ++d->navigationGeneration;d->pendingNavigation.reset();d->navigationIntents.clear();
        if(!d->path.isEmpty())d->scan(d->path,d->generation);
    }
    d->directoryPending=false;d->directoryError.clear();emit stateChanged();
}
void ViewerController::navigateDirectory(const QString& value) {
    d->explicitPositionIntent=true;d->restorePending=false;
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
// F002: folders of the typed scope that start with the last typed segment. The listing runs off the UI thread, is
// cancelled by the next request, and its result is dropped when a newer request exists. Text only; nothing is created.
void ViewerController::completePath(const QString& text){
    d->completionStop.request_stop(); d->completionStop=std::stop_source{};
    const quint64 generation=++d->completionGeneration;
    const QString typed=QDir::fromNativeSeparators(text);
    const int cut=typed.lastIndexOf(QLatin1Char('/'));
    if(cut<0||typed.trimmed().isEmpty()){emit pathCompletions(text,{});return;}
    const QString scope=typed.left(cut+1),prefix=typed.mid(cut+1);
    const auto cancel=d->completionStop.get_token();
    auto* watcher=new QFutureWatcher<QStringList>(this);
    connect(watcher,&QFutureWatcher<QStringList>::finished,this,[this,watcher,text,generation]{
        const auto names=watcher->result();watcher->deleteLater();
        if(generation==d->completionGeneration)emit pathCompletions(text,names);
    });
    watcher->setFuture(QtConcurrent::run([scope,prefix,cancel]{
        QStringList names;
        const auto listing=childFolders(nativePath(scope),cancel,5000);
        for(const auto& folder:listing.folders){
            if(cancel.stop_requested())return QStringList{};
            const QString name=displayPath(folder.filename());
            if(name.startsWith(prefix,Qt::CaseInsensitive))names.append(name);
        }
        names.sort(Qt::CaseInsensitive);
        if(names.size()>20)names=names.mid(0,20);
        return names;
    }));
}
void ViewerController::openFolder(const QString& value){
    d->explicitPositionIntent=true;d->restorePending=false;
    d->cancelLookahead();
    d->directoryPending=false;d->directoryError.clear();
    const QUrl url(value);const QString folder=QFileInfo(url.isLocalFile()?url.toLocalFile():value).absoluteFilePath();
    if(value.trimmed().isEmpty()||folder.isEmpty())return;
    d->navigationIntents.clear();
    // Newest folder request wins: stop whatever search or step is still running and start from a fresh token.
    d->navigationStop.request_stop(); d->navigationStop=std::stop_source{};
    Navigator copy=d->nav;
    const auto cancellation=d->navigationStop.get_token();
    copy.setCancellation(cancellation);
    d->status=QCoreApplication::translate("Status","Opening folder %1…").arg(QFileInfo(folder).fileName());emit stateChanged();
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
    d->cache.clear();d->notifyCacheChanged();
    emit filesInvalidated();
    const QString folder=directory();
    const bool folderRemoved=d->removedCovers(folder);
    const bool imageRemoved=d->removedCovers(d->path);
    if(!folderRemoved&&!imageRemoved)return;
    // This window steps exactly once; a pending watcher reload or queued key repeat must not step again.
    d->pendingObservation.reset(); d->navigationIntents.clear();
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
                if(!copy.snapshot().root.empty())target.setRoot(copy.snapshot().root);
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
void ViewerController::setFilmstripVisible(bool value){if(d->filmstrip==value)return;d->filmstrip=value;if(!d->save({"filmstrip"}))d->status=QCoreApplication::translate("Status","Filmstrip setting changed; settings could not be saved.");emit stateChanged();}
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
void ViewerController::setBoundaryMode(const QString& value){if(value!="continue"&&value!="stop"&&value!="loop")return;d->boundary=value;d->nav.setBoundary(boundaryValue(value));if(!d->save({"boundary"}))d->status=QCoreApplication::translate("Status","Boundary mode changed; settings could not be saved.");emit stateChanged();}
void ViewerController::setWheelMode(const QString& value){if(value!="navigate"&&value!="zoom"&&value!="pan")return;d->wheel=value;if(!d->save({"wheel"}))d->status=QCoreApplication::translate("Status","Wheel behavior changed; settings could not be saved.");emit stateChanged();}
qreal ViewerController::textScale() const { return d->textScale; }
void ViewerController::setTextScale(qreal scale) {
    const qreal clamped = std::clamp(scale, 0.75, 2.0);
    if (qFuzzyCompare(clamped, d->textScale)) return;
    d->textScale = clamped;
    d->save({"textScale"});
    emit stateChanged();
}
bool ViewerController::highContrast() const { return d->highContrast; }
void ViewerController::setHighContrast(bool on) { if (on == d->highContrast) return; d->highContrast = on; d->save({"highContrast"}); emit stateChanged(); }
bool ViewerController::largeTargets() const { return d->largeTargets; }
void ViewerController::setLargeTargets(bool on) { if (on == d->largeTargets) return; d->largeTargets = on; d->save({"largeTargets"}); emit stateChanged(); }
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
    emit configurationReloaded();
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
    emit configurationReloaded();
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
    emit configurationReloaded();
    emit stateChanged();
    emit bindingsChanged();
    return true;
}

QString ViewerController::filterPattern() const { return d->filter.pattern; }
void ViewerController::setFilterPattern(const QString& pattern) {
    if (d->filter.pattern == pattern) return;
    d->filter.pattern = pattern;
    emit filterChanged();
}
bool ViewerController::isFilterPersistent() const { return d->filter.isPersistent(); }
void ViewerController::setFilterPersistent(bool persistent) {
    const auto mode = persistent ? glancelot::FilterPersistence::Persistent : glancelot::FilterPersistence::Temporary;
    if (d->filter.persistence == mode) return;
    d->filter.persistence = mode;
    emit filterChanged();
}
bool ViewerController::hasFilter() const { return d->filter.isActive(); }
void ViewerController::setFilter(const QString& pattern, bool persistent) {
    const auto mode = persistent ? glancelot::FilterPersistence::Persistent : glancelot::FilterPersistence::Temporary;
    if (d->filter.pattern == pattern && d->filter.persistence == mode) return;
    d->cancelLookahead();
    d->filter.pattern = pattern;
    d->filter.persistence = mode;
    emit filterChanged();
}
void ViewerController::clearFilter() {
    if (!d->filter.isActive()) return;
    d->cancelLookahead();
    d->filter.clear();
    emit filterChanged();
}
void ViewerController::toggleFilterPersistence() {
    d->cancelLookahead();
    d->filter.persistence = d->filter.isPersistent() ? glancelot::FilterPersistence::Temporary : glancelot::FilterPersistence::Persistent;
    emit filterChanged();
}
qint64 ViewerController::decodedCacheBytes() const { return d->cache.bytes(); }
int ViewerController::decodedCacheCount() const { return d->cache.count(); }
bool ViewerController::isLastDecodeCacheHit() const { return d->lastDecodeCacheHit; }
void ViewerController::clearDecodedCache() { d->cancelLookahead(); d->cache.clear(); emit cacheChanged(); }

QString ViewerController::storageProfile() const {
    return d->storageProfile;
}

void ViewerController::setStorageProfile(const QString& profile) {
    const QString p = profile.trimmed().toLower();
    d->storageProfile = (p == "ssd" || p == "hdd" || p == "rotational" || p == "network") ? p : "auto";
    IoScheduler::instance().setProfile(parseStorageProfile(d->storageProfile));
    d->save({"storageProfile"});
    emit storageProfileChanged();
    emit currentStorageTypeChanged();
    emit ioStatsChanged();
}

QString ViewerController::currentStorageType() const {
    const QString dir = directory().isEmpty() ? QDir::currentPath() : directory();
    return QString::fromLatin1(storageDeviceTypeToString(IoScheduler::instance().effectiveStorageType(dir)));
}

int ViewerController::activeIoJobs() const {
    return IoScheduler::instance().activeJobCount();
}

qint64 ViewerController::admittedIoBytes() const {
    return IoScheduler::instance().admittedBytes();
}

MetadataModel* ViewerController::metadataModel() const {
    return &d->metadataModel;
}

glancelot::AnimationController* ViewerController::animationController() const {
    return &d->animationController;
}

bool ViewerController::isMetadataVisible() const {
    return d->metadataVisible;
}

void ViewerController::setMetadataVisible(bool visible) {
    if (d->metadataVisible == visible) return;
    d->metadataVisible = visible;
    emit metadataVisibleChanged();
}

void ViewerController::toggleMetadata() {
    setMetadataVisible(!d->metadataVisible);
}


glancelot::MutationService* ViewerController::mutationService() const {
    return &d->mutationService;
}

void ViewerController::reconcileRenamedPath(const QString& oldPath, const QString& newPath) {
    if(oldPath.isEmpty()||newPath.isEmpty())return;
    const auto old=QDir::cleanPath(QDir::fromNativeSeparators(oldPath));
    const auto replacement=QDir::cleanPath(QDir::fromNativeSeparators(newPath));
    const auto remap=[&](const QString& value) -> QString {
        if(fs_watch::key(value)==fs_watch::key(old))return replacement;
        if(!value.isEmpty()&&insideRoot(old,value)) {
            const auto prefix=old.endsWith('/')?old:old+'/';
            return QDir::cleanPath(replacement+'/'+value.mid(prefix.size()));
        }
        return {};
    };
    d->cache.clear();d->notifyCacheChanged();
    const auto image=remap(d->path);
    const auto folder=remap(directory());
    if(!image.isEmpty())open(image);
    else if(!folder.isEmpty())navigateDirectory(folder);
    else {d->observe();d->monitor->refresh();}
    emit filesInvalidated();
}

void ViewerController::reconcileCreatedFolder(const QString& folderPath) {
    Q_UNUSED(folderPath);
    emit filesInvalidated();
}


glancelot::CopyMoveService* ViewerController::copyMoveService() const {
    return &d->copyMoveService;
}

glancelot::DisplayManager* ViewerController::displayManager() const {
    return &d->displayManager;
}

bool ViewerController::isPresentationActive() const {
    return d->displayManager.isPresentationActive();
}

QString ViewerController::presentationMode() const {
    return d->displayManager.presentationModeString();
}

void ViewerController::togglePresentation() {
    d->displayManager.togglePresentation();
}

void ViewerController::openPresentation(const QString& displayId, const QString& mode) {
    auto m = glancelot::PresentationMode::Mirror;
    if (mode == QStringLiteral("pinned")) m = glancelot::PresentationMode::Pinned;
    else if (mode == QStringLiteral("comparison")) m = glancelot::PresentationMode::Comparison;
    d->displayManager.openPresentation(displayId, m);
}

void ViewerController::setPresentationFitMode(const QString& mode) {
    d->displayManager.setOutputFitMode(mode);
}

void ViewerController::cyclePresentationFit() {
    static const QStringList order{QStringLiteral("shrink"), QStringLiteral("fit"), QStringLiteral("fill")};
    const int i = order.indexOf(d->displayManager.outputFitMode());
    d->displayManager.setOutputFitMode(order[(i + 1) % order.size()]);
}

void ViewerController::closePresentation() {
    d->displayManager.closePresentation();
}

void ViewerController::pinPresentationImage() {
    d->displayManager.pinCurrentImage();
}

void ViewerController::unpinPresentationImage() {
    d->displayManager.unpin();
}
