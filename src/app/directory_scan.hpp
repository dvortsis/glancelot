#pragma once
// Private A1 helper (not a public contract): the worker side of DirectoryEntryModel's folder listing.
// Worker-thread only; nothing here touches a model, a QObject or the GUI thread. The model hands a scan its
// parameters by value and shares only an Arrivals queue with it, so a worker wedged in the OS can outlive the model.
//  * enumerate() is glancelot::listDirectory's classification as a stream: tests/perf_enum_tests.cpp proves parity.
//  * ScanOrder sorts with keys built once per entry instead of re-deriving file names in every comparison.
//  * Arrivals is a bounded queue. A full queue makes the worker wait; the stop token always wakes it.
#include "glancelot/directory_listing.hpp"
#include "glancelot/perf_trace.hpp"
#include "../core/link_policy.hpp"
#include "fs_revision.hpp"
#include <QDir>
#include <QFile>
#include <QHash>
#include <QString>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <stop_token>
#include <vector>

namespace directory_scan {
namespace fs = std::filesystem;

// Experimental starting points (docs/PERFORMANCE-STRATEGY.md): see docs/handoff/A1-HANDOFF.md for the traces.
struct Tuning {
    int patienceMs{120};      // a listing finishing sooner publishes once, with no provisional rows
    int slowFloorMs{1000};    // past this, even a small slow listing shows what it has
    int minArrivalRows{2048}; // below this (and the floor) rows wait for the definitive order
    int batchRows{2048};      // most rows in one batch
    int flushMs{100};         // a slow source still delivers what it has this often
    int queuedRows{16384};    // the worker waits rather than queue more rows than this
};

inline QString displayPath(const fs::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QFile::decodeName(path.native().c_str());
#endif
}
inline fs::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
// Stable identity: cleaned absolute spelling with forward slashes, case-folded where the OS is.
inline QString keyFor(const QString& path) {
    if (path.isEmpty()) return {};
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
inline qint64 modifiedMs(const glancelot::FileMeta& meta) {
    if (!meta.known) return -1;
    using namespace std::chrono;
    const auto stamp = fs::file_time_type(fs::file_time_type::duration(meta.modified));
    // Portable conversion (no clock_cast/to_sys, which older standard libraries lack): offset by "now" on both
    // clocks. Display-only, so the microsecond skew between the two now() calls is irrelevant.
    const auto system = system_clock::now() + duration_cast<system_clock::duration>(stamp - fs::file_time_type::clock::now());
    return duration_cast<milliseconds>(system.time_since_epoch()).count();
}

struct Row {
    QString path, name, key;
    glancelot::EntryKind kind{glancelot::EntryKind::File};
    bool link{}, hidden{};
    qint64 size{-1}, modified{-1};
    bool operator==(const Row&) const = default;
};
inline Row makeRow(const glancelot::DirectoryEntry& entry, const QString& base) {
    Row row;
    row.name = displayPath(entry.path.filename());
    row.path = base + row.name;
    row.key = keyFor(row.path);
    row.kind = entry.kind; row.link = entry.link; row.hidden = entry.hidden;
    if (entry.meta.known && entry.kind != glancelot::EntryKind::Folder) row.size = qint64(entry.meta.size);
    row.modified = modifiedMs(entry.meta);
    return row;
}

// Built entirely on the worker: strings, keys and the key->row index. The GUI thread only swaps it in.
struct Listing {
    std::vector<Row> rows;
    QHash<QString, int> index;
    bool accessible{true}, cancelled{}, truncated{};
    bool rowsPublished{};   // the rows already went out as the definitive batch; this result carries only identities
    qsizetype rowCount{};
    fs_watch::Snapshot identities;
    QHash<QString, QString> renamed;
    bool renamedDirectory{};
};
// Rows in a batch are already sorted among themselves; folders stay apart because they publish before every file.
// The last batch of an arrival instead carries the definitive listing.
struct Batch {
    std::vector<Row> folders, files;
    std::optional<Listing> definitive;
};

class Arrivals {
public:
    explicit Arrivals(std::size_t capacityRows) : capacity_(capacityRows) {}
    // False when stopped. A batch larger than the capacity still fits into an empty queue, so progress never stalls.
    bool push(Batch batch, std::stop_token stop) {
        const auto rows = batch.folders.size() + batch.files.size();
        std::unique_lock lock(mutex_);
        if (!changed_.wait(lock, stop, [&] { return queue_.empty() || queuedRows_ + rows <= capacity_; })) return false;
        queuedRows_ += rows;
        queue_.push_back(std::move(batch));
        return true;
    }
    std::optional<Batch> take() {
        std::optional<Batch> batch;
        {
            std::scoped_lock lock(mutex_);
            if (queue_.empty()) return batch;
            batch = std::move(queue_.front());
            queue_.pop_front();
            queuedRows_ -= batch->folders.size() + batch->files.size();
        }
        changed_.notify_all();
        return batch;
    }
private:
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::deque<Batch> queue_;
    std::size_t queuedRows_{}, capacity_;
};

// A key whose unit-by-unit order equals glancelot::naturalLess on file names, up to its raw-name tie break: ASCII
// folded, and each numeric run as one digit-range marker, its significant length, then its significant digits. A
// literal digit never appears outside a run, so the marker compares against other units exactly as any digit does.
using NameKey = fs::path::string_type;
inline NameKey nameKey(const fs::path::string_type& name) {
    using Unit = fs::path::value_type;
    constexpr auto digit = [](Unit c) { return c >= Unit('0') && c <= Unit('9'); };
    NameKey key;
    key.reserve(name.size() + 4);
    for (std::size_t i = 0; i < name.size();) {
        if (digit(name[i])) {
            auto end = i;
            while (end < name.size() && digit(name[end])) ++end;
            auto first = i;
            while (first < end && name[first] == Unit('0')) ++first;
            const auto length = end - first;
            key.push_back(Unit('0'));
            key.push_back(Unit('0' + (length >> 6) % 64));
            key.push_back(Unit('0' + length % 64));
            key.append(name, first, length);
            i = end;
        } else {
            const auto c = name[i++];
            key.push_back(c >= Unit('A') && c <= Unit('Z') ? Unit(c + ('a' - 'A')) : c);
        }
    }
    return key;
}
inline int compareKeys(const NameKey& a, const NameKey& b) {
    // Plain unit comparison as glancelot::naturalLess does it, not char_traits (which is unsigned for char).
    const auto size = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < size; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}

struct Cancelled {};
// The listing order: folders first in natural order, then files in `sort` order. Entries and keys are parallel arrays
// owned by the scan; sorts move only indices. Keys it has no fast form for go through glancelot::fileSortBefore.
class ScanOrder {
public:
    ScanOrder(const std::vector<glancelot::DirectoryEntry>& entries, const std::vector<NameKey>& keys,
              const glancelot::SortSpec& sort, std::stop_token cancel)
        : entries_(entries), keys_(keys), sort_(sort), cancel_(cancel) {}
    bool folderBefore(std::uint32_t a, std::uint32_t b) const {
        if (cancel_.stop_requested()) throw Cancelled{};
        return naturalBefore(a, b);
    }
    bool fileBefore(std::uint32_t a, std::uint32_t b) const {
        if (cancel_.stop_requested()) throw Cancelled{};
        const auto& ea = entries_[a]; const auto& eb = entries_[b];
        using glancelot::SortKey;
        switch (sort_.key) {
        case SortKey::Name: return sort_.descending ? naturalBefore(b, a) : naturalBefore(a, b);
        case SortKey::Modified: case SortKey::Size: {
            if (ea.meta.known != eb.meta.known) return ea.meta.known;
            if (ea.meta.known) {
                int order = 0;
                if (sort_.key == SortKey::Modified) order = ea.meta.modified < eb.meta.modified ? -1 : ea.meta.modified > eb.meta.modified ? 1 : 0;
                else order = ea.meta.size < eb.meta.size ? -1 : ea.meta.size > eb.meta.size ? 1 : 0;
                if (order != 0) return sort_.descending ? order > 0 : order < 0;
            }
            return naturalBefore(a, b);
        }
        default: return glancelot::fileSortBefore(sort_, ea.path, ea.meta, eb.path, eb.meta);
        }
    }
    bool isFolder(std::uint32_t at) const { return entries_[at].kind == glancelot::EntryKind::Folder; }
    // Sorts `order` in place, folders first. Returns the folder count; throws Cancelled.
    std::size_t sort(std::vector<std::uint32_t>& order) const {
        const auto split = std::stable_partition(order.begin(), order.end(), [&](std::uint32_t at) { return isFolder(at); });
        std::sort(order.begin(), split, [&](std::uint32_t a, std::uint32_t b) { return folderBefore(a, b); });
        std::sort(split, order.end(), [&](std::uint32_t a, std::uint32_t b) { return fileBefore(a, b); });
        return std::size_t(split - order.begin());
    }
private:
    bool naturalBefore(std::uint32_t a, std::uint32_t b) const {
        const auto comparison = compareKeys(keys_[a], keys_[b]);
        if (comparison != 0) return comparison < 0;
        return glancelot::naturalLess(entries_[a].path, entries_[b].path);
    }
    const std::vector<glancelot::DirectoryEntry>& entries_;
    const std::vector<NameKey>& keys_;
    const glancelot::SortSpec& sort_;
    std::stop_token cancel_;
};

struct Outcome { bool accessible{true}, cancelled{}, truncated{}; };
// Returns false from the sink to stop early (the entry limit).
using Sink = std::function<bool(glancelot::DirectoryEntry&&)>;
using Enumerator = std::function<Outcome(const fs::path&, bool, std::stop_token, std::size_t, const Sink&)>;
// One directory level, one pass, the same policy as glancelot::listDirectory (see the parity test). `limit` caps the
// admitted entries; reaching it with more entries left sets `truncated`.
inline Outcome enumerate(const fs::path& directory, bool includeHidden, std::stop_token cancel, std::size_t limit, const Sink& sink) {
    namespace perf = glancelot::perf;
    using glancelot::DirectoryEntry; using glancelot::EntryKind; using glancelot::FileMeta;
    perf::ScopedSpan timing(perf::Stage::Enumerate);
    Outcome result;
    std::size_t admitted = 0;
    const auto cancelled = [&] { timing.setFlags(perf::Cancelled); result = {}; result.cancelled = true; return result; };
    const auto failed = [&] { timing.setFlags(perf::Failed); result = {}; result.accessible = false; return result; };
    if (cancel.stop_requested()) return cancelled();
    // Same policy as the tree and traversal: a folder reached through a link is not listed.
    if (directory.empty() || !glancelot::detail::safePath(directory, cancel)) {
        if (cancel.stop_requested()) return cancelled();
        return failed();
    }
    const auto admit = [&](DirectoryEntry&& entry) {
        if (entry.hidden && !includeHidden) return true;
        if (admitted >= limit) { result.truncated = true; return false; }
        ++admitted;
        return sink(std::move(entry));
    };
#ifdef _WIN32
    WIN32_FIND_DATAW data{};
    const auto pattern = directory / L"*";
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        // A drive root may legitimately contain nothing at all; everything else is "cannot list".
        if (GetLastError() != ERROR_FILE_NOT_FOUND) return failed();
        return result;
    }
    bool stopped = false;
    do {
        if (cancel.stop_requested()) { FindClose(find); return cancelled(); }
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        const DWORD attributes = data.dwFileAttributes;
        if (attributes & FILE_ATTRIBUTE_DEVICE) continue;
        DirectoryEntry entry;
        entry.path = directory / data.cFileName;
        entry.link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        entry.hidden = (attributes & FILE_ATTRIBUTE_HIDDEN) || name.front() == L'.';
        const bool folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.kind = folder ? EntryKind::Folder : (!entry.link && glancelot::isSupportedImage(entry.path) ? EntryKind::Image : EntryKind::File);
        // FILETIME ticks are file_clock ticks on MSVC, so ordering matches Navigator's metadata exactly.
        entry.meta = FileMeta{std::int64_t((std::uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime),
                              std::uintmax_t((std::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow), true};
        if (!admit(std::move(entry))) { stopped = true; break; }
    } while (FindNextFileW(find, &data));
    const DWORD enumerationError = stopped ? ERROR_NO_MORE_FILES : GetLastError();
    FindClose(find);
    // A disconnect or read failure after some entries is not a complete folder listing.
    if (enumerationError != ERROR_NO_MORE_FILES) return failed();
#else
    std::error_code ec;
    // No skip_permission_denied: an inaccessible folder must not look empty.
    fs::directory_iterator it(directory, ec), end;
    if (ec) return failed();
    while (it != end) {
        if (cancel.stop_requested()) return cancelled();
        DirectoryEntry entry;
        entry.path = it->path();
        std::error_code statusError;
        const auto own = it->symlink_status(statusError);
        bool keep = !statusError;
        if (keep) {
            entry.link = fs::is_symlink(own);
            const auto target = entry.link ? it->status(statusError) : own;
            if (fs::is_directory(target) && !statusError) entry.kind = EntryKind::Folder;
            else if (entry.link && (statusError || fs::is_regular_file(target))) entry.kind = EntryKind::File; // dangling links stay visible
            else if (!entry.link && fs::is_regular_file(target)) entry.kind = glancelot::isSupportedImage(entry.path) ? EntryKind::Image : EntryKind::File;
            else keep = false; // devices, sockets, FIFOs (and links to them)
        }
        if (keep) {
            const auto filename = entry.path.filename();
            entry.hidden = !filename.native().empty() && filename.native().front() == '.';
            std::error_code sizeError, timeError;
            const auto size = entry.kind == EntryKind::Folder ? std::uintmax_t{} : it->file_size(sizeError);
            const auto time = it->last_write_time(timeError);
            entry.meta = FileMeta{timeError ? 0 : std::int64_t(time.time_since_epoch().count()), sizeError ? 0 : size, !sizeError && !timeError};
            if (!admit(std::move(entry))) break;
        }
        it.increment(ec);
        if (ec) return failed();
    }
#endif
    timing.setBytes(admitted);
    return result;
}

struct Request {
    QString directory;
    glancelot::SortSpec sort;
    bool includeHidden{};
    std::size_t limit{};
    fs_watch::Snapshot prior;
    Tuning tuning;
    Enumerator enumerator;   // empty: the real filesystem
    std::function<void(std::stop_token)> beforeProbes; // empty outside perf_enum tests
};
#ifdef GLANCELOT_PERF_ENUM_TESTING
// Copied at model construction; only the perf_enum_tests executable compiles this.
struct TestSeam { Tuning tuning; Enumerator enumerator; std::function<void(std::stop_token)> beforeProbes; };
inline thread_local TestSeam testSeam;
#endif
// `arrivals` is set for a first listing with no rows on screen: past the patience window, batches go there as they
// are read, and the definitive order follows before the per-file identity probes, which can take far longer than
// everything else together. A refresh passes null, keeps what is shown and publishes only the finished listing.
inline Listing scan(const Request& request, const std::shared_ptr<Arrivals>& arrivals, std::stop_token cancel) {
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    Listing result;
    const auto cancelledResult = [] { Listing listing; listing.cancelled = true; return listing; };
    fs_watch::Revision directoryEntryRevision; fs::path resolvedDirectory;
    const auto directoryRevision = fs_watch::directoryProbe(nativePath(request.directory), &directoryEntryRevision, &resolvedDirectory);
    QString base = QDir::cleanPath(QDir::fromNativeSeparators(request.directory));
    if (!base.endsWith('/')) base += '/';

    // Parallel arrays, indexed by arrival; sorts move only indices.
    std::vector<glancelot::DirectoryEntry> entries;
    std::vector<NameKey> keys;
    std::vector<Row> rows;
    const ScanOrder order(entries, keys, request.sort, cancel);
    std::size_t flushed = 0;
    bool open = false, stopped = false;
    auto lastFlush = started;
    const auto elapsedMs = [](clock::time_point since) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - since).count();
    };
    // Delivers entries [flushed, entries.size()) as batches of at most batchRows.
    const auto flush = [&]() {
        const auto batchRows = std::size_t(std::max(1, request.tuning.batchRows));
        while (flushed < entries.size() && !stopped) {
            const auto end = std::min(entries.size(), flushed + batchRows);
            std::vector<std::uint32_t> local(end - flushed);
            std::iota(local.begin(), local.end(), std::uint32_t(flushed));
            std::size_t folders = 0;
            // Never let this throw through the enumerator's open directory handle.
            try { folders = order.sort(local); } catch (const Cancelled&) { stopped = true; return; }
            Batch batch;
            batch.folders.reserve(folders); batch.files.reserve(local.size() - folders);
            for (std::size_t i = 0; i < local.size(); ++i) (i < folders ? batch.folders : batch.files).push_back(rows[local[i]]);
            if (!arrivals->push(std::move(batch), cancel)) stopped = true;
            flushed = end;
        }
        lastFlush = clock::now();
    };
    const auto sink = [&](glancelot::DirectoryEntry&& entry) {
        keys.push_back(nameKey(entry.path.filename().native()));
        rows.push_back(makeRow(entry, base));
        entries.push_back(std::move(entry));
        if (arrivals) {
            const auto& tuning = request.tuning;
            if (!open) {
                const auto waited = elapsedMs(started);
                open = waited >= tuning.slowFloorMs || (waited >= tuning.patienceMs && int(entries.size()) >= tuning.minArrivalRows);
            }
            if (open && (entries.size() - flushed >= std::size_t(tuning.batchRows) || elapsedMs(lastFlush) >= tuning.flushMs)) flush();
        }
        return !stopped;
    };
    const auto directory = resolvedDirectory.empty() ? nativePath(request.directory) : resolvedDirectory;
    const Outcome outcome = request.enumerator ? request.enumerator(directory, request.includeHidden, cancel, request.limit, sink)
                                               : enumerate(directory, request.includeHidden, cancel, request.limit, sink);
    if (outcome.cancelled || stopped || cancel.stop_requested()) return cancelledResult();
    result.accessible = outcome.accessible; result.truncated = outcome.truncated;
    if (!outcome.accessible) { result.identities.directory = request.directory; return result; }
    if (open) flush(); // every row is on the queue before the definitive order is computed
    if (stopped) return cancelledResult();

    std::vector<std::uint32_t> sorted(entries.size());
    std::iota(sorted.begin(), sorted.end(), 0U);
    try {
        glancelot::perf::ScopedSpan timing(glancelot::perf::Stage::Sort);
        timing.setBytes(entries.size());
        order.sort(sorted);
    } catch (const Cancelled&) { return cancelledResult(); }
    if (cancel.stop_requested()) return cancelledResult();

    result.rows.reserve(entries.size());
    result.index.reserve(qsizetype(entries.size()));
    for (const auto at : sorted) {
        result.index.insert(rows[at].key, int(result.rows.size()));
        result.rows.push_back(std::move(rows[at]));
    }
    result.rowCount = qsizetype(result.rows.size());
    result.identities.directory = request.directory;
    result.identities.revision = directoryRevision; result.identities.entryRevision = directoryEntryRevision;
    result.identities.complete = outcome.accessible && !outcome.truncated && directoryRevision.known &&
        directoryRevision.directory && !directoryRevision.link;
    // Even an empty filename costs at least this much identity storage. If the
    // whole listing cannot fit, probing a large prefix cannot establish a
    // complete rename baseline and only adds avoidable native I/O and memory.
    const auto minimumIdentityCost = fs_watch::entryBytes(base);
    if (entries.size() > fs_watch::IdentityByteBudget / minimumIdentityCost) result.identities.complete = false;

    std::vector<Row> probing; // what the probes below still need after the rows have been handed over
    if (arrivals) {
        Listing definitive;
        definitive.truncated = result.truncated; definitive.rowCount = result.rowCount;
        if (result.identities.complete) probing = result.rows;
        definitive.rows = std::move(result.rows); definitive.index = std::move(result.index);
        result.rows = {}; result.index = {};
        Batch batch;
        batch.definitive = std::move(definitive);
        if (!arrivals->push(std::move(batch), cancel)) return cancelledResult();
        result.rowsPublished = true;
    }
    if (request.beforeProbes) request.beforeProbes(cancel);
    const auto& probed = arrivals ? probing : result.rows;
    std::size_t identityBytes = 0;
    for (std::size_t i = 0; result.identities.complete && i < probed.size(); ++i) {
        if (cancel.stop_requested()) return cancelledResult();
        const auto& row = probed[i];
        const auto identityCost = fs_watch::entryBytes(row.path);
        if (identityCost > fs_watch::IdentityByteBudget - identityBytes) { result.identities.complete = false; break; }
        const auto revision = fs_watch::probe(entries[sorted[i]].path);
        result.identities.complete = revision.known; identityBytes += identityCost;
        result.identities.entries.insert(row.key, {row.path, revision});
    }
    if (cancel.stop_requested()) return cancelledResult();
    result.identities.complete = result.identities.complete &&
        fs_watch::directoryProbe(nativePath(request.directory)) == directoryRevision &&
        fs_watch::probe(nativePath(request.directory)) == directoryEntryRevision;
    const auto directoryIdentity = directoryEntryRevision.identity();
    const auto& prior = request.prior;
    result.renamedDirectory = keyFor(prior.directory) != keyFor(request.directory) && !directoryIdentity.isEmpty() &&
        directoryIdentity == prior.entryRevision.identity() &&
        fs_watch::directoryProbe(nativePath(prior.directory)).identity() != directoryIdentity;
    if (keyFor(prior.directory) == keyFor(request.directory) || result.renamedDirectory)
        result.renamed = fs_watch::renames(prior, result.identities);
    return result;
}
}
