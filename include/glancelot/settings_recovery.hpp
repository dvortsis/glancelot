#pragma once
#include "window_claim.hpp"
#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>
#include <optional>
#include <stop_token>
#include <variant>

namespace glancelot::recovery {
// SAFE-MODE-01 lead-frozen facade. Filesystem operations run on a worker or on
// the headless maintenance path. All inputs/results own their values. No stable
// external plugin ABI. Existing SettingsStatus codes/callers stay unchanged.
inline constexpr qint64 MaxMemberBytes = 1024 * 1024;
inline constexpr qint64 MaxManifestBytes = 8 * 1024;
inline constexpr qint64 MaxPackageBytes = 3 * 1024 * 1024;
inline constexpr qint64 MaxPreviewBytes = 3 * 1024 * 1024;
inline constexpr int MaxDiffDepth = 64;
inline constexpr int MaxDiffRows = 4096;
inline constexpr int MaxLockWaitMs = 4000;

enum class Code {
    Ok, NoChange, Committed, Conflict, Cancelled, Unsupported, Corrupt,
    Busy, Unreadable, WriteFailed, LimitExceeded
};
struct Status {
    Code code{Code::Corrupt};
    QString detail;
    quint64 generation{};
};
struct Options {
    std::stop_token cancellation;
    quint64 generation{};
    int lockTimeoutMs{MaxLockWaitMs}; // validated in [0, MaxLockWaitMs]
};
struct Context {
    QString directory; // explicit absolute namespace; never re-read redirected env
    // Optional process-owned temporary-root owner. Operations/plans retain it;
    // no worker borrows a startup-stack object's lifetime.
    std::shared_ptr<const void> lifetime;
};
enum class ParseKind { Absent, Supported, Corrupt, Unsupported };
struct RawMember {
    bool present{};
    QByteArray bytes;
    QByteArray sha256; // 32 binary bytes for a present member
    ParseKind kind{ParseKind::Absent};
};
struct RawSnapshot {
    QString directory;
    QString namespaceId;
    RawMember primary, lastGood;
    QString revision; // sr1: plus 64 lowercase SHA256 hexadecimal digits
};
struct SnapshotResult {
    Status status;
    std::optional<RawSnapshot> snapshot; // absent on read/limit/lock failure
};
struct CaptureResult {
    Status status; // Ok only after the exclusive package is sealed
    QString destination;
    QString packageRevision;
};
enum class SourceMember { Primary, LastGood };
enum class GlobalSection { All, Viewing, Browsing, Appearance, Bindings, Performance };
enum class SlotSection { Position, Layout };
struct GlobalScope { GlobalSection section{GlobalSection::All}; };
struct OwnedSlotScope { SlotSection section{SlotSection::Position}; WindowClaim claim; };
struct OfflineSlotScope { SlotSection section{SlotSection::Position}; int slot{}; };
using Scope = std::variant<GlobalScope, OwnedSlotScope, OfflineSlotScope>;
enum class Change { Added, Removed, Changed };
enum class Treatment { Apply, Preserve, Invalid };
struct DiffRow {
    QStringList path;
    Change change{Change::Changed};
    Treatment treatment{Treatment::Preserve};
    bool hadBefore{}, hasAfter{}; // distinguish missing from JSON null
    QJsonValue before, after;
};
namespace detail { struct RestorePlanData; struct RecoveryAccess; }
class RestorePlan {
public:
    RestorePlan() = default;
    bool applicable() const noexcept { return bool(data_); }
private:
    friend struct detail::RecoveryAccess;
    std::shared_ptr<const detail::RestorePlanData> data_;
};
struct PreviewResult {
    Status status;
    QList<DiffRow> rows;
    bool truncated{};
    QString expectedCurrentRevision, packageRevision, namespaceId, policyRevision;
    QString fingerprint;
    RestorePlan plan; // no plan for malformed/unsupported/truncated preview
};
struct ApplyResult {
    Status status;
    QString resultingRevision;
    QString preimage; // retained even after a later pre-commit failure
};

SnapshotResult readRaw(const Context&, Options = {});
CaptureResult captureBackup(const Context&, const QString& destination, Options = {});
PreviewResult preview(const Context&, const QString& packageDirectory,
                      Scope = GlobalScope{}, SourceMember = SourceMember::Primary, Options = {});
// Revalidate raw pair, package/namespace/policy and ownership before mutation.
// NoChange/Conflict/Cancelled do not write primary/bak/damaged. An exclusive
// sealed raw preimage precedes primary replacement. Once replaced, Committed
// cannot become Cancelled. Reuse the ordinary writer's private locked pipeline.
ApplyResult apply(const RestorePlan&, Options = {});
}
