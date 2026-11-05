// PHASE2-CONTRACTS: the seven frozen contracts' headers compile, and their frozen values cannot drift unnoticed.
// A static_assert below stops the build; the runtime checks run under ctest. See cmake/Phase2ContractTests.cmake.
#include "glancelot/asset_group.hpp"
#include "glancelot/capability_model.hpp"
#include "glancelot/compare_panes.hpp"
#include "glancelot/contracts.hpp"
#include "glancelot/directory_listing.hpp"
#include "glancelot/navigation.hpp"
#include "glancelot/operation_journal.hpp"
#include "glancelot/resource_ledger.hpp"
#include <QTemporaryDir>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

using namespace glancelot;
namespace fs = std::filesystem;

// META-ROLES: appended values never renumber the old ones.
static_assert(int(SortKey::Name) == 0 && int(SortKey::Modified) == 1 && int(SortKey::Size) == 2 && int(SortKey::Shuffle) == 3);
static_assert(int(SortKey::Captured) == 4 && int(SortKey::Dimensions) == 5);
// Positional FileMeta{modified, size, known} stays valid and leaves the extended keys missing.
static_assert(!FileMeta{1, 2, true}.capturedKnown && !FileMeta{1, 2, true}.dimensionsKnown);
static_assert(std::is_invocable_r_v<bool, SortKeyReader, const std::vector<fs::path>&, std::vector<FileMeta>&, std::stop_token>);

// DECODE-TIER: Preview < Fit < Full, Tile outside the ladder, and a default request or descriptor means today's behaviour.
static_assert(tierRank(DecodeTier::Preview) < tierRank(DecodeTier::Fit) && tierRank(DecodeTier::Fit) < tierRank(DecodeTier::Full));
static_assert(tierRank(DecodeTier::Tile) == 0);
static_assert(parseDecodeTier(decodeTierName(DecodeTier::Preview)) == DecodeTier::Preview);
static_assert(parseDecodeTier(decodeTierName(DecodeTier::Fit)) == DecodeTier::Fit);
static_assert(parseDecodeTier(decodeTierName(DecodeTier::Full)) == DecodeTier::Full);
static_assert(parseDecodeTier(decodeTierName(DecodeTier::Tile)) == DecodeTier::Tile);
static_assert(!parseDecodeTier("FULL") && !parseDecodeTier(""));
static_assert(DecodeRegion{}.empty() && !DecodeRegion{0, 0, 1, 1}.empty());
static_assert(kFitBucketMinEdge == 512 && kFitBucketMaxEdge == 8192);
static_assert(std::is_same_v<decltype(DecodeRequest::tier), DecodeTier> && std::is_same_v<decltype(PixelBufferDescriptor::tier), DecodeTier>);

// RESOURCE-LEDGER: the ledgers and bounds consumers size against.
static_assert(int(resource::Ledger::GpuTextures) + 1 == resource::kLedgerCount);
static_assert(resource::kUnknown < 0 && resource::kMaxForegroundHeadroomBytes == 1LL << 30);
// With every source unknown, the largest validated decode still fits.
static_assert(resource::kDefaultFallbackBytes - resource::kDefaultSafetyReserveBytes >= resource::kMaxForegroundHeadroomBytes);
// A reservation made before the allocation names its share key when it commits.
static_assert(std::is_same_v<decltype(&resource::Reservation::commit), bool (resource::Reservation::*)(qint64, quint64)>);
static_assert(!std::is_copy_constructible_v<resource::Reservation> && std::is_nothrow_move_constructible_v<resource::Reservation>);
static_assert(!resource::OwnerId{});

// ASSET-GROUP: nothing is implicit, so the scope enum has exactly the explicit choices and file-only comes first.
static_assert(int(assets::Scope::FileOnly) == 0);
static_assert(assets::kMaxMembers == 16 && assets::kExpandBudgetMs > 0 && assets::kMaxExcludedListed > 0);
static_assert(assets::kMaxExpandedItems == journal::kMaxItemsPerOperation);

// OPERATION-JOURNAL: what may run without a journal, and a displaced original is never treated as merely interrupted.
static_assert(!journal::requiresJournal(journal::Kind::Trash) && !journal::requiresJournal(journal::Kind::Copy)
              && !journal::requiresJournal(journal::Kind::Move) && !journal::requiresJournal(journal::Kind::Rename)
              && !journal::requiresJournal(journal::Kind::CreateFolder));
static_assert(journal::requiresJournal(journal::Kind::SidecarWrite) && journal::requiresJournal(journal::Kind::LosslessTransform)
              && journal::requiresJournal(journal::Kind::BatchRename));
