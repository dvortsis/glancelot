#pragma once
// B9 (F057): the in-app restore of a workspace recovered after an unexpected exit.
//
// The recovery engine (recovery_checkpoint, recovery_session) already resumes a crashed window's checkpoint lazily on
// the next start, as SAFE-MODE-01 requires. This class covers what the engine cannot decide alone:
//  * An explicit open wins. When a file or folder was opened (command line, Open, drop, navigation) before or instead
//    of the resume, the recovered workspace is not restored over it. The offer keeps it available: accepting adds the
//    recovered locations as tabs beside the explicit one and switches to the recovered active tab. Only that tab's
//    folder is listed now; every other tab stays a saved location until it is activated (lazy, F015).
//  * SAFE mode and a settings reset never restore anything; each is shown as its own state, never silently.
//  * Dismissing is remembered for that recovery point (the engine's checkpoint revision), across starts.
//  * A restore can be cancelled while its folder or photo is still loading; the tab set returns to what it was.
// No IO on the calling (GUI) thread: the availability probe, the dismissal read and write and the reset-file lookup
// run on the global thread pool with value-owned inputs, and every result carries a generation so a stale result never
// replaces a newer recovery point.
#include "glancelot/recovery_checkpoint.hpp"
#include <QObject>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

class ViewerController;

namespace glancelot {
class RestoreOffer final : public QObject {
    Q_OBJECT
    // "hidden", "offer" (a recovered workspace that was not restored), "unavailable" (every recovered folder is gone),
    // "restoring" (accepted; the recovered tab is loading), "safe" (SAFE mode) or "reset" (--reset-settings).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString folder READ folder NOTIFY changed)          // the recovered active folder, native separators
    Q_PROPERTY(QString folderName READ folderName NOTIFY changed)
    Q_PROPERTY(int locationCount READ locationCount NOTIFY changed) // recovered tabs (1 for a single location)
    Q_PROPERTY(int unavailableCount READ unavailableCount NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)         // the availability probe has not answered yet
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY changed)
    Q_PROPERTY(QString resetFile READ resetFile NOTIFY changed)    // the set-aside settings file name, when found
    Q_PROPERTY(QString feedback READ feedback NOTIFY changed)      // the last outcome ("restored 3 tabs", "dismissed")
    Q_PROPERTY(quint64 generation READ generation NOTIFY changed)
public:
    // Controller state the offer reads; it never holds a pointer into the controller's private data.
    struct Context {
        bool safeMode{}, explicitIntent{}, restoreRequested{}, busy{};
        int slot{};
        QString folder, image; // the active tab's location ("" when nothing is open)
    };
    // What adopt() did: tab indexes are positions in viewer->tabs() after it returned.
    struct Adopted {
        int previous{-1};  // the explicit tab, or -1 when the window showed nothing and the recovered set replaced it
        int first{-1};     // the first added tab
        int added{};       // tabs added (duplicates of open tabs and tabs over the limit are skipped)
        int skipped{};     // over recovery::MaxTabs
        int target{-1};    // the tab activated for the recovered active location
    };
    struct Host {
        std::function<Context()> context;
        // Adds `locations` as tabs and activates the one standing for `active`; never navigates any other tab.
        std::function<Adopted(const QList<recovery::TabLocation>& locations, int active)> adopt;
    };
    RestoreOffer(ViewerController* viewer, Host host, QObject* parent = nullptr);
    ~RestoreOffer() override;

    // Called by the controller when its claim's begin captured the previous position (once per claim).
    void recovered(const recovery::ResumeState& previous);
    // The startup arguments decide the reset state; the default is QCoreApplication::arguments(). Tests replace it.
    void setStartupArguments(const QStringList& arguments);
    // Where the dismissal record and reset files live; the default is settingsDirectory() when an offer is made.
    void setSettingsDirectory(const QString& directory);

    Q_INVOKABLE bool accept();
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE bool dismiss();

    QString state() const;
    QString folder() const;
    QString folderName() const;
    int locationCount() const;
    int unavailableCount() const { return unavailable_; }
    bool checking() const { return checking_; }
    bool cancellable() const;
    QString resetFile() const { return resetFile_; }
    QString feedback() const { return feedback_; }
    quint64 generation() const { return generation_; }

    // The per-slot dismissal record: <settings>/recovery-dismissed-<slot>.json, {"version":1,"checkpointRevision":"…"}.
    // Absent, unreadable, oversized or malformed records read as "nothing dismissed".
    static QString dismissalPath(const QString& directory, int slot);
    static QString readDismissedRevision(const QString& path);
    static bool writeDismissedRevision(const QString& path, const QString& revision);
    // True when the arguments ask for --reset-settings (single or double dash), before any "--".
    static bool resetRequested(const QStringList& arguments);

signals:
    void changed();

private:
    enum class Phase { Idle, Restoring };
    void evaluate();
    void setFeedback(const QString& text);
    void probe();
    void findResetFile();
    QList<recovery::TabLocation> locations() const;

    QPointer<ViewerController> viewer_;
    Host host_;
    QString state_{QStringLiteral("hidden")};
    std::optional<recovery::ResumeState> candidate_;
    QList<bool> available_;
    int unavailable_{};
    bool checking_{}, safeDismissed_{}, resetIntent_{}, resetDismissed_{}, resetLooked_{};
    QString directory_, resetFile_, feedback_, dismissedRevision_;
    int slot_{};
    quint64 generation_{}, resetGeneration_{};
    Phase phase_{Phase::Idle};
    Adopted adopted_;
    int baseTabCount_{};
    bool settleArmed_{};
};
}
