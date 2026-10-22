#pragma once
#include "contracts.hpp"
#include <QObject>
#include <QImage>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "glancelot/metadata_model.hpp"
#include "glancelot/animation_controller.hpp"
#include "glancelot/mutation_service.hpp"
#include "glancelot/copy_move_service.hpp"
#include "glancelot/display_manager.hpp"
namespace glancelot { class WindowClaim; }

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
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY stateChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY stateChanged)
    // F053: a high-contrast palette (WCAG AAA text and non-text contrast) and 44 px touch-sized targets. Both persist.
    // F015 tabs (shape A: a tab is a saved location; switching navigates). Empty while there is one tab. At most 8 tabs; they persist
    // with the window's position and restore with the active one navigated.
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY tabsChanged)
    Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY stateChanged)
    Q_PROPERTY(bool largeTargets READ largeTargets WRITE setLargeTargets NOTIFY stateChanged)
    Q_PROPERTY(QString filterPattern READ filterPattern WRITE setFilterPattern NOTIFY filterChanged)
    Q_PROPERTY(bool filterPersistent READ isFilterPersistent WRITE setFilterPersistent NOTIFY filterChanged)
    Q_PROPERTY(bool hasFilter READ hasFilter NOTIFY filterChanged)
    Q_PROPERTY(qint64 decodedCacheBytes READ decodedCacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(int decodedCacheCount READ decodedCacheCount NOTIFY cacheChanged)
    Q_PROPERTY(bool isLastDecodeCacheHit READ isLastDecodeCacheHit NOTIFY imageChanged)
    Q_PROPERTY(QString storageProfile READ storageProfile WRITE setStorageProfile NOTIFY storageProfileChanged)
    Q_PROPERTY(QString currentStorageType READ currentStorageType NOTIFY currentStorageTypeChanged)
    Q_PROPERTY(int activeIoJobs READ activeIoJobs NOTIFY ioStatsChanged)
    Q_PROPERTY(qint64 admittedIoBytes READ admittedIoBytes NOTIFY ioStatsChanged)
    Q_PROPERTY(MetadataModel* metadataModel READ metadataModel CONSTANT)
    Q_PROPERTY(bool metadataVisible READ isMetadataVisible WRITE setMetadataVisible NOTIFY metadataVisibleChanged)
    Q_PROPERTY(glancelot::AnimationController* animationController READ animationController CONSTANT)
    Q_PROPERTY(glancelot::MutationService* mutationService READ mutationService CONSTANT)
    Q_PROPERTY(glancelot::CopyMoveService* copyMoveService READ copyMoveService CONSTANT)
    Q_PROPERTY(glancelot::DisplayManager* displayManager READ displayManager CONSTANT)
    Q_PROPERTY(bool presentationActive READ isPresentationActive NOTIFY presentationChanged)
    Q_PROPERTY(QString presentationMode READ presentationMode NOTIFY presentationChanged)
public:
    explicit ViewerController(QObject* parent=nullptr);
    ~ViewerController() override;
    QImage image() const;
    // F040 reference window: a pinned copy of the current photo (shares its pixels, so it costs no decode or memory beyond
    // the one image) that stays put while the main window browses on. Empty until pinned.
    Q_PROPERTY(QImage referenceImage READ referenceImage NOTIFY referenceChanged)
    Q_PROPERTY(QString referenceName READ referenceName NOTIFY referenceChanged)
    QImage referenceImage() const; QString referenceName() const;
    Q_INVOKABLE bool pinReference();   // false when no photo has decoded
    Q_INVOKABLE void clearReference();
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
    // The portable text of one key press (as it appears in a binding step), so panes can tell whether a key is remapped.
    Q_INVOKABLE QString keyStep(int key, int modifiers) const;
    // Why `sequence` cannot be saved for `id` (reserved Escape, too many steps, a hold that needs one key, or the command it
    // would take a key from), in a sentence for the user; empty when it can. Clearing ("") is always fine.
    Q_INVOKABLE QString bindingProblem(const QString& id, const QString& sequence) const;
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
    // F002: asynchronous, cancellable folder-name completion for the typed path. Emits pathCompletions(text, names) for the
    // newest request only; names are child folders of the typed scope whose names start with the last segment (at most 20).
    Q_INVOKABLE void completePath(const QString& text);
    Q_INVOKABLE bool copyFilePath(const QStringList& paths = QStringList());
    Q_INVOKABLE bool copyFileUrls(const QStringList& paths = QStringList());
    Q_INVOKABLE bool copyImageBitmap();
    Q_INVOKABLE void cancelDirectoryNavigation();
    Q_INVOKABLE bool revealInFileManager(const QString& path = QString());
    Q_INVOKABLE bool openTerminal(const QString& directory = QString());
    qreal textScale() const; void setTextScale(qreal scale);
    bool reducedMotion() const; void setReducedMotion(bool reduced);
    QVariantList tabs() const; int activeTab() const;
    Q_INVOKABLE bool newTab();
    Q_INVOKABLE bool closeTab(int index);
    Q_INVOKABLE bool activateTab(int index);
    bool highContrast() const; void setHighContrast(bool on);
    bool largeTargets() const; void setLargeTargets(bool on);
    Q_INVOKABLE bool exportSettings(const QString& targetFilePath);
    Q_INVOKABLE bool importSettings(const QString& sourceFilePath);
    Q_INVOKABLE bool resetSection(const QString& sectionName);
    Q_INVOKABLE bool resetAllSettings();
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
    // SAFE-MODE-01: application attaches its real claim for guarded checkpoints.
    // Numeric setWindowSlot retains ordinary legacy position-memory behavior,
    // but never authorizes recovery metadata, restore, or clean-exit marking.
    void setWindowClaim(const glancelot::WindowClaim& claim);
    void setSafeMode(bool enabled); // startup-only, session feedback; never persisted
    // Reopens this slot's saved image, or the first image of its saved folder, unless something is already open;
    // an explicit open() always wins. A missing folder reports an actionable status and never erases the saved slot.
    Q_INVOKABLE void restoreWindow();
    // Writes any pending position immediately (window close / application exit).
    void flushWindowPosition();
    // Filtering (FILTER-01 / NAV-05 / UX-02)
    QString filterPattern() const;
    void setFilterPattern(const QString& pattern);
    bool isFilterPersistent() const;
    void setFilterPersistent(bool persistent);
    bool hasFilter() const;
    Q_INVOKABLE void setFilter(const QString& pattern, bool persistent = false);
    Q_INVOKABLE void clearFilter();
    Q_INVOKABLE void toggleFilterPersistence();
    qint64 decodedCacheBytes() const;
    int decodedCacheCount() const;
    bool isLastDecodeCacheHit() const;
    Q_INVOKABLE void clearDecodedCache();

    // Storage & I/O Scheduling (IO-01 / PERF-05 / SYS-02)
    QString storageProfile() const;
    void setStorageProfile(const QString& profile);
    QString currentStorageType() const;
    int activeIoJobs() const;
    qint64 admittedIoBytes() const;

    // Metadata Inspector (META-HUD-01 / META-01 / UX-04)
    MetadataModel* metadataModel() const;
    bool isMetadataVisible() const;
    void setMetadataVisible(bool visible);
    Q_INVOKABLE void toggleMetadata();

    // Animation & Multipage Playback (ANIM-01 / ADV-06 / FMT-03)
    glancelot::AnimationController* animationController() const;

    // Mutation Service (RENAME-01 / F018 / SYS-06)
    glancelot::MutationService* mutationService() const;
    Q_INVOKABLE void reconcileRenamedPath(const QString& oldPath, const QString& newPath);
    Q_INVOKABLE void reconcileCreatedFolder(const QString& folderPath);

    // Copy / Move Service (COPY-MOVE-01 / F020 / META-07 / SYS-06)
    glancelot::CopyMoveService* copyMoveService() const;

    // Current folder listing; valid until the next stateChanged. Copy before use across event-loop turns.
    // Presentation & Second Display (MONITOR-OUTPUT-01 / UX-10 / COLOR-01 / VIEW-04 / PERF-04 / SYS-02)
    glancelot::DisplayManager* displayManager() const;
    bool isPresentationActive() const;
    QString presentationMode() const;
    Q_INVOKABLE void togglePresentation();
    Q_INVOKABLE void openPresentation(const QString& displayId = QString(), const QString& mode = QStringLiteral("mirror"));
    Q_INVOKABLE void closePresentation();
    Q_INVOKABLE void setPresentationFitMode(const QString& mode);
    Q_INVOKABLE void cyclePresentationFit();
    Q_INVOKABLE void pinPresentationImage();
    Q_INVOKABLE void unpinPresentationImage();

    const glancelot::NavigationSnapshot& snapshot() const;
signals:
    // MONITOR-CUSTOM-01: explicit successful import/reset/restore; not navigation.
    void configurationReloaded();
    void imageChanged(); void stateChanged(); void bindingsChanged(); void referenceChanged(); void filterChanged(); void cacheChanged();
    void storageProfileChanged(); void currentStorageTypeChanged(); void ioStatsChanged();
    void metadataVisibleChanged();
    void presentationChanged();
    // The watched file or folder changed on disk; cached derived data (thumbnails) is stale.
    void filesInvalidated();
    // The folder tree was just shown by command; the UI should move keyboard focus into it.
    void treeShown();
    void viewCommand(const QString& id);
    void tabsChanged();
    void pathCompletions(const QString& text, const QStringList& names);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
