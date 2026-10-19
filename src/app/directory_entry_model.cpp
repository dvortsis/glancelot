#include "glancelot/perf_trace.hpp"
#include "../core/perf_context.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/directory_listing.hpp"
#include "thumbnail_id.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QHash>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
#include <optional>
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
};
Listing buildListing(const QString& directory, const glancelot::SortSpec& sort, bool hidden, std::stop_token cancel, std::size_t limit) {
    Listing result;
    auto listing = glancelot::listDirectory(nativePath(directory), sort, hidden, cancel, limit);
    result.accessible = listing.accessible; result.cancelled = listing.cancelled; result.truncated = listing.truncated;
    if (listing.cancelled || cancel.stop_requested()) { result = Listing{}; result.cancelled = true; return result; }
    QString base = QDir::cleanPath(QDir::fromNativeSeparators(directory));
    if (!base.endsWith('/')) base += '/';
    result.rows.reserve(listing.entries.size());
    result.index.reserve(qsizetype(listing.entries.size()));
    for (const auto& entry : listing.entries) {
        if (cancel.stop_requested()) { result = Listing{}; result.cancelled = true; return result; }
        Row row;
        row.name = displayPath(entry.path.filename());
        row.path = base + row.name;
        row.key = keyFor(row.path);
        row.kind = entry.kind; row.link = entry.link; row.hidden = entry.hidden;
        if (entry.meta.known && entry.kind != glancelot::EntryKind::Folder) row.size = qint64(entry.meta.size);
        row.modified = modifiedMs(entry.meta);
        result.index.insert(row.key, int(result.rows.size()));
        result.rows.push_back(std::move(row));
    }
    return result;
}
}

struct DirectoryEntryModel::Private {
    DirectoryEntryModel* q;
    QString directory, directoryKey, currentImage, currentKey;
    std::vector<Row> rows;
    QHash<QString, int> index;
    QSet<QString> selected;
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
            auto listing = watcher->result(); watcher->deleteLater(); --inFlight;
            { glancelot::perf::ScopedSpan publication(glancelot::perf::Stage::Publish, token);
            publication.setBytes(listing.rows.size());
            if (token != generation || !active) publication.setFlags(glancelot::perf::Stale);
            else if (listing.cancelled) publication.setFlags(glancelot::perf::Cancelled);
            else { if (!listing.accessible) publication.setFlags(glancelot::perf::Failed); apply(std::move(listing)); } }
            pump();
        });
        watcher->setFuture(QtConcurrent::run(pool, [directory = directory, sort = sort, hidden = includeHidden, cancel = stop.get_token(), token] {
            glancelot::perf::detail::GenerationContext context(token);
            return buildListing(directory, sort, hidden, cancel, kMaxEntries);
        }));
    }
    void apply(Listing listing) {
        const bool wasAccessible = accessible, wasTruncated = truncated;
        const int oldCount = int(rows.size());
        accessible = listing.accessible; truncated = listing.truncated;
        merge(std::move(listing));
        // Selection, anchor and cursor are keys: keep those that still exist, drop the rest. A failed listing of the
        // same folder (a transient share or permission error) proves nothing about which entries exist, so the keys
        // are kept for the next successful listing, as setActive(false) does. They are not counted meanwhile.
        if (accessible) for (auto it = selected.begin(); it != selected.end();) { if (index.contains(*it)) ++it; else it = selected.erase(it); }
        if (accessible && !index.contains(anchorKey)) anchorKey.clear();
        if (accessible && !index.contains(cursorKey)) cursorKey.clear();
        setLoading(false);
        if (int(rows.size()) != oldCount) emit q->countChanged();
        if (wasAccessible != accessible || wasTruncated != truncated) emit q->directoryChanged();
        emit q->selectionChanged();
        if (!accessible) emit q->listingFailed(QStringLiteral("This folder can't be opened. It may have been moved or deleted, or you may not have permission to read it."));
    }
    // Same folder relisted: remove vanished rows and insert new ones in place when the surviving order is
    // unchanged, so delegates, scroll position and in-flight previews survive a refresh. Otherwise reset.
    void merge(Listing listing) {
        auto& incoming = listing.rows;
        if (rows.empty() || incoming.empty()) { reset(std::move(listing)); return; }
        std::vector<int> survivors;
        survivors.reserve(incoming.size());
        for (int i = 0; i < int(rows.size()); ++i) if (listing.index.contains(rows[std::size_t(i)].key)) survivors.push_back(i);
        // Retained rows must appear in the same relative order in the new listing.
        int last = -1;
        for (int i : survivors) {
            const int at = listing.index.value(rows[std::size_t(i)].key);
            if (at <= last) { reset(std::move(listing)); return; }
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
        // Metadata or type may have changed for surviving rows.
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
        q->beginResetModel();
        rows = std::move(listing.rows);
        index = std::move(listing.index);
        q->endResetModel();
    }
    void release() {
        if (!rows.empty()) { q->beginResetModel(); rows.clear(); index.clear(); q->endResetModel(); emit q->countChanged(); }
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
int DirectoryEntryModel::selectedCount() const { return d->accessible ? int(d->selected.size()) : 0; }
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
