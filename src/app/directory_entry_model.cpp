#include "glancelot/perf_trace.hpp"
#include <QCoreApplication>
#include "../core/perf_context.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/filter_state.hpp"
#include "directory_scan.hpp"
#include "thumbnail_id.hpp"
#include <QDateTime>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <optional>
#include <type_traits>
#include <utility>

namespace {
using directory_scan::keyFor;
using directory_scan::Listing;
using directory_scan::Row;
constexpr int kMaxConcurrentListings = 2;
// Above this many separate insertion/removal runs a refresh resets instead of merging in place (bounded GUI work).
constexpr int kMaxMergeRuns = 32;
// Arrival publication: one delta per tick, so a large folder arriving costs the GUI thread a bounded share of each frame.
constexpr int kDrainIntervalMs = 16;
constexpr qint64 kDrainBudgetMs = 6;
// While rows are provisional the index stores positions inside the folder or file segment: inserting a folder shifts
// every file's row, and this keeps that insert from rewriting the index. See Private::rowOfKey.
constexpr int kFolderSegment = 1 << 30;

glancelot::SortSpec sortSpec(const QString& key, bool descending) {
    glancelot::SortSpec spec;
    spec.key = key == "modified" ? glancelot::SortKey::Modified : key == "size" ? glancelot::SortKey::Size
             : key == "shuffle" ? glancelot::SortKey::Shuffle : glancelot::SortKey::Name;
    spec.descending = descending;
    return spec;
}

}

struct DirectoryEntryModel::Private {
    DirectoryEntryModel* q;
#ifdef GLANCELOT_FS_WATCH_TESTING
    std::function<void(const fs_watch::ModelPublicationStats&)> publicationTiming=fs_watch::modelPublicationTiming;
    fs_watch::ModelPublicationStats publicationStats;
    struct { qint64 ms{}; } definitiveStats; // the rows' share of a publication split around the identity probes
#endif
    QString directory, directoryKey, currentImage, currentKey;
    std::vector<Row> rows;
    QHash<QString, int> index;
    Listing rawListing;
    glancelot::FilterSpec filter;
    int kindWanted{-1}; // -1: every kind; otherwise a glancelot::EntryKind value (F042 type chip)
    bool filtering() const { return filter.isActive() || kindWanted >= 0; }
    QSet<QString> selected;
    QSet<QString> previousSelection;
    QString previousAnchor,previousCursor;
    fs_watch::Snapshot identities;
    QString anchorKey, cursorKey;
    glancelot::SortSpec sort;
    bool includeHidden{}, active{}, loading{}, accessible{true}, truncated{}, wanted{};
    quint64 epoch{1}, generation{};
    int inFlight{};
    std::stop_source stop;
    // First listing of a folder: the worker's queue of provisional rows, drained on a timer under the GUI budget.
    std::shared_ptr<directory_scan::Arrivals> arrivals;
    QTimer drainTimer;
    std::optional<Listing> finished;   // the finished scan, waiting for the batches queued before it
    bool arriving{};                   // a first listing whose definitive order is not published yet
    bool provisional{};                // rows are in arrival order, and `index` holds segment positions
    bool refreshAfterArrival{};        // a change arrived mid-listing: finish it, then refresh, rather than start over
    bool loadingMore{};                // the user asked for the next page, and its listing has not been published yet
    std::size_t entryLimit{};          // this folder's cap; "Show more" raises it, opening another folder resets it
    int provisionalFolders{};
    directory_scan::Tuning tuning;
    directory_scan::Enumerator enumerator;
    std::function<void(std::stop_token)> beforeProbes;
    // Heap-allocated so a listing wedged in the OS (dead network share) can be abandoned at teardown.
    QThreadPool* pool = new QThreadPool;

