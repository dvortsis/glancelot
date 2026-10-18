#include "glancelot/file_operation_service.hpp"
#include "glancelot/file_operations.hpp"
#include "platform_trash.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <optional>
#include <stop_token>
#include <system_error>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <sherrors.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace fs = std::filesystem;
using namespace glancelot;

namespace {
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
// After a reported success the original must be gone (or be a different item that appeared since).
bool stillPresent(const MutationTarget& target) {
    std::error_code error;
    const auto now = snapshotTarget(target.path, error);
    return error || (now && now->identity == target.identity);
}

#ifdef Q_OS_WIN
class RecycleOnlySink final : public IFileOperationProgressSink {
public:
    bool refused{}, posted{}, recycled{};
    HRESULT result{S_OK};
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG left = --references; if (left == 0) delete this; return left; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IFileOperationProgressSink)) *object = static_cast<IFileOperationProgressSink*>(this);
        if (!*object) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return E_FAIL; } // never copies
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    // The safety gate: the shell asks before deleting each item; anything it would not recycle is refused.
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags, IShellItem*) override {
        const HRESULT decision = HRESULT(platform_trash::preDeleteDecision(flags));
        if (FAILED(decision)) refused = true;
        return decision;
    }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT hrDelete, IShellItem* newlyCreated) override {
        posted = true; result = hrDelete;
        recycled = newlyCreated != nullptr; // null means the item was deleted outright, not recycled
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }
private:
    ULONG references{1};
};
template<class T> struct ComRef {
    T* p{};
    ~ComRef() { if (p) p->Release(); }
    T** out() { return &p; }
};
std::string hresultText(HRESULT hr) {
    std::string text = std::system_category().message(int(hr));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.')) text.pop_back();
    const std::string code = "0x" + QString::number(quint32(hr), 16).toStdString();
    return text.empty() || text == "unknown error" ? "shell error " + code : text + " (" + code + ")";
}
DeleteOutcome outcomeFor(HRESULT hr) {
    switch (hr) {
    case __HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED): case COPYENGINE_E_ACCESS_DENIED_SRC: // E_ACCESSDENIED is the first
    case COPYENGINE_E_REQUIRES_ELEVATION: case __HRESULT_FROM_WIN32(ERROR_WRITE_PROTECT):
        return DeleteOutcome::Denied;
    case __HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION): case __HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION):
    case COPYENGINE_E_SHARING_VIOLATION_SRC: case __HRESULT_FROM_WIN32(ERROR_USER_MAPPED_FILE):
        return DeleteOutcome::InUse;
    case __HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND): case __HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND): case COPYENGINE_E_PATH_NOT_FOUND_SRC:
        return DeleteOutcome::Missing;
    case COPYENGINE_E_RECYCLE_BIN_NOT_FOUND:
        return DeleteOutcome::TrashUnavailable;
    default:
        return DeleteOutcome::Failed;
    }
}
class ShellTrash final : public TrashBackend {
public:
    DeleteResult moveToTrash(const MutationTarget& target) override {
        DeleteResult out{target.path, DeleteOutcome::Failed, {}};
        // IFileOperation requires a single-threaded apartment; each call owns its own initialization.
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (init == RPC_E_CHANGED_MODE) { out.message = "the shell file operation is unavailable on this thread"; return out; }
        struct Uninit { bool on; ~Uninit() { if (on) CoUninitialize(); } } uninit{SUCCEEDED(init)};
        ComRef<IFileOperation> operation;
        HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(operation.out()));
        if (FAILED(hr)) { out.message = hresultText(hr); return out; }
        // No UI of any kind; recycle on delete; fail early. FOF_NOCONFIRMATION also answers the shell's "delete
        // permanently instead?" question with yes, which is why the sink's PreDeleteItem refusal is mandatory.
        hr = operation.p->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI
                                            | FOFX_EARLYFAILURE | FOF_NOCONFIRMMKDIR);
        if (FAILED(hr)) { out.message = hresultText(hr); return out; }
        ComRef<IShellItem> item;
        hr = SHCreateItemFromParsingName(target.path.c_str(), nullptr, IID_PPV_ARGS(item.out()));
        if (FAILED(hr)) { out.outcome = outcomeFor(hr); out.message = hresultText(hr); return out; }
        auto* sink = new RecycleOnlySink;
        struct Release { RecycleOnlySink* s; ~Release() { s->Release(); } } release{sink};
        hr = operation.p->DeleteItem(item.p, sink);
        if (FAILED(hr)) { out.message = hresultText(hr); return out; }
        hr = operation.p->PerformOperations();
        BOOL aborted = FALSE;
        operation.p->GetAnyOperationsAborted(&aborted);
        if (sink->refused) {
            out.outcome = DeleteOutcome::TrashUnavailable;
            out.message = "the Recycle Bin cannot take this item (no Recycle Bin on this drive, or the item is too large)";
            return out;
        }
        if (sink->posted && FAILED(sink->result)) { out.outcome = outcomeFor(sink->result); out.message = hresultText(sink->result); return out; }
        if (FAILED(hr)) { out.outcome = outcomeFor(hr); out.message = hresultText(hr); return out; }
        if (aborted || !sink->posted) { out.message = "the shell stopped the operation"; return out; }
        if (!sink->recycled) { out.message = "Windows did not report a Recycle Bin item"; return out; }
        if (stillPresent(target)) { out.message = "the item is still in place after the move"; return out; }
        out.outcome = DeleteOutcome::Trashed;
        return out;
    }
};
#else
class FreedesktopTrash final : public TrashBackend {
public:
    DeleteResult moveToTrash(const MutationTarget& target) override {
        DeleteResult out{target.path, DeleteOutcome::Failed, {}};
        const QString path = displayPath(target.path);
        QFile file(path);
        // Qt's freedesktop implementation renames into $XDG_DATA_HOME/Trash or the volume's .Trash-$uid; it never
        // copies, and a cross-device or permission failure leaves the original untouched.
        if (file.moveToTrash()) {
            if (stillPresent(target)) { out.message = "a copy reached the Trash but the original could not be removed"; return out; }
            out.outcome = DeleteOutcome::Trashed;
            return out;
        }
        std::error_code error;
        if (!snapshotTarget(target.path, error) && !error) { out.outcome = DeleteOutcome::Missing; out.message = "it no longer exists"; return out; }
        const QFileInfo parent(QFileInfo(path).absolutePath());
        if (!parent.isWritable()) { out.outcome = DeleteOutcome::Denied; out.message = "you don't have permission to change its folder"; return out; }
        out.outcome = DeleteOutcome::TrashUnavailable;
        out.message = (file.errorString().isEmpty() ? QStringLiteral("no usable Trash for this location") : file.errorString()).toStdString();
        return out;
    }
};
#endif

