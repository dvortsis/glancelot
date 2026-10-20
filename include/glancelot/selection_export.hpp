#pragma once
#include "file_operations.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Lead-frozen EXPORT-CSV-01 (F070) core contract: explicit, user-initiated export of a selection snapshot to a
// CSV file or a plain UTF-8 path list for downstream tools. Worker-thread only; never called from UI/render code.
//  * The snapshot is taken when the user asks (identity + metadata already read off the UI thread by the caller);
//    no image decoding or metadata parsing happens here.
//  * The engine writes exactly one new file, at the destination the user chose. It never writes, renames or
//    truncates an exported item or any other existing file unless `overwrite` is set, and never a selected item.
//  * Output is written to a temporary sibling and renamed into place; a cancelled or failed export leaves no
//    destination file and no temporary file.
//  * The JSON selection-manifest format belongs to F030 and is out of scope here.
namespace glancelot {
struct ExportItem {
    std::filesystem::path path;      // absolute, lexically normal; as shown to the user
    TargetKind kind{TargetKind::File};
    FileIdentity identity;           // from snapshotTarget(); revalidated before writing when requested
    std::uintmax_t bytes{};          // File items only
    std::int64_t modified{};         // file_time ticks, as in MutationTarget
    // Already-parsed metadata, UTF-8 key/value. Only keys named in ExportRequest::metadataColumns are written.
    std::vector<std::pair<std::string, std::string>> metadata;
};
enum class ExportFormat {
    Csv,      // RFC 4180: comma, CRLF, fields quoted when needed, embedded quotes doubled, header row first
    PathList  // one path per line, LF, no header; a path containing CR or LF is rejected (ExportSkip::Unrepresentable)
};
enum class ExportColumn { Path, Name, Folder, Kind, Bytes, Modified };  // Modified: ISO 8601 UTC, second precision
enum class PathPolicy {
    Absolute,       // native separators
    RelativeToRoot  // '/' separators relative to `root`; an item outside `root` is skipped (OutsideRoot)
};
struct ExportRequest {
    std::uint64_t generation{};      // echoed in the result; the caller drops results whose generation is stale
    std::vector<ExportItem> items;   // written in this order; duplicates (same identity) are written once
    std::filesystem::path destination;
    ExportFormat format{ExportFormat::Csv};
    std::vector<ExportColumn> columns{ExportColumn::Path, ExportColumn::Name, ExportColumn::Kind,
                                      ExportColumn::Bytes, ExportColumn::Modified};
    std::vector<std::string> metadataColumns;  // extra CSV columns after `columns`, header = key; missing -> empty
    PathPolicy pathPolicy{PathPolicy::Absolute};
    std::filesystem::path root;      // required for RelativeToRoot
    bool neutralizeFormulas{true};   // CSV: prefix ' to fields starting with = + - @ TAB or CR (formula injection)
    bool utf8Bom{true};              // CSV only; for spreadsheet applications
    bool revalidate{true};           // re-snapshot each item; Missing/Changed items are skipped and reported
    bool overwrite{false};           // replace an existing destination that is not an exported item
    std::size_t maxItems{1000000};   // more items -> ExportStatus::TooMany, nothing written
};
enum class ExportSkip { Missing, Changed, Duplicate, OutsideRoot, Unrepresentable };
struct SkippedItem { std::filesystem::path path; ExportSkip reason; };
enum class ExportStatus {
    Written,
    Cancelled,          // stop requested; nothing left behind
    DestinationExists,  // and overwrite == false
    DestinationIsItem,  // destination resolves to an exported item (never overwritten, even with overwrite)
    InvalidRequest,     // empty destination, RelativeToRoot without root, no columns, ...
    TooMany,
    Failed              // I/O error; message carries the reason
};
struct ExportResult {
    std::uint64_t generation{};
    ExportStatus status{ExportStatus::Failed};
    std::size_t written{};           // data rows / lines written
    std::vector<SkippedItem> skipped;
    std::string message;
};
// One CSV field (UTF-8 in, UTF-8 out), quoted per RFC 4180 when it contains comma, quote, CR or LF, and with the
// formula-injection prefix applied first when `neutralizeFormulas` is set. Invalid UTF-8 is replaced with U+FFFD.
std::string encodeCsvField(std::string_view utf8, bool neutralizeFormulas);
// Cancellation is honoured between items. `progress(done, total)` is optional and called off the UI thread.
ExportResult exportSelection(const ExportRequest& request, std::stop_token cancel = {},
                             const std::function<void(std::size_t, std::size_t)>& progress = {});
}