    explicit Private(DirectoryEntryModel* owner) : q(owner) {
        pool->setMaxThreadCount(kMaxConcurrentListings);
#ifdef GLANCELOT_PERF_ENUM_TESTING
        tuning = directory_scan::testSeam.tuning; enumerator = directory_scan::testSeam.enumerator; beforeProbes = directory_scan::testSeam.beforeProbes;
#endif
        entryLimit = tuning.entryStep;
        drainTimer.setInterval(kDrainIntervalMs);
        QObject::connect(&drainTimer, &QTimer::timeout, q, [this] { drain(); });
    }
    ~Private() {
        stop.request_stop();
        if (pool->waitForDone(1000)) delete pool;
        // else: abandon the pool; its job owns only by-value captures.
    }
    int rowOfKey(const QString& key) const {
        const int value = key.isEmpty() ? -1 : index.value(key, -1);
        if (value < 0 || !provisional) return value;
        return value & kFolderSegment ? value & ~kFolderSegment : provisionalFolders + value;
    }
    bool canLoadMore() const { return truncated && (loadingMore || entryLimit < tuning.entryLimit); }
    bool passesFilter(const Row& row) const {
        return (!filter.isActive() || filter.matches(row.name)) && (kindWanted < 0 || int(row.kind) == kindWanted);
    }
    void setLoading(bool value) { if (loading != value) { loading = value; emit q->loadingChanged(); } }
    void setArriving(bool value) { if (arriving != value) { arriving = value; emit q->arrivingChanged(); } }
    void applyFilter() {
        q->beginResetModel();
        rows.clear();
        index.clear();
        if (provisional) {
            // rawListing is in arrival order here and has no index yet.
            provisionalFolders = 0;
            std::vector<Row> files;
            for (const auto& row : rawListing.rows) {
                if (!passesFilter(row)) continue;
                if (row.kind == glancelot::EntryKind::Folder) { index.insert(row.key, kFolderSegment | provisionalFolders++); rows.push_back(row); }
                else { index.insert(row.key, int(files.size())); files.push_back(row); }
            }
            rows.insert(rows.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
        } else if (!filtering()) {
            rows = rawListing.rows;
            index = rawListing.index;
        } else {
            rows.reserve(rawListing.rows.size());
            index.reserve(rawListing.index.size());
            for (const auto& row : rawListing.rows) {
                if (passesFilter(row)) {
                    index.insert(row.key, int(rows.size()));
                    rows.push_back(row);
                }
            }
        }
        q->endResetModel();
        emit q->countChanged();
        emit q->filterChanged();
        emit q->selectionChanged();
    }
    // Newest request wins: stale work is cancelled; at most kMaxConcurrentListings run and one request waits.
    void request() {
        if (!active || directory.isEmpty()) return;
        abandonScan();
        stop = std::stop_source{};
        wanted = true;
        // Provisional rows belong to the abandoned scan, in an order nothing will finish.
        if (provisional) release();
        // A folder with rows on screen keeps them until its new listing is finished; only a first listing arrives.
        if (rows.empty() && rawListing.rows.empty()) {
            arrivals = std::make_shared<directory_scan::Arrivals>(std::size_t(std::max(1, tuning.queuedRows)));
            drainTimer.start();
            setArriving(true);
        }
        setLoading(true);
        pump();
    }
    void abandonScan() {
        stop.request_stop(); ++generation; wanted = false;
        arrivals.reset(); finished.reset(); refreshAfterArrival = false;
        drainTimer.stop();
        setArriving(false);
    }
    void refresh() {
        if (arrivals || loadingMore) refreshAfterArrival = true; else request();
    }
    void pump() {
        if (!wanted || inFlight >= kMaxConcurrentListings) return;
        wanted = false;
        ++inFlight;
        const quint64 token = generation;
        auto* watcher = new QFutureWatcher<Listing>(q);
        QObject::connect(watcher, &QFutureWatcher<Listing>::finished, q, [this, watcher, token] {
            // Single consumer, reached only after finished: move the rows out
            // instead of deep-copying a 100k-row vector on the GUI thread and
            // leaving a second copy for deferred watcher destruction.
            static_assert(std::is_nothrow_move_constructible_v<Listing>);
            auto listing = watcher->future().takeResult(); watcher->deleteLater(); --inFlight;
            if (token != generation || !active || listing.cancelled) {
                glancelot::perf::ScopedSpan publication(glancelot::perf::Stage::Publish, token);
                publication.setFlags(token != generation || !active ? glancelot::perf::Stale : glancelot::perf::Cancelled);
            } else { finished = std::move(listing); drain(); }
            pump();
        });
        directory_scan::Request scan{directory, sort, includeHidden, entryLimit, identities, tuning, enumerator, beforeProbes, loadingMore && !filtering()};
        watcher->setFuture(QtConcurrent::run(pool, [scan = std::move(scan), queue = arrivals, cancel = stop.get_token(), token] {
            glancelot::perf::detail::GenerationContext context(token);
            return directory_scan::scan(scan, queue, cancel);
        }));
    }
    // Queued rows first, under the budget; the finished scan publishes once nothing queued before it is left.
    void drain() {
        if (arrivals && !publishBatches()) return;
        if (finished) publishFinished();
    }
    // One tick's delta: at most one insert for folders and one for files. Returns true when the queue ran dry.
    bool publishBatches() {
        auto batch = arrivals->take();
        if (!batch) return true;
        QElapsedTimer budget; budget.start();
        std::optional<Listing> definitive;
        bool dry = false;
        { glancelot::perf::ScopedSpan publication(glancelot::perf::Stage::Publish, generation);
        std::vector<Row> folders, files;
        const auto admit = [&](std::vector<Row>& from, std::vector<Row>& to, bool folder) {
            for (auto& row : from) {
                rawListing.rows.push_back(row);
                if (!passesFilter(row)) continue;
                index.insert(row.key, folder ? kFolderSegment | (provisionalFolders + int(to.size())) : int(rows.size()) - provisionalFolders + int(to.size()));
                to.push_back(std::move(row));
            }
        };
        do {
            if (batch->definitive) { definitive = std::move(batch->definitive); batch.reset(); break; }
            provisional = true;
            admit(batch->folders, folders, true);
            admit(batch->files, files, false);
            batch.reset();
        } while (budget.elapsed() < kDrainBudgetMs && (batch = arrivals->take()));
        dry = !batch && budget.elapsed() < kDrainBudgetMs;
        publication.setBytes(folders.size() + files.size());
        if (!folders.empty()) {
            q->beginInsertRows({}, provisionalFolders, provisionalFolders + int(folders.size()) - 1);
            rows.insert(rows.begin() + provisionalFolders, std::make_move_iterator(folders.begin()), std::make_move_iterator(folders.end()));
            provisionalFolders += int(folders.size());
            q->endInsertRows();
        }
        if (!files.empty()) {
            q->beginInsertRows({}, int(rows.size()), int(rows.size() + files.size()) - 1);
            rows.insert(rows.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
            q->endInsertRows();
        }
        if (!folders.empty() || !files.empty()) emit q->countChanged();
        if (provisional && !accessible) { accessible = true; emit q->directoryChanged(); } // recovering: rows, not the failure, are showing
        }
        if (definitive) publishDefinitive(std::move(*definitive));
        return dry;
    }
    // The order is final and the rows are complete; only the identity baseline is still on its way.
    void publishDefinitive(Listing listing) {
#ifdef GLANCELOT_FS_WATCH_TESTING
        QElapsedTimer publicationClock;publicationClock.start();
#endif
        glancelot::perf::ScopedSpan publication(glancelot::perf::Stage::Publish, generation);
        publication.setBytes(listing.rows.size());
        drainTimer.stop(); // the queue is finished; the scan's end wakes drain() itself
        const bool wasAccessible = accessible, wasTruncated = truncated;
        accessible = true; truncated = listing.truncated;
        publishRows(std::move(listing));
        setArriving(false);
        if (wasAccessible != accessible || wasTruncated != truncated) emit q->directoryChanged();
#ifdef GLANCELOT_FS_WATCH_TESTING
        definitiveStats.ms=publicationClock.elapsed();
#endif
    }
    void publishFinished() {
#ifdef GLANCELOT_FS_WATCH_TESTING
        QElapsedTimer publicationClock;publicationClock.start();
        publicationStats={};
#endif
        Listing listing = std::move(*finished);
        finished.reset(); arrivals.reset(); drainTimer.stop();
        { glancelot::perf::ScopedSpan publication(glancelot::perf::Stage::Publish, generation);
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.rows=listing.rowsPublished?listing.rowCount:qsizetype(listing.rows.size());
        publicationStats.observedIdentities=listing.identities.entries.size();
        publicationStats.complete=listing.identities.complete;
#endif
        publication.setBytes(listing.rows.size());
        if (!listing.accessible) publication.setFlags(glancelot::perf::Failed);
        apply(std::move(listing)); }
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.totalMs=publicationClock.elapsed()+definitiveStats.ms;
        publicationStats.acceptedIdentities=identities.entries.size();
        if(publicationTiming)publicationTiming(publicationStats);
        definitiveStats={};
#endif
        if (refreshAfterArrival) { refreshAfterArrival = false; request(); }
    }
    void publishRows(Listing listing) {
#ifdef GLANCELOT_FS_WATCH_TESTING
        QElapsedTimer phase;phase.start();
#endif
        const int oldCount = int(rows.size());
        // Filtering is a projection: identity matching/pruning uses the full
        // physical listing, never just the visible rows.
        if (!truncated || provisional || loadingMore || (filtering() ? rawListing.rows.empty() : rows.empty())) {
#ifdef GLANCELOT_FS_WATCH_TESTING
            publicationStats.applied=true;publicationStats.filtered=filtering();
#endif
            if (!filtering()) {
                if (loadingMore) replaceRows(std::move(listing));
                else {
                    merge(std::move(listing));
                    rawListing.rows = rows;
                    rawListing.index = index;
                }
#ifdef GLANCELOT_FS_WATCH_TESTING
                publicationStats.mergeMs=phase.restart();
#endif
                if (int(rows.size()) != oldCount) emit q->countChanged();
            } else {
                provisional = false; provisionalFolders = 0;
                if (loadingMore) retire(std::make_shared<Listing>(std::move(rawListing)));
                rawListing = std::move(listing);
                rawListing.rawRows = {};
                applyFilter();
            }
        }
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.projectionMs=phase.restart();
#endif
    }
    void apply(Listing listing) {
#ifdef GLANCELOT_FS_WATCH_TESTING
        QElapsedTimer phase;phase.start();
#endif
        const bool wasAccessible = accessible, wasTruncated = truncated, couldLoadMore = canLoadMore();
        accessible = listing.accessible; truncated = listing.truncated;
        const bool rowsWerePublished = listing.rowsPublished;
        const QSet<QString> selectedBefore = rowsWerePublished ? selected : QSet<QString>{};
        const QString cursorBefore = cursorKey;
        // Identity matching was bounded and performed on the listing worker. Keep
        // selection/cursor/anchor on a proven renamed object, including noncurrent rows.
        if(listing.identities.complete) {
            if(listing.renamedDirectory) {
                selected=previousSelection;anchorKey=previousAnchor;cursorKey=previousCursor;
            }
            QSet<QString> renamedSelection;
            for(const auto& value:selected)renamedSelection.insert(listing.renamed.contains(value)?keyFor(listing.renamed[value]):value);
            selected=std::move(renamedSelection);
            if(listing.renamed.contains(anchorKey))anchorKey=keyFor(listing.renamed[anchorKey]);
            if(listing.renamed.contains(cursorKey))cursorKey=keyFor(listing.renamed[cursorKey]);
            previousSelection.clear();previousAnchor.clear();previousCursor.clear();
        }
        // An incomplete identity scan cannot replace the last complete baseline
        // for this scope. Do not carry that baseline across unrelated navigation.
        if(listing.identities.complete)identities=listing.identities;
        else if(keyFor(identities.directory)!=directoryKey)identities={};
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.identityMs=phase.restart();
#endif
        if (!rowsWerePublished) publishRows(std::move(listing));
#ifdef GLANCELOT_FS_WATCH_TESTING
        else { publicationStats.applied=true;publicationStats.filtered=filtering(); }
        phase.restart();
#endif
        if (accessible && !truncated) {
            for (auto it = selected.begin(); it != selected.end();) {
                if (rawListing.index.contains(*it)) ++it; else it = selected.erase(it);
            }
            if (!rawListing.index.contains(anchorKey)) anchorKey.clear();
            if (!rawListing.index.contains(cursorKey)) cursorKey.clear();
        }
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.selectionMs=phase.elapsed();
#endif

        // Rows published before the identities arrived: delegates read selection from data(), so tell them it moved.
        if (rowsWerePublished && (selected != selectedBefore || cursorKey != cursorBefore)) {
            changedRows(QSet<QString>(selectedBefore).unite(selected), {SelectedRole});
            changedRows({cursorBefore, cursorKey}, {CursorRole});
        }
        loadingMore = false;
        setArriving(false);
        setLoading(false);
        if (wasAccessible != accessible || wasTruncated != truncated || couldLoadMore != canLoadMore()) emit q->directoryChanged();
        emit q->selectionChanged();
        if (!accessible) emit q->listingFailed(QCoreApplication::translate("Grid","This folder can't be opened. It may have been moved or deleted, or you may not have permission to read it."));
    }
    // Same folder relisted: remove vanished rows and insert new ones in place when the surviving order is
    // unchanged, so delegates, scroll position and in-flight previews survive a refresh. Otherwise reset.
    void merge(Listing listing) {
#ifdef GLANCELOT_FS_WATCH_TESTING
        QElapsedTimer scanClock;scanClock.start();
#endif
        auto& incoming = listing.rows;
        const bool arrived = std::exchange(provisional, false);
        provisionalFolders = 0;
        if (rows.empty() || incoming.empty()) { reset(std::move(listing)); return; }
        // A file-content notification usually leaves membership and order intact.
        // Prove that directly before building survivors and repeatedly hashing
        // every path to discover that no insertion/removal is needed.
        if (rows.size() == incoming.size() && std::equal(rows.cbegin(), rows.cend(), incoming.cbegin(),
                [](const Row& oldRow, const Row& newRow) { return oldRow.key == newRow.key; })) {
#ifdef GLANCELOT_FS_WATCH_TESTING
            publicationStats.mergeScanMs=scanClock.elapsed();
#endif
            updateMetadata(listing);
            return;
        }
        // The definitive order of rows already showing: same rows, so views keep their delegates and position.
        if (arrived && rows.size() == incoming.size()) { reorder(std::move(listing)); return; }
        std::vector<int> survivors;
        survivors.reserve(incoming.size());
        for (int i = 0; i < int(rows.size()); ++i) if (listing.index.contains(rows[std::size_t(i)].key)) survivors.push_back(i);
        // Retained rows must appear in the same relative order in the new listing.
        int last = -1;
        for (int i : survivors) {
            const int at = listing.index.value(rows[std::size_t(i)].key);
            if (at <= last) {
#ifdef GLANCELOT_FS_WATCH_TESTING
                publicationStats.mergeScanMs=scanClock.elapsed();
#endif
                reset(std::move(listing)); return;
            }
            last = at;
        }
        // Every removal or insertion run costs an O(rows) vector move plus one model signal on the GUI thread, so
        // many scattered changes (a sync tool touching a large folder) would stall the UI for seconds. Count the
        // runs first (O(rows)) and replace the listing in one reset when the in-place merge would be expensive.
        int runs = 0;
        for (int i = 0; i < int(rows.size()); ++i)
            if (!listing.index.contains(rows[std::size_t(i)].key) && (i == 0 || listing.index.contains(rows[std::size_t(i - 1)].key))) ++runs;
        for (int i = 0; i < int(incoming.size()); ++i)
            if (!index.contains(incoming[std::size_t(i)].key) && (i == 0 || index.contains(incoming[std::size_t(i - 1)].key))) ++runs;
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.mergeScanMs=scanClock.elapsed();
#endif
        if (runs > kMaxMergeRuns) { reset(std::move(listing)); return; }
        // Removals, back to front in contiguous runs.
        for (int i = int(rows.size()) - 1; i >= 0;) {
            if (listing.index.contains(rows[std::size_t(i)].key)) { --i; continue; }
            int first = i;
            while (first > 0 && !listing.index.contains(rows[std::size_t(first - 1)].key)) --first;
            q->beginRemoveRows({}, first, i);
            rows.erase(rows.begin() + first, rows.begin() + i + 1);
            q->endRemoveRows();
            i = first - 1;
        }
        // Insertions, front to back: at each position either the surviving row matches or new rows go in.
        for (int i = 0; i < int(incoming.size());) {
            if (i < int(rows.size()) && rows[std::size_t(i)].key == incoming[std::size_t(i)].key) { ++i; continue; }
            int end = i;
            // rows[i], when present, is a survivor that appears later in `incoming`; new rows fill the gap.
            while (end < int(incoming.size()) && (i >= int(rows.size()) || rows[std::size_t(i)].key != incoming[std::size_t(end)].key)) ++end;
            q->beginInsertRows({}, i, end - 1);
            rows.insert(rows.begin() + i, incoming.begin() + i, incoming.begin() + end);
            q->endInsertRows();
            i = end;
        }
        updateMetadata(listing);
    }
    // Both merge paths have established identical row keys and order here.
    // Preserve delegates and publish changed metadata through the same signal.
    void updateMetadata(Listing& listing) {
        const auto& incoming = listing.rows;
        int firstChanged = -1, lastChanged = -1;
        for (int i = 0; i < int(rows.size()); ++i) {
            if (!(rows[std::size_t(i)] == incoming[std::size_t(i)])) {
                rows[std::size_t(i)] = incoming[std::size_t(i)];
                if (firstChanged < 0) firstChanged = i;
                lastChanged = i;
            }
        }
        index = std::move(listing.index);
        if (firstChanged >= 0) emit q->dataChanged(q->index(firstChanged), q->index(lastChanged));
    }
    // Publishes a new order of the same rows. VerticalSortHint makes QML item views rebind in place instead of resetting.
    void reorder(Listing listing) {
        const QList<QPersistentModelIndex> everything;
        emit q->layoutAboutToBeChanged(everything, QAbstractItemModel::VerticalSortHint);
        const auto before = q->persistentIndexList();
        QStringList keys;
        keys.reserve(before.size());
        for (const auto& at : before) keys << (at.row() < int(rows.size()) ? rows[std::size_t(at.row())].key : QString());
        rows = std::move(listing.rows);
        index = std::move(listing.index);
        QModelIndexList after;
        after.reserve(before.size());
        for (const auto& key : keys) after << q->index(index.value(key, -1));
        q->changePersistentIndexList(before, after);
        emit q->layoutChanged(everything, QAbstractItemModel::VerticalSortHint);
    }
    void reset(Listing listing) {
#ifdef GLANCELOT_FS_WATCH_TESTING
        QElapsedTimer resetClock;resetClock.start();
#endif
        q->beginResetModel();
        rows = std::move(listing.rows);
        index = std::move(listing.index);
        q->endResetModel();
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.resetMs+=resetClock.elapsed();
#endif
    }
    // A new page lands anywhere in the order, across up to a million rows. Merging would hash them all here, and copying
    // or freeing that many rows here would stall the GUI, so the worker made the copy and the pool frees the old rows.
    // Selection is by key, and the view restores its anchor after a reset.
    void replaceRows(Listing listing) {
        q->beginResetModel();
        auto old = std::make_shared<Listing>(std::move(rawListing));
        old->rawRows = std::move(rows);
        rows = std::move(listing.rows);
        index = std::move(listing.index);
        rawListing = {};
        rawListing.rows = listing.rawRows.empty() ? rows : std::move(listing.rawRows);
        rawListing.index = index;
        q->endResetModel();
        retire(std::move(old));
    }
    template <typename T> void retire(std::shared_ptr<T> old) { pool->start([old = std::move(old)]() mutable { old.reset(); }); }
    void release() {
        provisional = false; provisionalFolders = 0;
        if (!rows.empty() || !rawListing.rows.empty()) {
            q->beginResetModel();
            rows.clear();
            index.clear();
            rawListing.rows.clear();
            rawListing.index.clear();
            q->endResetModel();
            emit q->countChanged();
        }
    }
    void changedRows(const QSet<QString>& keys, const QList<int>& roles) {
        int first = -1, last = -1, n = 0;
        QList<int> found;
        for (const auto& key : keys) {
            const int row = rowOfKey(key);
            if (row < 0) continue;
            found << row; ++n;
            first = first < 0 ? row : std::min(first, row);
            last = std::max(last, row);
        }
        if (n == 0) return;
        if (n > 64) { emit q->dataChanged(q->index(first), q->index(last), roles); return; }
        for (int row : found) emit q->dataChanged(q->index(row), q->index(row), roles);
    }
};

DirectoryEntryModel::DirectoryEntryModel(QObject* parent) : QAbstractListModel(parent), d(std::make_unique<Private>(this)) {}
DirectoryEntryModel::~DirectoryEntryModel() = default;
int DirectoryEntryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(d->rows.size()); }
QVariant DirectoryEntryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(d->rows.size())) return {};
    const Row& row = d->rows[std::size_t(index.row())];
    switch (role) {
    case NameRole: return row.name;
    case PathRole: return row.path;
    case KindRole: return kindAt(index.row());
    case LinkRole: return row.link;
    case HiddenRole: return row.hidden;
    case SizeRole: return row.size;
    case ModifiedRole: return row.modified < 0 ? QVariant{} : QVariant(QDateTime::fromMSecsSinceEpoch(row.modified));
    case SelectedRole: return d->selected.contains(row.key);
    case CursorRole: return row.key == d->cursorKey;
    case CurrentRole: return !d->currentKey.isEmpty() && row.key == d->currentKey;
    default: return {};
    }
}
QHash<int, QByteArray> DirectoryEntryModel::roleNames() const {
    return {{NameRole, "name"}, {PathRole, "path"}, {KindRole, "kind"}, {LinkRole, "isLink"}, {HiddenRole, "isHidden"},
            {SizeRole, "size"}, {ModifiedRole, "modified"}, {SelectedRole, "isSelected"}, {CursorRole, "isCursor"},
            {CurrentRole, "isCurrent"}};
}
QString DirectoryEntryModel::directory() const { return d->directory; }
int DirectoryEntryModel::count() const { return int(d->rows.size()); }
bool DirectoryEntryModel::loading() const { return d->loading; }
bool DirectoryEntryModel::accessible() const { return d->accessible; }
bool DirectoryEntryModel::truncated() const { return d->truncated; }
bool DirectoryEntryModel::arriving() const { return d->arriving; }
bool DirectoryEntryModel::canLoadMore() const { return d->canLoadMore(); }