QString outcomeName(DeleteOutcome outcome) {
    switch (outcome) {
    case DeleteOutcome::Trashed: return "trashed";
    case DeleteOutcome::Missing: return "missing";
    case DeleteOutcome::Changed: return "changed";
    case DeleteOutcome::Denied: return "denied";
    case DeleteOutcome::InUse: return "inUse";
    case DeleteOutcome::TrashUnavailable: return "trashUnavailable";
    case DeleteOutcome::Cancelled: return "cancelled";
    default: return "failed";
    }
}
QString reasonName(PlanRejection reason) {
    switch (reason) {
    case PlanRejection::Missing: return "missing";
    case PlanRejection::Root: return "root";
    case PlanRejection::Duplicate: return "duplicate";
    case PlanRejection::ReadOnlyVolume: return "readOnlyVolume";
    case PlanRejection::Special: return "special";
    default: return "inaccessible";
    }
}
QString leafName(const fs::path& path) {
    const QString leaf = displayPath(path.filename());
    return leaf.isEmpty() ? displayPath(path) : leaf;
}
}

namespace platform_trash {
std::unique_ptr<TrashBackend> make() {
#ifdef Q_OS_WIN
    return std::make_unique<ShellTrash>();
#else
    return std::make_unique<FreedesktopTrash>();
#endif
}
std::string trashName() {
#ifdef Q_OS_WIN
    return "Recycle Bin";
#else
    return "Trash";
#endif
}
#ifdef Q_OS_WIN
long preDeleteDecision(unsigned long transferFlags) { return (transferFlags & TSF_DELETE_RECYCLE_IF_POSSIBLE) ? S_OK : E_FAIL; }
#endif
}

struct FileOperationService::Private {
    FileOperationService* q;
    std::shared_ptr<TrashBackend> backend;
    // Heap-allocated so a worker wedged in the OS (dead share) can be abandoned at teardown; jobs own their captures.
    QThreadPool* pool = new QThreadPool;
    quint64 nextId{};
    quint64 planningId{}, readyId{}, executingId{};
    std::stop_source planStop, executeStop;
    std::optional<DeletionPlan> ready;
    QString readyOrigin;
    bool busy{};

    Private(FileOperationService* owner, std::unique_ptr<TrashBackend> trash) : q(owner), backend(std::move(trash)) {
        pool->setMaxThreadCount(2); // one plan and one execution; a wedged plan never blocks an execution
    }
    ~Private() {
        planStop.request_stop(); executeStop.request_stop();
        // An item already handed to the shell completes; nothing new starts. Give it a moment, then abandon.
        if (pool->waitForDone(5000)) delete pool;
    }
    void setBusy(bool value) { if (busy != value) { busy = value; emit q->busyChanged(); } }
    QVariantMap summary(quint64 id, const QString& origin, const DeletionPlan& plan) const {
        QVariantList targets, rejected;
        int files = 0, folders = 0;
        quint64 contentFiles = 0, contentFolders = 0;
        bool complete = true;
        for (const auto& t : plan.targets) {
            const bool folder = t.kind == TargetKind::Folder;
            (folder ? folders : files) += 1;
            contentFiles += t.contentFiles; contentFolders += t.contentFolders;
            complete = complete && t.contentCountComplete;
            targets.append(QVariantMap{{"path", displayPath(t.path)}, {"name", leafName(t.path)}, {"kind", folder ? "folder" : "file"},
                                       {"link", t.link}, {"bytes", double(t.bytes)}, {"contentFiles", double(t.contentFiles)},
                                       {"contentFolders", double(t.contentFolders)}, {"contentCountComplete", t.contentCountComplete}});
        }
        for (const auto& r : plan.rejected)
            rejected.append(QVariantMap{{"path", displayPath(r.path)}, {"name", leafName(r.path)}, {"reason", reasonName(r.reason)},
                                        {"message", QString::fromStdString(r.message)}});
        return {{"id", id}, {"origin", origin}, {"targets", targets}, {"rejected", rejected}, {"fileCount", files},
                {"folderCount", folders}, {"contentFiles", double(contentFiles)}, {"contentFolders", double(contentFolders)},
                {"contentCountComplete", complete}, {"trashName", QString::fromStdString(platform_trash::trashName())}};
    }
};

