#include "../core/perf_context.hpp"
#include "glancelot/thumbnail_service.hpp"
#include "glancelot/io_scheduler.hpp"
#include "io_job_lease.hpp"

using namespace glancelot;
#include <QCache>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

namespace {
constexpr int kMaxPathChars = 8192;
constexpr int kMaxHeaderBytes = 4096;
constexpr qint64 kMaxThumbBytes = qint64(ThumbnailService::kMaxEdge) * ThumbnailService::kMaxEdge * 4;
constexpr int kIdleShutdownMs = 30000;
}

struct ThumbnailService::Private {
    struct Job { quint64 id{}; QString path; int edge{}; quint64 epoch{}; bool cancelled{}; qint64 queued{-1}, started{-1}; std::shared_ptr<glancelot::detail::IoJobLease> admission; };
    struct Worker {
        QProcess* process{};
        QTimer* deadline{};
        bool busy{};
        Job job;
        QByteArray buffer;
        // Body state: after a "thumb" header, exactly bodyBytes of RGBA follow.
        qint64 bodyBytes{-1};
        int width{}, height{};
    };
    struct Immediate { QImage image; QString error; };

    ThumbnailService* q;
    QString program;
    quint64 nextId{1}, epoch{1};
    int workerCount{1}, deadlineMs{8000};
    std::deque<Job> pending;
    std::vector<std::shared_ptr<Worker>> workers;
    QList<QPointer<QProcess>> retiring;
    QHash<quint64, Immediate> immediate; // cache hits and rejected paths awaiting queued delivery
    QCache<QString, QImage> cache{int(kCacheBytes)};
    QTimer idle;
    bool pumpQueued{};

    Private(ThumbnailService* owner, const QString& workerProgram) : q(owner), program(workerProgram) {
        idle.setSingleShot(true);
        idle.setInterval(kIdleShutdownMs);
        QObject::connect(&idle, &QTimer::timeout, q, [this] { if (activeCount() == 0 && pending.empty()) stopWorkers(false); });
        QObject::connect(&IoScheduler::instance(),&IoScheduler::admissionAvailable,q,[this]{schedulePump();});
        QObject::connect(&IoScheduler::instance(),&IoScheduler::profileChanged,q,[this]{schedulePump();});
    }
    void schedulePump() {
        if(pumpQueued)return;pumpQueued=true;
        QTimer::singleShot(0,q,[this]{pumpQueued=false;pump();});
    }
    static QString key(const QString& path, int edge) { return QString::number(edge) + '|' + path; }
    int activeCount() const { return int(std::count_if(workers.begin(), workers.end(), [](const auto& w) { return w->busy; })); }

    void deliverImmediate(quint64 id) {
        QTimer::singleShot(0, q, [this, id] {
            const auto it = immediate.find(id);
            if (it == immediate.end()) return; // cancelled or epoch advanced
            const auto value = it.value();
            immediate.erase(it);
            if (value.error.isEmpty()) emit q->ready(id, value.image); else emit q->failed(id, value.error);
        });
    }
    void retire(Worker& worker, bool wait) {
        auto admission=std::move(worker.job.admission);worker.job={};
        worker.busy = false;
        worker.buffer.clear(); worker.bodyBytes = -1;
        if (worker.deadline) { worker.deadline->stop(); worker.deadline->deleteLater(); worker.deadline = nullptr; }
        if (QProcess* process = worker.process) {
            worker.process = nullptr;
            process->disconnect(q);
            if (process->state() == QProcess::NotRunning) { admission.reset();process->deleteLater(); return; }
            process->closeWriteChannel();
            process->kill();
            if (wait&&process->waitForFinished(3000)) { admission.reset();delete process;return; }
            // Deleted only after the process has really exited; shutdown() waits for any still listed here.
            retiring.append(process);
            glancelot::detail::retireIoProcess(process,std::move(admission),q,[this,process]{retiring.removeAll(process);schedulePump();});
        }
    }
    void stopWorkers(bool wait) {
        for (auto& worker : workers) retire(*worker, wait);
        workers.clear();
        if (!wait) return;
        const auto leftovers = retiring;
        retiring.clear();
        for (const QPointer<QProcess>& process : leftovers) {
            if (!process) continue;
            process->disconnect(q); // Keep the independent reservation-release connection.
            if(process->state()==QProcess::NotRunning||process->waitForFinished(3000)) {
                if(process)delete process.data();
            }
        }
    }
    bool deliverable(const Job& job) const { return !job.cancelled && job.epoch == epoch; }

