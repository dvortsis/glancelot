#include "recovery_session.hpp"
#include "settings_transaction.hpp"
#include "settings_raw.hpp"
#include "settings_policy.hpp"
#include "window_claim_guard.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace glancelot::recovery {
struct Session::State {
    explicit State(WindowClaim value):claim(std::move(value)){}
    WindowClaim claim;
    mutable std::mutex mutex;
    std::condition_variable changed;
    Snapshot snapshot;
    std::optional<CheckpointInput> pending;
    QString persistedRevision;
    bool allowClean{true};
#ifdef GLANCELOT_RECOVERY_TESTING
    std::optional<settings_detail::TransactionHooks> hooks=settings_detail::transactionHooks?
        std::optional{*settings_detail::transactionHooks}:std::nullopt;
#endif
};
namespace {
bool success(Code code){return code==Code::Committed||code==Code::NoChange;}
bool retryable(Code code){return code==Code::Busy||code==Code::WriteFailed;}
std::optional<ResumeState> readOnlyPosition(const WindowClaim& identity) {
    const auto claim=glancelot::detail::WindowClaimAccess::get(identity);
    if(!claim||!claim->active.load())return {};
    const auto read=[](const QString& path)->std::optional<RawMember> {
        QFile file(path);if(!file.exists())return RawMember{};
        if(!file.open(QIODevice::ReadOnly)||file.size()>MaxMemberBytes)return {};
        auto bytes=file.read(MaxMemberBytes+1);
        if(file.error()!=QFileDevice::NoError||bytes.size()>MaxMemberBytes)return {};
        return raw::classify(std::move(bytes));
    };
    const auto path=QDir(claim->directory).filePath("settings.json");
    auto member=read(path);if(!member)return {};
    if(member->kind==ParseKind::Corrupt)member=read(path+".bak");
    if(!member||member->kind!=ParseKind::Supported)return {};
    const auto root=QJsonDocument::fromJson(member->bytes).object();
    policy::Selection selected;selected.slot=claim->slot;selected.slotSection=SlotSection::Position;
    if(policy::merge(root,root,selected,{}).status.code!=Code::Ok)return {};
    const auto row=root.value("windows").toObject().value(QString::number(claim->slot)).toObject();
    ResumeState result;result.folder=row.value("folder").toString();result.image=row.value("image").toString();
    result.hasPosition=!result.folder.isEmpty()||!result.image.isEmpty();
    return result;
}
}
Session::Session(WindowClaim claim):state_(std::make_shared<State>(std::move(claim))) {start(state_);}
Session::~Session(){close(0);}
void Session::start(const std::shared_ptr<State>& state) {
    {
        std::lock_guard lock(state->mutex);
        if(state->snapshot.busy||state->snapshot.closed)return;
        state->snapshot.busy=true;
    }
    try {std::thread([state]{
        try {run(state);}
        catch(...) {
            std::lock_guard lock(state->mutex);state->snapshot.busy=false;
            state->snapshot.beginFinished=true;
            if(!state->snapshot.begun)state->snapshot.beginning.status.code=Code::WriteFailed;
            state->snapshot.lastAttempt.status.code=Code::WriteFailed;
            if(state->snapshot.closing){state->snapshot.closed=true;state->snapshot.finalStatus.code=Code::WriteFailed;}
            state->changed.notify_all();
        }
    }).detach();}
    catch(...) {
        std::lock_guard lock(state->mutex);state->snapshot.busy=false;
        state->snapshot.beginFinished=true;state->snapshot.beginning.status.code=Code::WriteFailed;
        if(state->snapshot.closing){state->snapshot.closed=true;state->snapshot.finalStatus.code=Code::WriteFailed;}
        state->changed.notify_all();
    }
}
void Session::run(const std::shared_ptr<State>& state) {
#ifdef GLANCELOT_RECOVERY_TESTING
    // Copy test callbacks into the worker, never retain a test-stack hook pointer.
    const auto* previousHooks=settings_detail::transactionHooks;
    settings_detail::transactionHooks=state->hooks?&*state->hooks:nullptr;
    struct RestoreHooks {const settings_detail::TransactionHooks* previous;~RestoreHooks(){settings_detail::transactionHooks=previous;}} restoreHooks{previousHooks};
#endif
    Options options;options.lockTimeoutMs=0;
    const auto retry=[&](auto operation,quint64 sequence=0) {
        auto result=operation();
        for(int attempt=0;attempt<8&&retryable(result.status.code);++attempt) {
            std::unique_lock lock(state->mutex);
            if(sequence&&state->pending&&state->pending->sequence>sequence)break;
            // Closing must not turn bounded retries into a tight loop. A
            // newer checkpoint may still supersede this one immediately.
            state->changed.wait_for(lock,std::chrono::milliseconds(50),[&]{
                return sequence&&state->pending&&state->pending->sequence>sequence;
            });
            if(sequence&&state->pending&&state->pending->sequence>sequence)break;
            lock.unlock();result=operation();
        }
        return result;
    };
    bool begun;
    {std::lock_guard lock(state->mutex);begun=state->snapshot.begun;}
    if(!begun) {
        const auto result=retry([&]{return beginWindow(state->claim,options);});
        const auto fallback=success(result.status.code)?std::optional<ResumeState>{}:readOnlyPosition(state->claim);
        std::lock_guard lock(state->mutex);state->snapshot.beginFinished=true;state->snapshot.beginning=result;
        if(!success(result.status.code)) {
            state->snapshot.readOnlyResume=fallback;
            state->snapshot.busy=false;
            if(state->snapshot.closing){state->snapshot.closed=true;state->snapshot.finalStatus=result.status;}
            state->changed.notify_all();return;
        }
        state->snapshot.begun=true;state->persistedRevision=result.previous.checkpointRevision;
    }
    for(;;) {
        std::optional<CheckpointInput> input;bool finish=false;QString revision;quint64 sequence=0;
        {
            std::lock_guard lock(state->mutex);
            if(state->pending){input=std::move(state->pending);state->pending.reset();}
            else if(state->snapshot.closing) {
                if(!state->allowClean||state->snapshot.acceptedSequence!=state->snapshot.persistedSequence) {
                    state->snapshot.finalStatus=state->allowClean?state->snapshot.lastAttempt.status:Status{Code::WriteFailed,{},0};state->snapshot.closed=true;
                    state->snapshot.busy=false;state->changed.notify_all();return;
                }
                finish=true;revision=state->persistedRevision;sequence=state->snapshot.persistedSequence;
            }else {state->snapshot.busy=false;state->changed.notify_all();return;}
        }
        if(input) {
            const auto result=retry([&]{return checkpoint(state->claim,*input,options);},input->sequence);
            std::lock_guard lock(state->mutex);state->snapshot.lastAttempt=result;
            if(result.status.code==Code::Committed) {
                state->snapshot.persistedSequence=result.sequence;state->persistedRevision=result.checkpointRevision;
            }
        }else if(finish) {
            // Wrap Status so the same bounded retry policy applies to finish.
            const auto result=retry([&]{return BeginResult{finishWindow(state->claim,revision,sequence,options),{}};});
            std::lock_guard lock(state->mutex);state->snapshot.finalStatus=result.status;state->snapshot.closed=true;
            state->snapshot.busy=false;state->changed.notify_all();return;
        }
    }
}
bool Session::accept(CheckpointInput input) {
    {
        std::lock_guard lock(state_->mutex);
        if(state_->snapshot.closing||!input.sequence||input.sequence<=state_->snapshot.acceptedSequence)return false;
        state_->snapshot.acceptedSequence=input.sequence;state_->pending=std::move(input);state_->changed.notify_all();
    }
    start(state_);return true;
}
Session::Snapshot Session::snapshot() const {std::lock_guard lock(state_->mutex);return state_->snapshot;}
bool Session::waitForIdle(int timeoutMs) const {
    std::unique_lock lock(state_->mutex);
    return state_->changed.wait_for(lock,std::chrono::milliseconds(std::max(0,timeoutMs)),[&]{return !state_->snapshot.busy;});
}
bool Session::close(int timeoutMs,bool allowClean) {
    {std::lock_guard lock(state_->mutex);state_->allowClean&=allowClean;state_->snapshot.closing=true;state_->changed.notify_all();}
    start(state_);
    std::unique_lock lock(state_->mutex);
    if(!state_->changed.wait_for(lock,std::chrono::milliseconds(std::max(0,timeoutMs)),[&]{return state_->snapshot.closed;}))return false;
    return success(state_->snapshot.finalStatus.code);
}
}
