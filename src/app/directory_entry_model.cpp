#include "glancelot/perf_trace.hpp"
#include <QCoreApplication>
#include "../core/perf_context.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/directory_listing.hpp"
#include "glancelot/filter_state.hpp"
#include "thumbnail_id.hpp"
#include "fs_revision.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#ifdef GLANCELOT_FS_WATCH_TESTING
#include <QElapsedTimer>
#endif
#include <QHash>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
#include <optional>
#include <type_traits>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr int kMaxConcurrentListings = 2;
constexpr std::size_t kMaxEntries = 100000;
// Above this many separate insertion/removal runs a refresh resets instead of merging in place (bounded GUI work).
constexpr int kMaxMergeRuns = 32;

QString displayPath(const fs::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
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
// Stable identity: cleaned absolute spelling with forward slashes, case-folded where the OS is.
QString keyFor(const QString& path) {
    if (path.isEmpty()) return {};
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
qint64 modifiedMs(const glancelot::FileMeta& meta) {
    if (!meta.known) return -1;
    using namespace std::chrono;
    const auto stamp = fs::file_time_type(fs::file_time_type::duration(meta.modified));
    // Portable conversion (no clock_cast/to_sys, which older standard libraries lack): offset by "now" on both
    // clocks. Display-only, so the microsecond skew between the two now() calls is irrelevant.
    const auto system = system_clock::now() + duration_cast<system_clock::duration>(stamp - fs::file_time_type::clock::now());
    return duration_cast<milliseconds>(system.time_since_epoch()).count();
}
glancelot::SortSpec sortSpec(const QString& key, bool descending) {
    glancelot::SortSpec spec;
    spec.key = key == "modified" ? glancelot::SortKey::Modified : key == "size" ? glancelot::SortKey::Size
             : key == "shuffle" ? glancelot::SortKey::Shuffle : glancelot::SortKey::Name;
    spec.descending = descending;
    return spec;
}

struct Row {
    QString path, name, key;
    glancelot::EntryKind kind{glancelot::EntryKind::File};
    bool link{}, hidden{};
    qint64 size{-1}, modified{-1};
    bool operator==(const Row&) const = default;
};
// Built entirely on the worker: strings, keys and the key->row index. The GUI thread only swaps it in.
struct Listing {
    std::vector<Row> rows;
    QHash<QString, int> index;
    bool accessible{true}, cancelled{}, truncated{};
    fs_watch::Snapshot identities;
    QHash<QString,QString> renamed;
    bool renamedDirectory{};
};
Listing buildListing(const QString& directory, const glancelot::SortSpec& sort, bool hidden, std::stop_token cancel, std::size_t limit,
                     const fs_watch::Snapshot& prior) {
    Listing result;
    fs_watch::Revision directoryEntryRevision;fs::path resolvedDirectory;
    const auto directoryRevision=fs_watch::directoryProbe(nativePath(directory),&directoryEntryRevision,&resolvedDirectory);
    auto listing = glancelot::listDirectory(resolvedDirectory.empty()?nativePath(directory):resolvedDirectory, sort, hidden, cancel, limit);
    result.accessible = listing.accessible; result.cancelled = listing.cancelled; result.truncated = listing.truncated;
    if (listing.cancelled || cancel.stop_requested()) { result = Listing{}; result.cancelled = true; return result; }
    QString base = QDir::cleanPath(QDir::fromNativeSeparators(directory));
    if (!base.endsWith('/')) base += '/';
    result.rows.reserve(listing.entries.size());
    result.index.reserve(qsizetype(listing.entries.size()));
    result.identities.directory=directory;
    result.identities.revision=directoryRevision;result.identities.entryRevision=directoryEntryRevision;
    result.identities.complete=listing.accessible&&!listing.truncated&&directoryRevision.known&&
        directoryRevision.directory&&!directoryRevision.link;
    // Even an empty filename costs at least this much identity storage. If the
    // whole listing cannot fit, probing a large prefix cannot establish a
    // complete rename baseline and only adds avoidable native I/O and memory.
    const auto minimumIdentityCost=fs_watch::entryBytes(base);
    if(listing.entries.size()>fs_watch::IdentityByteBudget/minimumIdentityCost)
        result.identities.complete=false;
    std::size_t identityBytes=0;
    for (const auto& entry : listing.entries) {
        if (cancel.stop_requested()) { result = Listing{}; result.cancelled = true; return result; }
        Row row;
        row.name = displayPath(entry.path.filename());
        row.path = base + row.name;
        row.key = keyFor(row.path);
        const auto identityCost=fs_watch::entryBytes(row.path);
        if(identityCost>fs_watch::IdentityByteBudget-identityBytes)result.identities.complete=false;
        if(result.identities.complete) {
            const auto revision=fs_watch::probe(entry.path);
            result.identities.complete=revision.known;identityBytes+=identityCost;
            result.identities.entries.insert(row.key,{row.path,revision});
        }
        row.kind = entry.kind; row.link = entry.link; row.hidden = entry.hidden;
        if (entry.meta.known && entry.kind != glancelot::EntryKind::Folder) row.size = qint64(entry.meta.size);
        row.modified = modifiedMs(entry.meta);
        result.index.insert(row.key, int(result.rows.size()));
        result.rows.push_back(std::move(row));
    }
    result.identities.complete=result.identities.complete&&
        fs_watch::directoryProbe(nativePath(directory))==directoryRevision&&
        fs_watch::probe(nativePath(directory))==directoryEntryRevision;
    const auto directoryIdentity=directoryEntryRevision.identity();
    result.renamedDirectory=keyFor(prior.directory)!=keyFor(directory)&&!directoryIdentity.isEmpty()&&
        directoryIdentity==prior.entryRevision.identity()&&
        fs_watch::directoryProbe(nativePath(prior.directory)).identity()!=directoryIdentity;
    if(keyFor(prior.directory)==keyFor(directory)||result.renamedDirectory)
        result.renamed=fs_watch::renames(prior,result.identities);
    return result;
}
}

struct DirectoryEntryModel::Private {
    DirectoryEntryModel* q;
#ifdef GLANCELOT_FS_WATCH_TESTING
    std::function<void(const fs_watch::ModelPublicationStats&)> publicationTiming=fs_watch::modelPublicationTiming;
    fs_watch::ModelPublicationStats publicationStats;
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
    // Heap-allocated so a listing wedged in the OS (dead network share) can be abandoned at teardown.
    QThreadPool* pool = new QThreadPool;

    explicit Private(DirectoryEntryModel* owner) : q(owner) { pool->setMaxThreadCount(kMaxConcurrentListings); }
    ~Private() {
        stop.request_stop();
        if (pool->waitForDone(1000)) delete pool;
        // else: abandon the pool; its job owns only by-value captures.
    }
    int rowOfKey(const QString& key) const { return key.isEmpty() ? -1 : index.value(key, -1); }
    void setLoading(bool value) { if (loading != value) { loading = value; emit q->loadingChanged(); } }
    void applyFilter() {
        q->beginResetModel();
        rows.clear();
        index.clear();
        if (!filtering()) {
            rows = rawListing.rows;
            index = rawListing.index;
        } else {
            rows.reserve(rawListing.rows.size());
            index.reserve(rawListing.index.size());
            for (const auto& row : rawListing.rows) {
                if ((!filter.isActive() || filter.matches(row.name)) && (kindWanted < 0 || int(row.kind) == kindWanted)) {
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
        stop.request_stop(); stop = std::stop_source{};
        ++generation;
        wanted = true;
        setLoading(true);
        pump();
    }
    void pump() {
        if (!wanted || inFlight >= kMaxConcurrentListings) return;
        wanted = false;
        ++inFlight;
        const quint64 token = generation;
        auto* watcher = new QFutureWatcher<Listing>(q);
        QObject::connect(watcher, &QFutureWatcher<Listing>::finished, q, [this, watcher, token] {
#ifdef GLANCELOT_FS_WATCH_TESTING
            QElapsedTimer publicationClock;publicationClock.start();
            publicationStats={};
#endif
            { glancelot::perf::ScopedSpan publication(glancelot::perf::Stage::Publish, token);
            // Single consumer, reached only after finished: move the rows out
            // instead of deep-copying a 100k-row vector on the GUI thread and
            // leaving a second copy for deferred watcher destruction.
            static_assert(std::is_nothrow_move_constructible_v<Listing>);
            auto listing = watcher->future().takeResult(); watcher->deleteLater(); --inFlight;
#ifdef GLANCELOT_FS_WATCH_TESTING
            publicationStats.rows=qsizetype(listing.rows.size());
            publicationStats.observedIdentities=listing.identities.entries.size();
            publicationStats.complete=listing.identities.complete;
#endif
            publication.setBytes(listing.rows.size());
            if (token != generation || !active) publication.setFlags(glancelot::perf::Stale);
            else if (listing.cancelled) publication.setFlags(glancelot::perf::Cancelled);
            else { if (!listing.accessible) publication.setFlags(glancelot::perf::Failed); apply(std::move(listing)); } }
            pump();
#ifdef GLANCELOT_FS_WATCH_TESTING
            publicationStats.totalMs=publicationClock.elapsed();
            publicationStats.acceptedIdentities=identities.entries.size();
            if(publicationTiming)publicationTiming(publicationStats);
#endif
        });
        watcher->setFuture(QtConcurrent::run(pool, [directory = directory, sort = sort, hidden = includeHidden, cancel = stop.get_token(), token, prior=identities] {
            glancelot::perf::detail::GenerationContext context(token);
            return buildListing(directory, sort, hidden, cancel, kMaxEntries,prior);
        }));
    }
    void apply(Listing listing) {
#ifdef GLANCELOT_FS_WATCH_TESTING
        QElapsedTimer phase;phase.start();
#endif
        const bool wasAccessible = accessible, wasTruncated = truncated;
        accessible = listing.accessible; truncated = listing.truncated;
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
        const int oldCount = int(rows.size());
        // Filtering is a projection: identity matching/pruning uses the full
        // physical listing, never just the visible rows.
        if (!truncated || (filtering() ? rawListing.rows.empty() : rows.empty())) {
#ifdef GLANCELOT_FS_WATCH_TESTING
            publicationStats.applied=true;publicationStats.filtered=filtering();
#endif
            if (!filtering()) {
                merge(std::move(listing));
#ifdef GLANCELOT_FS_WATCH_TESTING
                publicationStats.mergeMs=phase.restart();
#endif
                rawListing.rows = rows;
                rawListing.index = index;
                if (int(rows.size()) != oldCount) emit q->countChanged();
            } else {
                rawListing = std::move(listing);
                applyFilter();
            }
        }
#ifdef GLANCELOT_FS_WATCH_TESTING
        publicationStats.projectionMs=phase.restart();
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

        setLoading(false);
        if (wasAccessible != accessible || wasTruncated != truncated) emit q->directoryChanged();
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
    void release() {
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
    d->stop.request_stop(); ++d->generation; d->wanted = false;
    d->directory = directory; d->directoryKey = key;
    d->accessible = true; d->truncated = false;
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
void DirectoryEntryModel::refresh() { d->request(); }
void DirectoryEntryModel::setActive(bool active) {
    if (active == d->active) return;
    d->active = active;
    if (active) { d->request(); return; }
    d->stop.request_stop(); ++d->generation; d->wanted = false;
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
