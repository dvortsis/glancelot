#pragma once
#include <QAbstractListModel>
#include <QSet>
#include <QStringList>
#include <memory>

// Lead-frozen FILE-GRID-01 model contract: the mixed file-manager grid (folders + all regular files).
// Image traversal and the filmstrip keep their eligible-image semantics; this model never feeds navigation.
//  * Listing runs on a worker; each request carries a generation and results older than the newest are dropped.
//  * Selection is a set of stable path keys (case-insensitive on Windows), never row indices. A relisting keeps
//    the keys that still exist and drops the rest; anchor and cursor follow the same rule.
//  * The model holds no listing while inactive (setActive(false)) and bounds entries via listDirectory's limit.
//  * Activation decisions are reported, not executed: QML calls viewer.navigateDirectory/open itself. Unsupported
//    files are never decoded, thumbnailed or launched.
class DirectoryEntryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool accessible READ accessible NOTIFY directoryChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY directoryChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectedBytes READ selectedBytes NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedBytesFormatted READ selectedBytesFormatted NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSummary READ selectedSummary NOTIFY selectionChanged)
    Q_PROPERTY(int cursorRow READ cursorRow NOTIFY selectionChanged)
    Q_PROPERTY(QString currentImage READ currentImage WRITE setCurrentImage NOTIFY currentImageChanged)
    Q_PROPERTY(quint64 epoch READ epoch NOTIFY epochChanged)
    Q_PROPERTY(QString filterPattern READ filterPattern WRITE setFilterPattern NOTIFY filterChanged)
    Q_PROPERTY(bool filterPersistent READ isFilterPersistent WRITE setFilterPersistent NOTIFY filterChanged)
    Q_PROPERTY(bool hasFilter READ hasFilter NOTIFY filterChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, KindRole, LinkRole, HiddenRole, SizeRole, ModifiedRole,
                SelectedRole, CursorRole, CurrentRole };
    enum SelectMode { Replace, Toggle, Range, ExtendRange };
    Q_ENUM(SelectMode)
    explicit DirectoryEntryModel(QObject* parent = nullptr);
    ~DirectoryEntryModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString directory() const; int count() const; bool loading() const; bool accessible() const; bool truncated() const;
    // Lists `directory` asynchronously (newest request wins). Same directory = refresh preserving selection.
    Q_INVOKABLE void setDirectory(const QString& directory);
    Q_INVOKABLE void refresh();
    // Inactive: cancel work and release the listing (grid hidden). Reactivating relists.
    Q_INVOKABLE void setActive(bool active);
    // "name" | "modified" | "size" | "shuffle"; folders stay first in natural order regardless. `seed` is the
    // controller's shuffle seed (ViewerController::sortSeed) so the grid's shuffle matches traversal.
    Q_INVOKABLE void setSort(const QString& key, bool descending, quint64 seed = 1);
    Q_INVOKABLE void setIncludeHidden(bool include);

    // Selection by row (rows are only addresses for the current listing).
    Q_INVOKABLE void select(int row, SelectMode mode = Replace);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void invertSelection();
    int selectedCount() const; int cursorRow() const;
    qint64 selectedBytes() const;
    QString selectedBytesFormatted() const;
    QString selectedSummary() const;
    Q_INVOKABLE QStringList selectedPaths() const;     // listing order
    Q_INVOKABLE int rowOf(const QString& path) const;  // -1 when absent
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString kindAt(int row) const;         // "folder" | "image" | "file" | ""

    // Instant search (F004): case-insensitive substring match across listing names.
    // Returns matching row index or -1 if no match found.
    Q_INVOKABLE int findMatch(const QString& query, int startRow = 0, bool forward = true) const;
    Q_INVOKABLE int matchCount(const QString& query) const;
    Q_INVOKABLE int matchIndex(const QString& query, int row) const;

    // Filtering (FILTER-01 / NAV-05 / UX-02)
    QString filterPattern() const;
    void setFilterPattern(const QString& pattern);
    bool isFilterPersistent() const;
    void setFilterPersistent(bool persistent);
    bool hasFilter() const;
    Q_INVOKABLE void setFilter(const QString& pattern, bool persistent = false);
    Q_INVOKABLE void clearFilter();
    Q_INVOKABLE void toggleFilterPersistence();

    QString currentImage() const; void setCurrentImage(const QString& path);
    quint64 epoch() const;
    void invalidate();                                 // content changed on disk; thumbnails must re-request
    // image:// URL; empty for non-image rows so unsupported files never reach the decoder.
    Q_INVOKABLE QString thumbnailSource(int row, int edge) const;
signals:
    void directoryChanged(); void countChanged(); void loadingChanged(); void selectionChanged();
    void currentImageChanged(); void epochChanged(); void filterChanged();
    // Listing failed or the folder became inaccessible; message is user-facing.
    void listingFailed(const QString& message);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