void DirectoryEntryModel::setDirectory(const QString& directory) {
    const QString key = keyFor(directory);
    if (key == d->directoryKey) { refresh(); return; }
    const QString previous = d->directoryKey;
    // Only a worker-proven renamed directory may restore these values. Ordinary
    // navigation continues to start with a new selection.
    d->previousSelection=d->selected;d->previousAnchor=d->anchorKey;d->previousCursor=d->cursorKey;
    if (!d->filter.isPersistent() && d->filter.isActive()) {
        d->filter.clear();
        emit filterChanged();
    }
    d->abandonScan();
    d->directory = directory; d->directoryKey = key;
    d->accessible = true; d->truncated = false;
    d->loadingMore = false; d->entryLimit = d->tuning.entryStep;
    d->release();
    d->selected.clear(); d->anchorKey.clear(); d->cursorKey.clear();
    // Going up a level puts the cursor on the folder that was just left, as file managers do.
    if (!previous.isEmpty() && !key.isEmpty() && previous.startsWith(key)
        && (key.endsWith('/') || previous.at(key.size()) == '/') && !previous.mid(key.size() + (key.endsWith('/') ? 0 : 1)).contains('/')) {
        d->cursorKey = d->anchorKey = previous; d->selected.insert(previous);
    }
    emit directoryChanged(); emit selectionChanged();
    if (d->active && !directory.isEmpty()) d->request(); else d->setLoading(false);
}
void DirectoryEntryModel::refresh() { d->refresh(); }
void DirectoryEntryModel::loadMore() {
    if (!d->active || d->arriving || d->loadingMore || !d->canLoadMore()) return;
    d->entryLimit = std::min(d->entryLimit + d->tuning.entryStep, d->tuning.entryLimit);
    d->loadingMore = true;
    d->request();
}
void DirectoryEntryModel::setActive(bool active) {
    if (active == d->active) return;
    d->active = active;
    if (active) { d->request(); return; }
    d->abandonScan();
    const bool couldLoadMore = d->canLoadMore();
    d->loadingMore = false;
    if (couldLoadMore != d->canLoadMore()) emit directoryChanged();
    d->setLoading(false);
    d->release(); // keys (selection/cursor) are kept and re-filtered against the next listing
}
void DirectoryEntryModel::setSort(const QString& key, bool descending, quint64 seed) {
    auto next = sortSpec(key, descending);
    next.seed = seed;
    if (next == d->sort) return;
    d->sort = next;
    d->request();
}
void DirectoryEntryModel::setIncludeHidden(bool include) {
    if (include == d->includeHidden) return;
    d->includeHidden = include;
    d->request();
}

