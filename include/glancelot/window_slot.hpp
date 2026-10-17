#pragma once
#include <QString>
#include <QtGlobal>

// Stable opening-order window slots, coordinated across independent viewer processes.
// Contract (frozen by the lead; change only through the lead):
//  * The first live window of a session is slot 1, the next 2, and so on. A slot number is never reused while
//    any window of the session is alive; closing windows in any order never renumbers the others, and a window
//    opened later takes the next number after the highest one handed out this session.
//  * When no window is alive (all closed or crashed) the next claim starts again at 1.
//  * Coordination state lives in sessions.json beside settings.json under a cross-process lock; entries of dead
//    processes (checked by pid plus process start time, so pid reuse cannot keep a slot alive) are pruned.
//  * number() == 0 means coordination was unavailable; the window then runs without position memory.
//  * Saved positions are stored by the settings writer (keyed by slot) and are not removed when a window closes.
class WindowSlot {
public:
    explicit WindowSlot(const QString& directory = {});
    ~WindowSlot();
    WindowSlot(const WindowSlot&) = delete;
    WindowSlot& operator=(const WindowSlot&) = delete;
    int number() const { return number_; }
    void release();
private:
    QString directory_;
    int number_{};
};
