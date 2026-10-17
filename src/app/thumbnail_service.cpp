#include "glancelot/thumbnail_service.hpp"
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
    struct Job { quint64 id{}; QString path; int edge{}; quint64 epoch{}; bool cancelled{}; };
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

    Private(ThumbnailService* owner, const QString& workerProgram) : q(owner), program(workerProgram) {
        idle.setSingleShot(true);
        idle.setInterval(kIdleShutdownMs);
        QObject::connect(&idle, &QTimer::timeout, q, [this] { if (activeCount() == 0 && pending.empty()) stopWorkers(false); });
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
        worker.busy = false;
        worker.buffer.clear(); worker.bodyBytes = -1;
        if (worker.deadline) { worker.deadline->stop(); worker.deadline->deleteLater(); worker.deadline = nullptr; }
        if (QProcess* process = worker.process) {
            worker.process = nullptr;
            process->disconnect(q);
            if (process->state() == QProcess::NotRunning) { process->deleteLater(); return; }
            process->closeWriteChannel();
            process->kill();
            if (wait) { process->waitForFinished(3000); delete process; return; }
            // Deleted only after the process has really exited; shutdown() waits for any still listed here.
            retiring.append(process);
            QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, [this, process] {
                retiring.removeAll(process);
                process->deleteLater();
            });
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
            process->disconnect();
            process->waitForFinished(3000);
            delete process.data();
        }
    }
    bool deliverable(const Job& job) const { return !job.cancelled && job.epoch == epoch; }

    void finish(Worker& worker, const QImage& image, const QString& error) {
        const Job job = worker.job;
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
            std::shared_ptr<Worker> idleWorker;
            for (auto& worker : workers) if (worker->process && !worker->busy) { idleWorker = worker; break; }
            if (!idleWorker) {
                const int live = int(std::count_if(workers.begin(), workers.end(), [](const auto& w) { return w->process != nullptr; }));
                if (live >= workerCount) break;
                idleWorker = spawn();
            }
            idleWorker->job = pending.front(); pending.pop_front();
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
            idleWorker->deadline->start(deadlineMs);
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
    if (const QImage* hit = d->cache.object(Private::key(path, edge))) {
        d->immediate.insert(id, {*hit, {}});
        d->deliverImmediate(id);
        return id;
    }
    d->pending.push_back({id, path, edge, epoch, false});
    d->pump();
    return id;
}

void ThumbnailService::cancel(quint64 id) {
    d->immediate.remove(id);
    const auto it = std::find_if(d->pending.begin(), d->pending.end(), [id](const auto& job) { return job.id == id; });
    if (it != d->pending.end()) d->pending.erase(it);
    for (auto& worker : d->workers) if (worker->busy && worker->job.id == id) worker->job.cancelled = true;
    if (d->pending.empty() && d->activeCount() == 0) d->idle.start();
}

void ThumbnailService::setEpoch(quint64 epoch) {
    if (epoch == d->epoch) return;
    d->epoch = epoch;
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
    d->pending.clear(); d->immediate.clear(); d->idle.stop();
    d->stopWorkers(true);
}