void DirectoryEntryModel::select(int row, SelectMode mode) {
    if (row < 0 || row >= count()) return;
    const QString key = d->rows[std::size_t(row)].key;
    QSet<QString> touched = d->selected;
    const QString oldCursor = d->cursorKey;
    switch (mode) {
    case Replace: d->selected = {key}; d->anchorKey = key; break;
    case Toggle:
        if (d->selected.contains(key)) d->selected.remove(key); else d->selected.insert(key);
        d->anchorKey = key; break;
    case Range: case ExtendRange: {
        // Range (Shift): the selection becomes anchor..row. ExtendRange (Ctrl+Shift): anchor..row is added.
        if (d->anchorKey.isEmpty() || d->rowOfKey(d->anchorKey) < 0) d->anchorKey = key;
        const int anchor = d->rowOfKey(d->anchorKey);
        if (mode == Range) d->selected.clear();
        for (int i = std::min(anchor, row); i <= std::max(anchor, row); ++i) d->selected.insert(d->rows[std::size_t(i)].key);
        break;
    }
    }
    d->cursorKey = key;
    touched.unite(d->selected);
    d->changedRows(touched, {SelectedRole});
    d->changedRows({oldCursor, key}, {CursorRole});
    emit selectionChanged();
}
void DirectoryEntryModel::selectAll() {
    if (d->rows.empty()) return;
    for (const auto& row : d->rows) d->selected.insert(row.key);
    if (d->cursorKey.isEmpty()) d->cursorKey = d->rows.front().key;
    if (d->anchorKey.isEmpty()) d->anchorKey = d->cursorKey;
    emit dataChanged(index(0), index(count() - 1), {SelectedRole, CursorRole});
    emit selectionChanged();
}
void DirectoryEntryModel::clearSelection() {
    if (d->selected.isEmpty()) return;
    const auto previous = d->selected;
    d->selected.clear();
    d->changedRows(previous, {SelectedRole});
    emit selectionChanged();
}
void DirectoryEntryModel::invertSelection() {
    if (d->rows.empty()) return;
    QSet<QString> newSelected;
    for (const auto& row : d->rows) {
        if (!d->selected.contains(row.key)) newSelected.insert(row.key);
    }
    d->selected = std::move(newSelected);
    emit dataChanged(index(0), index(count() - 1), {SelectedRole});
    emit selectionChanged();
}
int DirectoryEntryModel::findMatch(const QString& query, int startRow, bool forward) const {
    if (query.isEmpty() || d->rows.empty()) return -1;
    const int total = int(d->rows.size());
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) return -1;
    int cur = std::clamp(startRow, 0, total - 1);
    for (int step = 0; step < total; ++step) {
        if (d->rows[std::size_t(cur)].name.toLower().contains(q)) return cur;
        cur = forward ? (cur + 1) % total : (cur - 1 + total) % total;
    }
    return -1;
}
int DirectoryEntryModel::matchCount(const QString& query) const {
    if (query.isEmpty() || d->rows.empty()) return 0;
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) return 0;
    int matches = 0;
    for (const auto& row : d->rows) {
        if (row.name.toLower().contains(q)) ++matches;
    }
    return matches;
}
int DirectoryEntryModel::matchIndex(const QString& query, int row) const {
    if (query.isEmpty() || row < 0 || row >= int(d->rows.size())) return 0;
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) return 0;
    int index = 0;
    for (int i = 0; i <= row; ++i) {
        if (d->rows[std::size_t(i)].name.toLower().contains(q)) ++index;
    }
    return index;
}
int DirectoryEntryModel::selectedCount() const {
    if (!d->accessible || d->selected.isEmpty()) return 0;
    int count = 0;
    for (const auto& row : d->rows) {
        if (d->selected.contains(row.key)) ++count;
    }
    return count;
}
qint64 DirectoryEntryModel::selectedBytes() const {
    if (!d->accessible || d->selected.isEmpty()) return 0;
    qint64 total = 0;
    for (const auto& row : d->rows) {
        if (d->selected.contains(row.key) && row.size > 0) {
            total += row.size;
        }
    }
    return total;
}
QString DirectoryEntryModel::selectedBytesFormatted() const {
    return QLocale().formattedDataSize(selectedBytes());
}
QString DirectoryEntryModel::selectedSummary() const {
    const int count = selectedCount();
    if (count <= 0) return {};
    const qint64 bytes = selectedBytes();
    const QString countStr = QString::number(count) + (count == 1 ? QStringLiteral(" item") : QStringLiteral(" items"));
    if (bytes > 0) {
        return QCoreApplication::translate("Grid","Selected: %1 (%2)").arg(countStr, selectedBytesFormatted());
    }
    return QCoreApplication::translate("Grid","Selected: %1").arg(countStr);
}
int DirectoryEntryModel::cursorRow() const { return d->rowOfKey(d->cursorKey); }
QStringList DirectoryEntryModel::selectedPaths() const {
    QStringList paths;
    if (d->selected.isEmpty()) return paths;
    for (const auto& row : d->rows) if (d->selected.contains(row.key)) paths << row.path;
    return paths;
}
int DirectoryEntryModel::rowOf(const QString& path) const { return d->rowOfKey(keyFor(path)); }
QString DirectoryEntryModel::pathAt(int row) const { return row >= 0 && row < count() ? d->rows[std::size_t(row)].path : QString{}; }
QString DirectoryEntryModel::kindAt(int row) const {
    if (row < 0 || row >= count()) return {};
    switch (d->rows[std::size_t(row)].kind) {
    case glancelot::EntryKind::Folder: return QStringLiteral("folder");
    case glancelot::EntryKind::Image: return QStringLiteral("image");
    default: return QStringLiteral("file");
    }
}
QString DirectoryEntryModel::currentImage() const { return d->currentImage; }
void DirectoryEntryModel::setCurrentImage(const QString& path) {
    if (path == d->currentImage) return;
    const QString previous = d->currentKey;
    d->currentImage = path; d->currentKey = keyFor(path);
    if (previous != d->currentKey) d->changedRows({previous, d->currentKey}, {CurrentRole});
    emit currentImageChanged();
}
quint64 DirectoryEntryModel::epoch() const { return d->epoch; }
void DirectoryEntryModel::invalidate() { ++d->epoch; emit epochChanged(); }
QString DirectoryEntryModel::thumbnailSource(int row, int edge) const {
    if (row < 0 || row >= count() || d->rows[std::size_t(row)].kind != glancelot::EntryKind::Image) return {};
    return "image://thumb/" + thumbnail_id::encode(d->epoch, edge, d->rows[std::size_t(row)].path);
}

