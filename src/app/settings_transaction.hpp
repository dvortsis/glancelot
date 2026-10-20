#pragma once
#include "glancelot/settings_recovery.hpp"
#include <QJsonObject>
#include <functional>

namespace glancelot::settings_detail {
// Private writer pipeline. Only the future validated recovery facade constructs
// Restore requests; the public API never accepts editors/preimage opt-outs.
enum class Policy { Ordinary, Checkpoint, Restore };
struct Request {
    recovery::Context context;
    recovery::Options options;
    Policy policy{Policy::Checkpoint};
    bool retryReplacements{};
    QString expectedRevision, expectedNamespace;
};
struct View {
    QJsonObject base;
    std::optional<recovery::RawSnapshot> raw;
    bool recovered{};
};
struct Edit {
    recovery::Code decision{recovery::Code::NoChange};
    QJsonObject replacement;
};
struct Seal {
    recovery::Status status;
    QString path;
};
struct Result {
    recovery::Status status;
    bool recovered{};
    QString revision, preimage;
};
using Editor=std::function<Edit(const View&)>;
using PreimageWriter=std::function<Seal(const recovery::RawSnapshot&)>;
Result transact(const Request&,const Editor&,const PreimageWriter& = {});

enum class Stage { AfterRead, BeforeEvidence, AfterPreimage, BeforeReplace, AfterReplace };
#ifdef GLANCELOT_RECOVERY_TESTING
struct TransactionHooks {
    std::function<void(Stage)> observe;
    std::function<bool(Stage)> fail;
};
inline thread_local const TransactionHooks* transactionHooks{};
#endif
}