    void finish(Worker& worker, const QImage& image, const QString& error) {
        Job job = worker.job;worker.job={};job.admission.reset();
        const auto flags = std::uint8_t((job.cancelled ? glancelot::perf::Cancelled : 0)
            | (job.epoch != epoch ? glancelot::perf::Stale : 0) | (!error.isEmpty() ? glancelot::perf::Failed : 0));
        glancelot::perf::detail::finish(glancelot::perf::Stage::ThumbnailDecode,job.epoch,job.started,flags,std::uint64_t(image.sizeInBytes()));
        worker.busy = false; worker.buffer.clear(); worker.bodyBytes = -1;
        if (worker.deadline) worker.deadline->stop();
        if (deliverable(job)) {
            if (error.isEmpty()) {
                cache.insert(key(job.path, job.edge), new QImage(image), int(std::min<qint64>(image.sizeInBytes(), kCacheBytes)));
                emit q->ready(job.id, image);
            } else emit q->failed(job.id, error);
        }
        pump();
    }
    // The worker is unusable (crash, deadline, protocol violation): replace it; the job fails, the queue continues.
    void breakWorker(Worker& worker, const QString& error) {
        const Job job = worker.job;
        const bool hadJob = worker.busy;
        if (hadJob) glancelot::perf::detail::finish(glancelot::perf::Stage::ThumbnailDecode,job.epoch,job.started,
            std::uint8_t(glancelot::perf::Failed | (job.cancelled ? glancelot::perf::Cancelled : 0) | (job.epoch != epoch ? glancelot::perf::Stale : 0)));
        retire(worker, false);
        if (hadJob && deliverable(job)) emit q->failed(job.id, error);
        // Defer removal so pump() cannot reuse this slot and the vector is not mutated mid-iteration.
        QTimer::singleShot(0, q, [this] {
            workers.erase(std::remove_if(workers.begin(), workers.end(), [](const auto& w) { return !w->process; }), workers.end());
            pump();
        });
    }
    void consume(Worker& worker) {
        for (;;) {
            if (!worker.busy) { worker.buffer.clear(); return; }
            if (worker.bodyBytes < 0) {
                const auto newline = worker.buffer.indexOf('\n');
                if (newline < 0) { if (worker.buffer.size() > kMaxHeaderBytes) breakWorker(worker, "Thumbnail worker sent an oversized response."); return; }
                const auto line = worker.buffer.left(newline);
                worker.buffer.remove(0, newline + 1);
                QJsonParseError parse;
                const auto message = QJsonDocument::fromJson(line, &parse).object();
                if (parse.error != QJsonParseError::NoError || message.value("id").toInteger(-1) != qint64(worker.job.id)) {
                    breakWorker(worker, "Thumbnail worker sent an invalid response."); return;
                }
                const auto phase = message.value("phase").toString();
                if (phase == "error") { finish(worker, {}, message.value("error").toString("Thumbnail failed.")); return; }
                const qint64 w = message.value("width").toInteger(), h = message.value("height").toInteger(), bytes = message.value("bytes").toInteger();
                if (phase != "thumb" || w < 1 || h < 1 || w > worker.job.edge || h > worker.job.edge || bytes != w * h * 4 || bytes > kMaxThumbBytes) {
                    breakWorker(worker, "Thumbnail worker returned invalid geometry."); return;
                }
                worker.bodyBytes = bytes; worker.width = int(w); worker.height = int(h);
            }
            if (worker.buffer.size() < worker.bodyBytes) return;
            // Copy into an image-owned, aligned buffer; the QByteArray offset after remove() may be unaligned.
            QImage owned(worker.width, worker.height, QImage::Format_RGBA8888);
            if (!owned.isNull()) {
                const char* source = worker.buffer.constData();
                for (int y = 0; y < worker.height; ++y) std::memcpy(owned.scanLine(y), source + qint64(y) * worker.width * 4, size_t(worker.width) * 4);
            }
            worker.buffer.remove(0, worker.bodyBytes);
            if (owned.isNull()) { breakWorker(worker, "Cannot allocate the thumbnail."); return; }
            finish(worker, owned, {});
            return; // finish() may pump and start another job on this worker; its data arrives asynchronously
        }
    }
    // Lambdas hold weak references: a queued signal may arrive after breakWorker() retired the slot.
    std::shared_ptr<Worker> spawn() {
        auto worker = std::make_shared<Worker>();
        const std::weak_ptr<Worker> weak = worker;
        worker->process = new QProcess(q);
        worker->deadline = new QTimer(q);
        worker->deadline->setSingleShot(true);
        QObject::connect(worker->deadline, &QTimer::timeout, q, [this, weak] {
            if (const auto w = weak.lock()) breakWorker(*w, "Thumbnail timed out.");
        });
        QObject::connect(worker->process, &QProcess::readyReadStandardOutput, q, [this, weak] {
            const auto w = weak.lock();
            if (!w || !w->process) return;
            w->buffer += w->process->readAllStandardOutput();
            if (w->buffer.size() > kMaxHeaderBytes + kMaxThumbBytes) { breakWorker(*w, "Thumbnail worker exceeded its response limit."); return; }
            consume(*w);
        });
        QObject::connect(worker->process, &QProcess::errorOccurred, q, [this, weak](QProcess::ProcessError error) {
            const auto w = weak.lock();
            if (w && w->process && error == QProcess::FailedToStart) breakWorker(*w, missingMessage());
        }, Qt::QueuedConnection);
        QObject::connect(worker->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), q, [this, weak](int, QProcess::ExitStatus) {
            const auto w = weak.lock();
            if (w && w->process) breakWorker(*w, "Thumbnail worker exited.");
        });
        worker->process->setStandardErrorFile(QProcess::nullDevice()); // never leave worker stderr undrained
        worker->process->start(program, {"--thumbnail-server"});
        workers.push_back(worker);
        return worker;
    }
    static QString missingMessage() { return "Thumbnail worker is missing. Keep glancelot-decoder next to the viewer."; }
    void pump() {
        while (!pending.empty()) {
            const auto& candidate = pending.front();
            const auto targetPath = candidate.path;
            const int storageWorkerCap = IoScheduler::instance().maxConcurrentBackgroundWorkers(targetPath);
            const int effectiveMax = std::min(workerCount, storageWorkerCap);
            const int deadline = std::min(deadlineMs, IoScheduler::instance().recommendedThumbnailDeadlineMs(targetPath));

            std::shared_ptr<Worker> idleWorker;
            for (auto& worker : workers) if (worker->process && !worker->busy) { idleWorker = worker; break; }
            if (!idleWorker) {
                const int live = int(std::count_if(workers.begin(), workers.end(), [](const auto& w) { return w->process != nullptr; }));
                if (live >= effectiveMax) break;
                idleWorker = spawn();
            }

            const qint64 estimatedBytes = qint64(candidate.edge) * candidate.edge * 4;
            auto admission=glancelot::detail::admitIoJob(JobPriority::Thumbnail,estimatedBytes,targetPath);
            if (!admission) {
                break;
            }

            idleWorker->job = pending.front(); pending.pop_front();
            idleWorker->job.admission=std::move(admission);
            glancelot::perf::detail::finish(glancelot::perf::Stage::ThumbnailQueue,idleWorker->job.epoch,idleWorker->job.queued);
            idleWorker->job.started = glancelot::perf::detail::begin();
            idleWorker->busy = true; idleWorker->buffer.clear(); idleWorker->bodyBytes = -1;
            if (idleWorker->process->state() == QProcess::NotRunning) {
                // start() failed synchronously; fail this job on the next turn so callers never see a re-entrant signal.
                const std::weak_ptr<Worker> weak = idleWorker;
                QTimer::singleShot(0, q, [this, weak] { if (const auto w = weak.lock()) if (w->process) breakWorker(*w, missingMessage()); });
                continue;
            }
            const auto request = QJsonDocument(QJsonObject{{"id", double(idleWorker->job.id)}, {"path", idleWorker->job.path}, {"edge", idleWorker->job.edge}})
                                     .toJson(QJsonDocument::Compact) + '\n';
            idleWorker->process->write(request);
            idleWorker->deadline->start(deadline);
        }
        if (pending.empty() && activeCount() == 0) idle.start();
        else idle.stop();
    }
};

