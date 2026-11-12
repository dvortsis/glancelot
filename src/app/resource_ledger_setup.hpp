#pragma once
#include "glancelot/resource_ledger.hpp"

// E1 (RESOURCE-LEDGER M1): the process ledger's budget construction, reproducing today's limits exactly. Called on the
// GUI thread before any window, so instance() lives there; idempotent. Owner quotas are set by their owners
// (DisplayManager: the presentation image budget; CanvasTextureAdmission: each canvas group's texture budget).
namespace glancelot::resource {
inline constexpr qint64 kRetainedCacheCeilingBytes = 128LL << 20;   // DecodeCache (decode_cache.hpp)
inline constexpr qint64 kThumbnailCeilingBytes = 64LL << 20;        // ThumbnailService::kCacheBytes
inline ResourceLedger& configureProcessLedger() {
    auto& ledger = ResourceLedger::instance();
    ledger.setEnforcement(Enforcement::Shadow);   // M0: every site reserves; the legacy checks decide
    ledger.setLedgerCeiling(Ledger::RetainedCache, kRetainedCacheCeilingBytes);
    ledger.setLedgerCeiling(Ledger::Thumbnails, kThumbnailCeilingBytes);
    ledger.setAggregateMember(Ledger::GpuTextures, false);   // M1: textures are outside the aggregate
    return ledger;
}
}
