#include "selection_export_service.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariantList>
#include <QtQml/qqml.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace glancelot;

namespace {
// Registered here, not in app/main.cpp, so the app and the test harness use the same path.
void registerSelectionExportService() { qmlRegisterType<SelectionExportService>("Glancelot", 1, 0, "SelectionExportService"); }
Q_COREAPP_STARTUP_FUNCTION(registerSelectionExportService)

QString displayPath(const fs::path& path) {
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(QString::fromStdWString(path.native()));
#else
    return QFile::decodeName(path.native().c_str());
#endif
}
fs::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
// The engine converts every path to UTF-8 and throws on a name that has no such form (a lone surrogate on NTFS). Only
// that one item is lost when it is set aside here.
bool representable(const fs::path& path) {
    try {
        (void)path.u8string();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
QString describe(const char* what) { return QString::fromLocal8Bit(what); }

QString reasonName(ExportSkip reason) {
    switch (reason) {
    case ExportSkip::Missing: return QStringLiteral("missing");
    case ExportSkip::Changed: return QStringLiteral("changed");
    case ExportSkip::Duplicate: return QStringLiteral("duplicate");
    case ExportSkip::OutsideRoot: return QStringLiteral("outsideRoot");
    case ExportSkip::Unrepresentable: return QStringLiteral("unrepresentable");
    }
    return QStringLiteral("missing");
}
QString statusName(ExportStatus status) {
    switch (status) {
    case ExportStatus::Written: return QStringLiteral("written");
    case ExportStatus::Cancelled: return QStringLiteral("cancelled");
    case ExportStatus::DestinationExists: return QStringLiteral("destinationExists");
    case ExportStatus::DestinationIsItem: return QStringLiteral("destinationIsItem");
    case ExportStatus::InvalidRequest: return QStringLiteral("invalid");
    case ExportStatus::TooMany: return QStringLiteral("tooMany");
    case ExportStatus::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

// The engine's text when the OS refuses to replace the target. Error 5 is what a file held open by another app gives,
// and 32 is a sharing violation. The engine has no status of its own for this; tests pin the text with a real lock.
bool replaceWasBlocked(const std::string& message) {
    return message == "Atomic replacement failed with Windows error 5" || message == "Atomic replacement failed with Windows error 32";
}

struct Snapshot {
    std::vector<ExportItem> items;
    std::vector<SkippedItem> skipped; // set aside while snapshotting; reported with the export's own skips
};
struct ProgressState {
    std::atomic<std::size_t> done{}, total{};
    std::atomic<bool> posted{false}; // at most one queued update, however fast the engine reports
};

// Refusals the dialog can explain better than the engine can. Runs on the worker: these are file-system calls.
QString refuseDestination(const QString& text, const fs::path& destination) {
    if (text.trimmed().isEmpty()) return QStringLiteral("destinationEmpty");
    if (!destination.is_absolute()) return QStringLiteral("destinationRelative");
    std::error_code error;
    const auto status = fs::status(destination, error);
    if (fs::is_directory(status)) return QStringLiteral("destinationIsFolder");
    if (!fs::is_directory(fs::status(destination.parent_path(), error))) return QStringLiteral("folderMissing");
    if (!fs::exists(status)) return {};
    // Replacing is only ever offered for a file that can be a previous export, so the Replace button can never
    // overwrite a photo that merely was not part of this selection.
    const QString suffix = QFileInfo(text).suffix().toLower();
    if (suffix != QLatin1String("csv") && suffix != QLatin1String("txt")) return QStringLiteral("destinationProtected");
    return {};
}
}

struct SelectionExportService::Private {
    Engine engine = [](const ExportRequest& request, std::stop_token cancel, const Progress& progress) {
        return exportSelection(request, cancel, progress);
    };
    std::size_t maxItems = ExportRequest{}.maxItems;
    std::jthread worker;
    bool running{};
    quint64 lastId{};
    std::function<void(std::stop_token)> pending; // the newest job that waits for a superseded one; never more than one
    std::shared_ptr<const Snapshot> snapshot;
};

SelectionExportService::SelectionExportService(QObject* parent) : QObject(parent), d(std::make_unique<Private>()) {}
SelectionExportService::~SelectionExportService() {
    d->pending = nullptr;
    if (d->worker.joinable()) {
        d->worker.request_stop();
        d->worker.join();
    }
}
bool SelectionExportService::busy() const { return d->running; }
void SelectionExportService::setEngine(Engine engine) { d->engine = std::move(engine); }
void SelectionExportService::setMaxItems(std::size_t maxItems) { d->maxItems = maxItems; }

QString SelectionExportService::defaultFolder() const {
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}
QString SelectionExportService::localPath(const QUrl& url) const { return QDir::toNativeSeparators(url.toLocalFile()); }
QUrl SelectionExportService::folderUrl(const QString& path) const { return QUrl::fromLocalFile(path); }

void SelectionExportService::workerDone() {
    d->worker = {}; // the worker's last act was posting this call, so the join is immediate
    d->running = false;
    if (d->pending) {
        d->running = true;
        d->worker = std::jthread(std::exchange(d->pending, nullptr));
    } else {
        emit busyChanged();
    }
}

quint64 SelectionExportService::prepare(const QStringList& paths) {
    const quint64 id = ++d->lastId;
    d->snapshot.reset();
    const std::size_t limit = d->maxItems;
    auto job = [this, id, paths, limit](std::stop_token cancel) {
        auto snapshot = std::make_shared<Snapshot>();
        QVariantMap summary{{"status", "ready"}, {"maxItems", qulonglong(limit)}};
        try {
            int files = 0, folders = 0;
            if (std::size_t(paths.size()) > limit) {
                summary["status"] = "tooMany";
            } else {
                snapshot->items.reserve(std::size_t(paths.size()));
                for (const QString& text : paths) {
                    if (cancel.stop_requested()) { summary["status"] = "cancelled"; break; }
                    const fs::path path = nativePath(text);
                    if (!representable(path)) { snapshot->skipped.push_back({path, ExportSkip::Unrepresentable}); continue; }
                    std::error_code error;
                    const auto target = snapshotTarget(path, error);
                    if (!target || error) { snapshot->skipped.push_back({path, ExportSkip::Missing}); continue; }
                    ExportItem item;
                    item.path = target->path;
                    item.kind = target->kind;
                    item.identity = target->identity;
                    item.bytes = target->bytes;
                    item.modified = target->modified;
                    snapshot->items.push_back(std::move(item));
                    ++(target->kind == TargetKind::Folder ? folders : files);
                }
            }
            summary["total"] = paths.size();
            summary["files"] = files;
            summary["folders"] = folders;
            summary["unavailable"] = qulonglong(snapshot->skipped.size());
        } catch (const std::exception& failure) {
            snapshot->items.clear();
            summary["status"] = "error";
            summary["detail"] = describe(failure.what());
        } catch (...) {
            snapshot->items.clear();
            summary["status"] = "error";
        }
        QMetaObject::invokeMethod(this, [this, id, summary, snapshot] {
            const bool current = id == d->lastId;
            if (current && summary.value("status") == QLatin1String("ready")) d->snapshot = snapshot;
            workerDone();
            if (current) emit prepared(id, summary);
        }, Qt::QueuedConnection);
    };
    if (d->running) {
        d->worker.request_stop();
        d->pending = std::move(job); // starts when the superseded job has reported
    } else {
        d->running = true;
        d->worker = std::jthread(std::move(job));
        emit busyChanged();
    }
    return id;
}

quint64 SelectionExportService::start(const QVariantMap& options) {
    if (d->running || !d->snapshot) return 0;
    const quint64 id = ++d->lastId;
    auto job = [this, id, options, snapshot = d->snapshot, engine = d->engine, limit = d->maxItems](std::stop_token cancel) {
        const QString destinationText = options.value("destination").toString().trimmed();
        QVariantMap outcome{{"destination", destinationText}, {"written", 0}};
        std::vector<SkippedItem> skipped = snapshot->skipped;
        auto reportProgress = std::make_shared<ProgressState>();
        try {
            const fs::path destination = nativePath(destinationText).lexically_normal();
            QString refusal = refuseDestination(destinationText, destination);
            if (refusal == QLatin1String("destinationProtected")) {
                for (const auto& item : snapshot->items) { // the more useful explanation, where it applies
                    std::error_code same;
                    if (fs::equivalent(item.path, destination, same) && !same) { refusal = QStringLiteral("destinationIsItem"); break; }
                }
            }
            if (!refusal.isEmpty()) {
                outcome["status"] = refusal;
            } else {
                ExportRequest request;
                request.generation = id;
                request.items = snapshot->items;
                request.destination = destination;
                request.format = options.value("format") == QLatin1String("paths") ? ExportFormat::PathList : ExportFormat::Csv;
                if (options.value("relative").toBool()) {
                    request.pathPolicy = PathPolicy::RelativeToRoot;
                    request.root = nativePath(options.value("root").toString());
                }
                request.neutralizeFormulas = options.value("neutralizeFormulas", true).toBool();
                request.overwrite = options.value("overwrite").toBool();
                request.keepDestinationWhenEmpty = true; // the check belongs to the engine: only it knows the last item's fate
                request.maxItems = limit;
                const Progress onProgress = [this, id, reportProgress](std::size_t done, std::size_t total) {
                    reportProgress->done = done;
                    reportProgress->total = total;
                    if (reportProgress->posted.exchange(true)) return;
                    QMetaObject::invokeMethod(this, [this, id, reportProgress] {
                        reportProgress->posted = false;
                        if (id == d->lastId) emit progress(id, int(reportProgress->done), int(reportProgress->total));
                    }, Qt::QueuedConnection);
                };
                ExportResult result = engine(request, cancel, onProgress);
                outcome["status"] = statusName(result.status);
                outcome["replacing"] = request.overwrite;
                if (result.status == ExportStatus::Written && result.written == 0) outcome["status"] = QStringLiteral("nothingToExport");
                else if (result.status == ExportStatus::Failed && request.overwrite && replaceWasBlocked(result.message)) outcome["status"] = QStringLiteral("destinationLocked");
                outcome["written"] = qulonglong(result.written);
                outcome["detail"] = describe(result.message.c_str());
                skipped.insert(skipped.end(), result.skipped.begin(), result.skipped.end());
            }
        } catch (const std::exception& failure) {
            outcome["status"] = "error";
            outcome["detail"] = describe(failure.what());
            outcome["written"] = 0;
        } catch (...) {
            outcome["status"] = "error";
            outcome["written"] = 0;
        }
        QVariantList reported;
        const std::size_t shown = std::min<std::size_t>(skipped.size(), kMaxReportedSkips);
        reported.reserve(qsizetype(shown));
        for (std::size_t i = 0; i < shown; ++i) {
            const QString text = displayPath(skipped[i].path);
            reported.push_back(QVariantMap{{"path", text}, {"name", text.mid(text.lastIndexOf(QLatin1Char('/')) + 1)},
                                           {"reason", reasonName(skipped[i].reason)}});
        }
        outcome["skipped"] = reported;
        outcome["skippedCount"] = qulonglong(skipped.size());
        QMetaObject::invokeMethod(this, [this, id, outcome] {
            workerDone();
            if (id == d->lastId) emit finished(id, outcome);
        }, Qt::QueuedConnection);
    };
    d->running = true;
    d->worker = std::jthread(std::move(job));
    emit busyChanged();
    return id;
}

void SelectionExportService::cancel() {
    if (d->running) d->worker.request_stop();
}
