#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace glancelot { struct TrashBackend; }

// Lead-frozen DELETE-01 application contract: one shared asynchronous mutation service per process.
// Two explicit steps so the user always confirms an immutable snapshot:
//   1. plan(paths, origin) snapshots targets on a worker and emits planReady(id, summary).
//   2. confirm(id) executes exactly that plan (revalidating each target) and emits finished(id, results).
// A newer plan() supersedes any unconfirmed plan; a superseded or cancelled id can never be confirmed.
// Only one execution runs at a time; plan() while executing is rejected (busy).
//
// summary (QVariantMap): id, origin, targets [{path, name, kind "file"|"folder", link, bytes, contentFiles,
//   contentFolders, contentCountComplete}], rejected [{path, reason, message}], fileCount, folderCount,
//   contentFiles, contentFolders, contentCountComplete, trashName ("Recycle Bin" | "Trash").
// results (QVariantList): [{path, outcome "trashed"|"missing"|"changed"|"denied"|"inUse"|"trashUnavailable"|
//   "cancelled"|"failed", message}], plan order.
class FileOperationService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit FileOperationService(QObject* parent = nullptr);
    // Tests inject a backend; the default is the platform trash (never permanent deletion).
    explicit FileOperationService(std::unique_ptr<glancelot::TrashBackend> backend, QObject* parent = nullptr);
    ~FileOperationService() override;
    bool busy() const;
    // origin: "grid" | "tree" | "canvas" | "command" (diagnostics and wording only). Returns the plan id, 0 if busy.
    Q_INVOKABLE quint64 plan(const QStringList& paths, const QString& origin);
    Q_INVOKABLE bool confirm(quint64 id);   // false when id is stale, superseded, cancelled or empty
    Q_INVOKABLE void cancel(quint64 id);    // planning: discard; executing: stop before the next item
signals:
    void busyChanged();
    void planReady(quint64 id, const QVariantMap& summary);
    void planFailed(quint64 id, const QString& message);
    void progress(quint64 id, int done, int total);
    void finished(quint64 id, const QVariantList& results);
    // Emitted after finished() with every successfully trashed path so models, navigation, preview, caches and
    // the session can reconcile without a stale result restoring a removed entry.
    void pathsRemoved(const QStringList& paths);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
