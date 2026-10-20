#pragma once
#include "glancelot/recovery_checkpoint.hpp"
#include <memory>

namespace glancelot::recovery {
// Private controller worker. Jobs own this state and the immutable claim; no
// worker holds a QObject, controller pointer, or borrowed startup lifetime.
class Session {
public:
    struct Snapshot {
        bool beginFinished{},begun{},busy{},closing{},closed{};
        BeginResult beginning;
        CheckpointResult lastAttempt;
        quint64 acceptedSequence{},persistedSequence{};
        Status finalStatus;
    };
    explicit Session(WindowClaim);
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    bool accept(CheckpointInput); // replace only the latest pending value
    Snapshot snapshot() const;
    bool waitForIdle(int timeoutMs) const;
    // Stop accepting navigation and request a final save/clean. A timeout is
    // never clean success; the still-running value-owned job remains safe.
    bool close(int timeoutMs,bool allowClean=true);
private:
    struct State;
    std::shared_ptr<State> state_;
    static void start(const std::shared_ptr<State>&);
    static void run(const std::shared_ptr<State>&);
};
}
