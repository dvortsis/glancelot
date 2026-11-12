#include "restore_offer.hpp"
#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace glancelot {
namespace {
constexpr qint64 MaxDismissalBytes = 4096;
bool sameLocation(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return a.isEmpty() && b.isEmpty();
#ifdef Q_OS_WIN
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(a) == QDir::cleanPath(b);
#endif
}
struct Probe {
    quint64 generation{};
    QString dismissed;
    QList<bool> available;
};
}

RestoreOffer::RestoreOffer(ViewerController* viewer, Host host, QObject* parent)
    : QObject(parent), viewer_(viewer), host_(std::move(host)), resetIntent_(resetRequested(QCoreApplication::arguments())) {
    if (viewer) {
        connect(viewer, &ViewerController::stateChanged, this, &RestoreOffer::evaluate);
        connect(viewer, &ViewerController::tabsChanged, this, &RestoreOffer::evaluate);
    }
    QTimer::singleShot(0, this, &RestoreOffer::evaluate); // SAFE and reset are known before any claim begins
}
RestoreOffer::~RestoreOffer() = default;

QString RestoreOffer::dismissalPath(const QString& directory, int slot) {
    return QDir(directory).filePath(QStringLiteral("recovery-dismissed-%1.json").arg(slot));
}
QString RestoreOffer::readDismissedRevision(const QString& path) {
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly) || file.size() > MaxDismissalBytes) return {};
    const auto bytes = file.read(MaxDismissalBytes + 1);
    if (bytes.size() > MaxDismissalBytes) return {};
    const auto object = QJsonDocument::fromJson(bytes).object();
    if (object.value("version") != QJsonValue(1)) return {};
    const auto revision = object.value("checkpointRevision").toString();
    const QUuid uuid(revision);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == revision ? revision : QString{};
}
bool RestoreOffer::writeDismissedRevision(const QString& path, const QString& revision) {
    if (QFileInfo(path).absolutePath().isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"checkpointRevision", revision}}).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
bool RestoreOffer::resetRequested(const QStringList& arguments) {
    for (int i = 1; i < arguments.size(); ++i) {
        const auto& value = arguments.at(i);
        if (value == QLatin1String("--")) return false;
        if (value == QLatin1String("--reset-settings") || value == QLatin1String("-reset-settings")) return true;
    }
    return false;
}

void RestoreOffer::setStartupArguments(const QStringList& arguments) {
    resetIntent_ = resetRequested(arguments); resetLooked_ = false; resetFile_.clear(); ++resetGeneration_; evaluate();
}
void RestoreOffer::setSettingsDirectory(const QString& directory) { directory_ = directory; }

void RestoreOffer::recovered(const recovery::ResumeState& previous) {
    ++generation_;
    candidate_.reset(); available_.clear(); unavailable_ = 0; checking_ = false;
    if (phase_ == Phase::Restoring) phase_ = Phase::Idle;
    // Only a window that ended without a clean close has something to recover; a clean exit resumes as UX-12 says.
    if (previous.priorDirty && previous.hasPosition && !previous.checkpointRevision.isEmpty() &&
        previous.checkpointRevision != dismissedRevision_) {
        candidate_ = previous;
        slot_ = host_.context ? host_.context().slot : 0;
        if (directory_.isEmpty()) directory_ = settingsDirectory();
        checking_ = true;
        probe();
    }
    emit changed();
    evaluate();
}

QList<recovery::TabLocation> RestoreOffer::locations() const {
    if (!candidate_) return {};
    if (candidate_->tabs.size() >= 2) return candidate_->tabs;
    return {{candidate_->folder, candidate_->image}};
}

void RestoreOffer::probe() {
    const auto generation = generation_;
    const auto path = dismissalPath(directory_, slot_);
    QStringList folders;
    for (const auto& location : locations()) folders.append(location.folder);
    auto* watcher = new QFutureWatcher<Probe>(this);
    connect(watcher, &QFutureWatcher<Probe>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (result.generation != generation_ || !candidate_) return; // a newer recovery point replaced this one
        checking_ = false;
        if (!result.dismissed.isEmpty() && result.dismissed == candidate_->checkpointRevision) {
            candidate_.reset(); // dismissed earlier for this very recovery point
        } else {
            available_ = result.available;
            unavailable_ = int(std::count(available_.cbegin(), available_.cend(), false));
        }
        emit changed();
        evaluate();
    });
    // Value-owned job: a stat per recovered folder (no listing) and one small bounded read; it never touches this object.
    watcher->setFuture(QtConcurrent::run([generation, path, folders] {
        Probe result; result.generation = generation;
        result.dismissed = readDismissedRevision(path);
        for (const auto& folder : folders) result.available.append(!folder.isEmpty() && QFileInfo(folder).isDir());
        return result;
    }));
}

void RestoreOffer::findResetFile() {
    resetLooked_ = true;
    const auto directory = directory_.isEmpty() ? settingsDirectory() : directory_;
    const auto generation = ++resetGeneration_;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, generation] {
        const auto name = watcher->result(); watcher->deleteLater();
        if (generation != resetGeneration_ || !resetIntent_) return;
        resetFile_ = name; emit changed();
    });
    watcher->setFuture(QtConcurrent::run([directory] {
        const auto names = QDir(directory).entryList({QStringLiteral("settings.json.reset-*")}, QDir::Files, QDir::Name);
        return names.isEmpty() ? QString{} : names.last(); // the stamp sorts by time
    }));
}

void RestoreOffer::setFeedback(const QString& text) { feedback_ = text; emit changed(); }

