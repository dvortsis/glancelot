#pragma once
#include "contracts.hpp"
#include <QObject>
#include <QImage>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class ViewerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QImage image READ image NOTIFY imageChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString directoryError READ directoryError NOTIFY stateChanged)
    Q_PROPERTY(bool directoryPending READ directoryPending NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool imageReady READ imageReady NOTIFY stateChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY stateChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY stateChanged)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY bindingsChanged)
    Q_PROPERTY(QString boundaryMode READ boundaryMode WRITE setBoundaryMode NOTIFY stateChanged)
    Q_PROPERTY(QString wheelMode READ wheelMode WRITE setWheelMode NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap wheelModifiers READ wheelModifiers NOTIFY stateChanged)
    Q_PROPERTY(bool filmstripVisible READ filmstripVisible WRITE setFilmstripVisible NOTIFY stateChanged)
    Q_PROPERTY(bool canRaiseRoot READ canRaiseRoot NOTIFY stateChanged)
    Q_PROPERTY(bool treeVisible READ treeVisible WRITE setTreeVisible NOTIFY stateChanged)
    Q_PROPERTY(bool treePinned READ treePinned WRITE setTreePinned NOTIFY stateChanged)
    Q_PROPERTY(int windowSlot READ windowSlot NOTIFY stateChanged)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY stateChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY stateChanged)
public:
    explicit ViewerController(QObject* parent=nullptr);
    ~ViewerController() override;
    QImage image() const;
    QString filePath() const; QString fileName() const; QString directory() const; QString status() const;
    // True only when the currently requested file decoded successfully; a retained old image is not a success.
    bool imageReady() const;
    // Pure path comparison, including Windows separators/case; never stats the filesystem.
    Q_INVOKABLE bool isCurrentImage(const QString& path) const;
    bool loading() const; int currentIndex() const; int fileCount() const;
    int imageWidth() const; int imageHeight() const;
    QVariantList commands() const;
    QString boundaryMode() const; QString wheelMode() const;
    Q_INVOKABLE void open(const QString& file);
    Q_INVOKABLE void next(); Q_INVOKABLE void previous();
    Q_INVOKABLE void nextFolder(); Q_INVOKABLE void previousFolder();
    Q_INVOKABLE void trigger(const QString& id);
    Q_INVOKABLE QString bindingFor(const QString& id) const;
    Q_INVOKABLE bool setBinding(const QString& id,const QString& sequence);
    Q_INVOKABLE void resetBindings();
    void setBoundaryMode(const QString& mode); void setWheelMode(const QString& mode);
    // INPUT-02 wheel matrix: {"none","ctrl","shift","alt"} -> "navigate" | "zoom" | "pan" | "pan-horizontal". "none" is
    // wheelMode; an Alt entry nobody set follows it. Only the changed modifier is merged into the stored settings.
    QVariantMap wheelModifiers() const;
    Q_INVOKABLE bool setWheelModifier(const QString& modifier, const QString& action);
    // The command whose binding blocks `sequence` for `id` (same or overlapping scope, including chord prefixes), or "".
    Q_INVOKABLE QString bindingConflict(const QString& id, const QString& sequence) const;
    bool filmstripVisible() const; void setFilmstripVisible(bool visible);
    bool canRaiseRoot() const;
    bool treeVisible() const; void setTreeVisible(bool visible);
    bool treePinned() const; void setTreePinned(bool pinned);
    // File order within a folder: "name", "modified", "size" or "shuffle". Folder order never changes. A change
    // re-lists the current folder on the navigation worker and keeps the current image.
    QString sortKey() const; void setSortKey(const QString& key);
    bool sortDescending() const; void setSortDescending(bool descending);
    // Shuffle permutation seed; other views of the folder pass it on so their shuffle order matches traversal.
    quint64 sortSeed() const;
    // Folder selection from the tree: opens the first eligible image of `folder`, or of the first subfolder (in
    // traversal order) that has one. Asynchronous; the newest request wins. Reports a status when nothing is found.
    Q_INVOKABLE void openFolder(const QString& folder);
    // Exact asynchronous address visit; empty directories are valid and errors retain the location.
    Q_INVOKABLE void navigateDirectory(const QString& folder);
    Q_INVOKABLE void copyDirectory() const;
    Q_INVOKABLE void cancelDirectoryNavigation();
    QString directoryError() const; bool directoryPending() const;
    // Explicitly widens the traversal root to its parent (never implicit; never past the filesystem root).
    Q_INVOKABLE void raiseRoot();
    // DELETE-01: `paths` were just moved to the Trash/Recycle Bin. Invalidates derived data (filesInvalidated), then
    // either steps from a removed opened image to the next eligible one (deleted-current anchor) or, when the browsing
    // folder or an ancestor was removed, visits the nearest existing ancestor. Filesystem checks run on the worker.
    Q_INVOKABLE void reconcileRemovedPaths(const QStringList& paths);
    // Per-window position memory (UX-12). Slot 0 disables it. The last folder and image are written (debounced,
    // atomically, through the shared settings writer) only after an image decodes successfully.
    int windowSlot() const; void setWindowSlot(int slot);
    // Reopens this slot's saved image, or the first image of its saved folder, unless something is already open;
    // an explicit open() always wins. A missing folder reports an actionable status and never erases the saved slot.
    Q_INVOKABLE void restoreWindow();
    // Writes any pending position immediately (window close / application exit).
    void flushWindowPosition();
    // Current folder listing; valid until the next stateChanged. Copy before use across event-loop turns.
    const glancelot::NavigationSnapshot& snapshot() const;
signals:
    void imageChanged(); void stateChanged(); void bindingsChanged();
    // The watched file or folder changed on disk; cached derived data (thumbnails) is stale.
    void filesInvalidated();
    // The folder tree was just shown by command; the UI should move keyboard focus into it.
    void treeShown();
    void viewCommand(const QString& id);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