QString DirectoryEntryModel::filterPattern() const { return d->filter.pattern; }
void DirectoryEntryModel::setFilterPattern(const QString& pattern) {
    if (d->filter.pattern == pattern) return;
    d->filter.pattern = pattern;
    d->applyFilter();
}
bool DirectoryEntryModel::isFilterPersistent() const { return d->filter.isPersistent(); }
void DirectoryEntryModel::setFilterPersistent(bool persistent) {
    const auto mode = persistent ? glancelot::FilterPersistence::Persistent : glancelot::FilterPersistence::Temporary;
    if (d->filter.persistence == mode) return;
    d->filter.persistence = mode;
    emit filterChanged();
}
bool DirectoryEntryModel::hasFilter() const { return d->filtering(); }
QString DirectoryEntryModel::kindFilter() const {
    return d->kindWanted == int(glancelot::EntryKind::Folder) ? QStringLiteral("folder") : d->kindWanted == int(glancelot::EntryKind::Image) ? QStringLiteral("image") : d->kindWanted == int(glancelot::EntryKind::File) ? QStringLiteral("file") : QString();
}
void DirectoryEntryModel::setKindFilter(const QString& kind) {
    const int wanted = kind == "folder" ? int(glancelot::EntryKind::Folder) : kind == "image" ? int(glancelot::EntryKind::Image) : kind == "file" ? int(glancelot::EntryKind::File) : -1;
    if (wanted == d->kindWanted) return;
    d->kindWanted = wanted;
    d->applyFilter();
}
void DirectoryEntryModel::setFilter(const QString& pattern, bool persistent) {
    const auto mode = persistent ? glancelot::FilterPersistence::Persistent : glancelot::FilterPersistence::Temporary;
    if (d->filter.pattern == pattern && d->filter.persistence == mode) return;
    d->filter.pattern = pattern;
    d->filter.persistence = mode;
    d->applyFilter();
}
void DirectoryEntryModel::clearFilter() {
    if (!d->filtering()) return;
    d->filter.clear();
    d->kindWanted = -1;
    d->applyFilter();
}
void DirectoryEntryModel::toggleFilterPersistence() {
    d->filter.persistence = d->filter.isPersistent() ? glancelot::FilterPersistence::Temporary : glancelot::FilterPersistence::Persistent;
    emit filterChanged();
}