ThumbnailService::ThumbnailService(const QString& workerProgram, QObject* parent) : QObject(parent), d(std::make_unique<Private>(this, workerProgram)) {}
ThumbnailService::~ThumbnailService() { shutdown(); }

quint64 ThumbnailService::request(const QString& path, int edge, quint64 epoch) {
    if (epoch != d->epoch) return 0;
    edge = std::clamp(edge, kMinEdge, kMaxEdge);
    if (int(d->pending.size()) + d->activeCount() + int(d->immediate.size()) >= kMaxQueue) return 0;
    const quint64 id = d->nextId++;
    if (path.isEmpty() || path.size() > kMaxPathChars) {
        d->immediate.insert(id, {{}, "Invalid thumbnail path."});
        d->deliverImmediate(id);
        return id;
    }
    const QImage* hit = [&] {
        glancelot::perf::ScopedSpan lookup(glancelot::perf::Stage::CacheLookup,epoch);
        const auto* value = d->cache.object(Private::key(path, edge));
        if (value) { lookup.setFlags(glancelot::perf::CacheHit); lookup.setBytes(std::uint64_t(value->sizeInBytes())); }
        return value;
    }();
    if (hit) {
        d->immediate.insert(id, {*hit, {}});
        d->deliverImmediate(id);
        return id;
    }
    d->pending.push_back({id, path, edge, epoch, false, glancelot::perf::detail::begin(), -1});
    d->pump();
    return id;
}