void RestoreOffer::evaluate() {
    if (!host_.context) return;
    const auto context = host_.context();
    QString next = QStringLiteral("hidden");
    if (context.safeMode) {
        if (!safeDismissed_) next = QStringLiteral("safe");
    } else if (phase_ == Phase::Restoring) {
        if (context.busy) settleArmed_ = false; // the recovered tab's load has been seen to start
        if (!settleArmed_ && !context.busy) {
            phase_ = Phase::Idle;
            const int count = adopted_.added;
            candidate_.reset();
            setFeedback(count > 1 ? QCoreApplication::translate("RestoreOffer", "Restored %1 tabs from your previous session. Each loads when you switch to it.").arg(count)
                                  : QCoreApplication::translate("RestoreOffer", "Restored your previous folder."));
        } else next = QStringLiteral("restoring");
    } else if (resetIntent_ && !resetDismissed_) {
        next = QStringLiteral("reset");
        if (!resetLooked_) findResetFile();
    } else if (candidate_ && !checking_ && context.explicitIntent && !context.restoreRequested) {
        // An explicit open took the window, so the resume did not run. Nothing is offered when it opened the same place.
        const bool same = locations().size() < 2 && sameLocation(candidate_->folder, context.folder) &&
                          (candidate_->image.isEmpty() || sameLocation(candidate_->image, context.image));
        if (!same) next = unavailable_ >= locations().size() ? QStringLiteral("unavailable") : QStringLiteral("offer");
    }
    if (next != state_) { state_ = next; emit changed(); }
}

bool RestoreOffer::accept() {
    if (state_ != QLatin1String("offer") || !viewer_ || !host_.adopt) return false;
    const auto all = locations();
    int active = candidate_->tabs.size() >= 2 ? candidate_->activeTab : 0;
    // The recovered active tab is opened unless its folder is gone; then the first folder that is still there.
    if (active < available_.size() && !available_.at(active)) {
        const auto found = available_.indexOf(true);
        if (found >= 0) active = int(found);
    }
    adopted_ = host_.adopt(all, active);
    if (adopted_.target < 0 || adopted_.target == adopted_.previous) {
        const int added = adopted_.added;
        candidate_.reset(); phase_ = Phase::Idle;
        setFeedback(added > 1 ? QCoreApplication::translate("RestoreOffer", "Added %1 tabs from your previous session.").arg(added)
                    : added ? QCoreApplication::translate("RestoreOffer", "Added a tab from your previous session.")
                            : QCoreApplication::translate("RestoreOffer", "Your previous workspace is already open."));
        evaluate();
        return true;
    }
    phase_ = Phase::Restoring; settleArmed_ = true;
    QTimer::singleShot(0, this, [this] { settleArmed_ = false; evaluate(); });
    evaluate();
    return true;
}

bool RestoreOffer::cancellable() const {
    return phase_ == Phase::Restoring && (adopted_.previous >= 0 || (viewer_ && viewer_->directoryPending()));
}

bool RestoreOffer::cancel() {
    if (phase_ != Phase::Restoring || !viewer_ || !cancellable()) return false;
    phase_ = Phase::Idle;
    if (adopted_.previous >= 0) {
        // Back to the explicit tab first (its own open supersedes the recovered tab's load), then close the added tabs.
        viewer_->activateTab(adopted_.previous);
        if (viewer_->tabs().size() == adopted_.first + adopted_.added)
            for (int i = adopted_.first + adopted_.added - 1; i >= adopted_.first; --i) viewer_->closeTab(i);
        setFeedback(QCoreApplication::translate("RestoreOffer", "Restore cancelled. Your previous workspace is still offered."));
    } else {
        viewer_->cancelDirectoryNavigation(); // the folder listing stops; the tab keeps its saved location (F5 retries)
        candidate_.reset();
        setFeedback(QCoreApplication::translate("RestoreOffer", "Restore stopped. Press F5 to try that folder again."));
    }
    adopted_ = {};
    evaluate();
    return true;
}

bool RestoreOffer::dismiss() {
    if (state_ == QLatin1String("safe")) { safeDismissed_ = true; evaluate(); return true; }
    if (state_ == QLatin1String("reset")) { resetDismissed_ = true; evaluate(); return true; }
    if ((state_ != QLatin1String("offer") && state_ != QLatin1String("unavailable")) || !candidate_) return false;
    const auto path = dismissalPath(directory_, slot_);
    const auto revision = candidate_->checkpointRevision;
    dismissedRevision_ = revision; // this process remembers it even before the record is written
    candidate_.reset();
    setFeedback(QCoreApplication::translate("RestoreOffer", "Dismissed. Your previous workspace won't be offered again."));
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
        const bool written = watcher->result(); watcher->deleteLater();
        if (!written) setFeedback(QCoreApplication::translate("RestoreOffer", "Dismissed for now. Glancelot could not remember this, so it may be offered again next time."));
    });
    watcher->setFuture(QtConcurrent::run([path, revision] { return writeDismissedRevision(path, revision); }));
    evaluate();
    return true;
}

QString RestoreOffer::state() const { return state_; }
QString RestoreOffer::folder() const { return candidate_ ? QDir::toNativeSeparators(candidate_->folder) : QString{}; }
QString RestoreOffer::folderName() const {
    if (!candidate_) return {};
    const auto name = QFileInfo(candidate_->folder).fileName();
    return name.isEmpty() ? QDir::toNativeSeparators(candidate_->folder) : name;
}
int RestoreOffer::locationCount() const { return int(locations().size()); }
}