static_assert(journal::Outcome::OriginalDisplaced != journal::Outcome::Unknown);
static_assert(journal::kMaxIntentBatch == 64 && journal::kSchemaVersion == 1);
// The limits agree: a full-size operation fits at ordinary path lengths, and one operation fits in the journal.
static_assert(journal::kMaxOperationFileBytes / qint64(journal::kMaxItemsPerOperation) >= 2048);
static_assert(journal::kMaxJournalBytes >= journal::kMaxOperationFileBytes);

// COMPARE-PANES: the content modes and pane counts.
static_assert(parseCompareContentMode("compare2") == CompareLayout::Two && parseCompareContentMode("compare4") == CompareLayout::Four);
static_assert(!parseCompareContentMode("combined") && !parseCompareContentMode("preview") && !parseCompareContentMode("grid"));
static_assert(comparePaneCount(CompareLayout::Off) == 0 && comparePaneCount(CompareLayout::Two) == 2 && comparePaneCount(CompareLayout::Four) == kMaxComparePanes);

// CAPABILITY: unknown is the default, and the G-5 reference-window decision table.
static_assert(int(Support::Unknown) == 0 && int(Evidence::None) == 0);
static_assert(referenceWindowSupport({false, true, false}) == Support::Unsupported);
static_assert(referenceWindowSupport({false, false, true}) == Support::Unsupported);
static_assert(referenceWindowSupport({true, false, false}) == Support::Partial);   // Wayland
static_assert(referenceWindowSupport({true, true, true}) == Support::Partial);     // container remote desktop
static_assert(referenceWindowSupport({true, true, false}) == Support::Supported);
static_assert(SecondaryWindowFacts{}.multipleWindows == false);

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
    if (!ok) ++failures;
}
std::string names(const DirectoryListing& listing) {
    std::string joined;
    for (const auto& entry : listing.entries) joined += entry.path.filename().string() + ' ';
    return joined;
}
std::string names(const std::vector<fs::path>& files) {
    std::string joined;
    for (const auto& file : files) joined += file.filename().string() + ' ';
    return joined;
}
void writeFile(const fs::path& path, std::size_t bytes) { std::ofstream(path, std::ios::binary) << std::string(bytes, 'x'); }
}

int main() {
    check(!DecodeRequest{}.refresh && DecodeRequest{}.tier == DecodeTier::Full, "a default request is an ordinary Full decode");
    // The journal's records exist for undo and retry, so they must carry what those read.
    check(!journal::OperationSummary{}.reconciled && journal::OperationSummary{}.heldBackups == 0, "an operation starts unreconciled and holds no backup");
    check(journal::ItemIntent{}.preimagePath.isEmpty() && !journal::OperationHeader{}.permanentReplaceConsented, "no preimage and no consent unless stated");
    // CapabilityItem holds QStrings, so this default can't be a static_assert.
    check(CapabilityItem{}.support == Support::Unknown && CapabilityItem{}.evidence == Evidence::None, "a capability is unknown until something establishes it");
    // The new keys carry no values yet, so they must not fall into another key's comparison: the sizes below disagree with the
    // natural order, and a key that sorted by size would flip them.
    const FileMeta small{100, 1, true}, large{1, 9, true};
    for (const auto key : {SortKey::Captured, SortKey::Dimensions}) {
        const auto label = std::string(key == SortKey::Captured ? "captured" : "dimensions");
        for (const bool descending : {false, true}) {
            const SortSpec sort{key, descending};
            const auto direction = std::string(descending ? " descending" : " ascending");
            check(fileSortBefore(sort, "a.jpg", large, "b.jpg", small), label + direction + ": natural name wins over a smaller size");
            check(!fileSortBefore(sort, "b.jpg", small, "a.jpg", large), label + direction + ": the comparison is antisymmetric");
            check(fileSortBefore(sort, "a2.jpg", small, "a10.jpg", large), label + direction + ": natural, not lexical");
        }
    }
    // The same through both listing entry points, over real files whose sizes disagree with the natural order.
    QTemporaryDir temporary;
    const auto root = fs::path(temporary.path().toStdString());
    writeFile(root / "a.jpg", 30);
    writeFile(root / "b.jpg", 20);
    writeFile(root / "c.jpg", 10);
    for (const auto key : {SortKey::Captured, SortKey::Dimensions}) {
        for (const bool descending : {false, true}) {
            check(names(sortedImageFiles(root, SortSpec{key, descending})) == "a.jpg b.jpg c.jpg ", "sortedImageFiles keeps natural order");
            check(names(listDirectory(root, SortSpec{key, descending})) == "a.jpg b.jpg c.jpg ", "listDirectory keeps natural order");
        }
    }
    check(names(listDirectory(root, SortSpec{SortKey::Size, false})) == "c.jpg b.jpg a.jpg ", "the fixture's sizes really do disagree with natural order");
    if (failures) std::cout << failures << " failed\n";
    return failures ? 1 : 0;
}