FileOperationService::FileOperationService(QObject* parent) : FileOperationService(platform_trash::make(), parent) {}
FileOperationService::FileOperationService(std::unique_ptr<TrashBackend> backend, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(this, backend ? std::move(backend) : platform_trash::make())) {}
FileOperationService::~FileOperationService() = default;
bool FileOperationService::busy() const { return d->busy; }

quint64 FileOperationService::plan(const QStringList& paths, const QString& origin) {
    if (d->busy) return 0;
    // A newer request supersedes whatever was being planned or waiting for confirmation.
    d->planStop.request_stop(); d->planStop = std::stop_source{};
    d->ready.reset(); d->readyId = 0;
    const quint64 id = ++d->nextId;
    d->planningId = id;
    if (paths.isEmpty()) {
        d->planningId = 0;
        QTimer::singleShot(0, this, [this, id] { emit planFailed(id, QStringLiteral("Nothing is selected.")); });
        return id;
    }
    std::vector<fs::path> requested;
    requested.reserve(std::size_t(paths.size()));
    for (const auto& path : paths) requested.push_back(nativePath(path));
    auto* watcher = new QFutureWatcher<DeletionPlan>(this);
    connect(watcher, &QFutureWatcher<DeletionPlan>::finished, this, [this, watcher, id, origin] {
        auto plan = watcher->result(); watcher->deleteLater();
        if (id != d->planningId) return; // superseded or cancelled
        d->planningId = 0;
        if (plan.cancelled) { emit planFailed(id, QStringLiteral("Checking the items was stopped.")); return; }
        d->ready = std::move(plan); d->readyId = id; d->readyOrigin = origin;
        emit planReady(id, d->summary(id, origin, *d->ready));
    });
    watcher->setFuture(QtConcurrent::run(d->pool, [requested = std::move(requested), cancel = d->planStop.get_token()] {
        return planDeletion(requested, cancel);
    }));
    return id;
}

bool FileOperationService::confirm(quint64 id) {
    if (d->busy || id == 0 || id != d->readyId || !d->ready || d->ready->targets.empty()) return false;
    DeletionPlan plan = std::move(*d->ready);
    d->ready.reset(); d->readyId = 0; // one confirmation per plan
    d->executingId = id;
    d->executeStop = std::stop_source{};
    d->setBusy(true);
    const int total = int(plan.targets.size());
    auto* watcher = new QFutureWatcher<std::vector<DeleteResult>>(this);
    connect(watcher, &QFutureWatcher<std::vector<DeleteResult>>::progressValueChanged, this, [this, id, total](int done) {
        if (id == d->executingId) emit progress(id, done, total);
    });
    connect(watcher, &QFutureWatcher<std::vector<DeleteResult>>::finished, this, [this, watcher, id] {
        const auto results = watcher->result(); watcher->deleteLater();
        QVariantList list; QStringList removed;
        for (const auto& r : results) {
            const QString path = displayPath(r.path);
            list.append(QVariantMap{{"path", path}, {"name", leafName(r.path)}, {"outcome", outcomeName(r.outcome)}, {"message", QString::fromStdString(r.message)}});
            if (r.outcome == DeleteOutcome::Trashed) removed << path;
        }
        d->executingId = 0;
        d->setBusy(false);
        emit finished(id, list);
        if (!removed.isEmpty()) emit pathsRemoved(removed);
    });
    watcher->setFuture(QtConcurrent::run(d->pool, [backend = d->backend, plan = std::move(plan), cancel = d->executeStop.get_token()](QPromise<std::vector<DeleteResult>>& promise) {
        promise.setProgressRange(0, int(plan.targets.size()));
        auto results = executeDeletion(plan, *backend, cancel, [&promise](std::size_t done, std::size_t) { promise.setProgressValue(int(done)); });
        promise.addResult(std::move(results));
    }));
    return true;
}

void FileOperationService::cancel(quint64 id) {
    if (id == 0) return;
    if (id == d->planningId) { d->planStop.request_stop(); d->planningId = 0; }
    if (id == d->readyId) { d->ready.reset(); d->readyId = 0; }
    if (id == d->executingId) d->executeStop.request_stop();
}