void ThumbnailService::cancel(quint64 id) {
    d->immediate.remove(id);
    const auto it = std::find_if(d->pending.begin(), d->pending.end(), [id](const auto& job) { return job.id == id; });
    if (it != d->pending.end()) {
        glancelot::perf::detail::finish(glancelot::perf::Stage::ThumbnailQueue,it->epoch,it->queued,glancelot::perf::Cancelled);
        d->pending.erase(it);
    }
    for (auto& worker : d->workers) if (worker->busy && worker->job.id == id) worker->job.cancelled = true;
    if (d->pending.empty() && d->activeCount() == 0) d->idle.start();
}

void ThumbnailService::setEpoch(quint64 epoch) {
    if (epoch == d->epoch) return;
    d->epoch = epoch;
    for (const auto& job : d->pending) {
        glancelot::perf::detail::finish(glancelot::perf::Stage::ThumbnailQueue,job.epoch,job.queued,glancelot::perf::Stale);
    }
    d->cache.clear(); d->pending.clear(); d->immediate.clear();
    // In-flight jobs finish on their workers but fail the deliverable() epoch check.
}
quint64 ThumbnailService::epoch() const { return d->epoch; }
void ThumbnailService::setWorkerCount(int count) { d->workerCount = std::clamp(count, 1, kMaxWorkers); }
void ThumbnailService::setDeadlineMs(int milliseconds) { d->deadlineMs = std::max(50, milliseconds); }
int ThumbnailService::pending() const { return int(d->pending.size()); }
int ThumbnailService::active() const { return d->activeCount(); }
qint64 ThumbnailService::cacheBytes() const { return d->cache.totalCost(); }
void ThumbnailService::shutdown() {
    for (const auto& job : d->pending) {
        glancelot::perf::detail::finish(glancelot::perf::Stage::ThumbnailQueue,job.epoch,job.queued,glancelot::perf::Cancelled);
    }
    for (const auto& worker : d->workers) if (worker->busy) {
        glancelot::perf::detail::finish(glancelot::perf::Stage::ThumbnailDecode,worker->job.epoch,worker->job.started,glancelot::perf::Cancelled);
    }
    d->pending.clear(); d->immediate.clear(); d->idle.stop();
    d->stopWorkers(true);
}
