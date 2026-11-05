#pragma once
#include "glancelot/selection_export.hpp"
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>

// B3 (F070): the worker behind ui/ExportDialog.qml. Registered as a QML type by its own translation unit, so an
// executable that does not compile it can still load ui/Main.qml (the dialog is loaded by URL, on first use).
//   1. prepare(paths) snapshots the items on a worker and emits prepared(id, summary).
//   2. start(options) exports that immutable snapshot on a worker and emits finished(id, result).
// Nothing here runs file-system calls on the calling thread. A newer prepare() supersedes whatever is running, and a
// result whose id is not the newest is dropped. Every exception from the engine or the conversions around it stays
// on the worker and becomes the "error" status (review O1 of EXPORT-CSV-01). The destructor stops the worker and
// waits for it, so a window closed mid-export leaves no temporary file.
//
// summary (QVariantMap): status "ready"|"tooMany"|"cancelled"|"error", total, files, folders, unavailable, detail,
//   maxItems.
// options (QVariantMap): destination, format "csv"|"paths", relative (bool), root, neutralizeFormulas (bool),
//   overwrite (bool).
// result (QVariantMap): status "written"|"cancelled"|"destinationEmpty"|"destinationRelative"|"destinationIsFolder"|
//   "folderMissing"|"destinationProtected"|"destinationExists"|"destinationIsItem"|"invalid"|"tooMany"|"failed"|
//   "error", written, destination, detail, skippedCount, skipped [{path, name, reason "missing"|"changed"|"duplicate"|
//   "outsideRoot"|"unrepresentable"}] (at most kMaxReportedSkips).
class SelectionExportService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    static constexpr int kMaxReportedSkips = 200;
    using Progress = std::function<void(std::size_t, std::size_t)>;
    using Engine = std::function<glancelot::ExportResult(const glancelot::ExportRequest&, std::stop_token, const Progress&)>;

    explicit SelectionExportService(QObject* parent = nullptr);
    ~SelectionExportService() override;
    bool busy() const;
    // Test seams: the default engine is glancelot::exportSelection and the default limit is the engine's own.
    void setEngine(Engine engine);
    void setMaxItems(std::size_t maxItems);

    Q_INVOKABLE quint64 prepare(const QStringList& paths); // returns the id, never 0
    Q_INVOKABLE quint64 start(const QVariantMap& options); // returns the id; 0 when busy or nothing is prepared
    Q_INVOKABLE void cancel();                             // honoured between items; the result still arrives
    Q_INVOKABLE QString defaultFolder() const;
    Q_INVOKABLE QString localPath(const QUrl& url) const;
    Q_INVOKABLE QUrl folderUrl(const QString& path) const;
signals:
    void busyChanged();
    void prepared(quint64 id, const QVariantMap& summary);
    void progress(quint64 id, int done, int total);
    void finished(quint64 id, const QVariantMap& result);
private:
    void workerDone();
    struct Private;
    std::unique_ptr<Private> d;
};
