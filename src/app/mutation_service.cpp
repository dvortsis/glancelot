#include "glancelot/mutation_service.hpp"
#include "asset_group_internal.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QThreadPool>
#include <QUuid>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <optional>
#include <stop_token>

namespace glancelot {

namespace {

QString clean(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool isWindowsReservedStem(const QString& stem) {
    static const QStringList reserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),  QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"), QStringLiteral("COM9"),
        QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"), QStringLiteral("LPT4"),
        QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    return reserved.contains(stem.toUpper());
}

QVariantMap validateName(const QString& parentDirectory, const QString& newName, const QString& currentPath);
QVariantMap performRename(const QString& sourcePath, const QString& newName);

struct RenamePlan {
    QVariantMap summary;
    QString source, newName;
    std::optional<MutationTarget> snapshot;
    bool executable{}; // file scope, a valid changed name, no collision
    bool cancelled{};
};

// Worker: the preview of a rename. Reads only.
RenamePlan buildRenamePlan(const QString& sourcePath, const QString& newName, assets::Scope scope, std::stop_token cancel) {
    namespace fs = std::filesystem;
    RenamePlan plan;
    plan.source = clean(sourcePath);
    plan.newName = newName;
    const fs::path source = assets::detail::nativePath(plan.source);
    const auto expansion = assets::expand({source}, scope, assets::Operation::Rename, nullptr, cancel);
    if (expansion.cancelled || cancel.stop_requested()) { plan.cancelled = true; return plan; }

    QVariantMap summary;
    assets::detail::addCompanionSummary(summary, expansion, scope);
    QVariantList excluded;
    bool collisionFallback = false;
    for (const auto& row : summary.value(QStringLiteral("companions")).toList()) {
        const QVariantMap companion = row.toMap();
        if (companion.value(QStringLiteral("included")).toBool()) continue;
        excluded.append(companion);
        const QString why = companion.value(QStringLiteral("exclusion")).toString();
        collisionFallback = collisionFallback || why == QLatin1String("collision") || why == QLatin1String("ambiguous");
    }
    summary.insert(QStringLiteral("excluded"), excluded);
    summary.insert(QStringLiteral("source"), plan.source);
    summary.insert(QStringLiteral("newName"), newName);

    std::error_code probeError;
    plan.snapshot = snapshotTarget(source, probeError);
    QString error;
    if (probeError || !plan.snapshot) error = QStringLiteral("Source item does not exist.");

    QVariantList renames, collisions;
    const QFileInfo sourceInfo(plan.source);
    const QString parent = clean(sourceInfo.dir().absolutePath());
    bool asset = scope == assets::Scope::Asset && !expansion.included.empty();
    if (asset) {
        // One new stem for every member, from what was typed for this file. The extension typed, if any, is ignored:
        // every member keeps its own.
        const auto& requested = expansion.included.front().member;
        const QString own = assets::detail::splitAssetName(sourceInfo.fileName(), requested.sidecar).suffix;
        const QString stem = !own.isEmpty() && newName.endsWith(own, Qt::CaseInsensitive)
            ? newName.chopped(own.size()) : assets::detail::splitAssetName(newName, assets::SidecarStyle::None).stem;
        std::vector<std::pair<fs::path, fs::path>> pairs;
        if (error.isEmpty()) pairs = assets::renameAsset(expansion, stem.toStdString());
        if (pairs.empty() && error.isEmpty() && MutationService::isValidFileName(stem, &error)) error = QStringLiteral("Name cannot exceed 255 characters.");
        QSet<QString> froms, tos;
        for (const auto& [from, to] : pairs) froms.insert(clean(assets::detail::displayPath(from)).toCaseFolded());
        for (const auto& [from, to] : pairs) {
            const QString target = clean(assets::detail::displayPath(to));
            renames.append(QVariantMap{{"from", assets::detail::displayPath(from)}, {"to", target}});
            // Case folding included: a target that is only another member's old name is fine, because it moves away.
            const QString key = target.toCaseFolded();
            const bool repeated = tos.contains(key);
            tos.insert(key);
            if (repeated || (!froms.contains(key) && (QFileInfo::exists(target) || QFileInfo(target).isSymLink()))) {
                collisions.append(QVariantMap{{"path", assets::detail::displayPath(from)}, {"target", target}});
            }
        }
    } else if (error.isEmpty()) {
        const QVariantMap validation = validateName(parent, newName, plan.source);
        const QString target = clean(QDir(parent).filePath(newName));
        if (!validation.value(QStringLiteral("valid")).toBool()) {
            error = validation.value(QStringLiteral("error")).toString();
            if (QFileInfo::exists(target)) collisions.append(QVariantMap{{"path", plan.source}, {"target", target}});
        }
        renames.append(QVariantMap{{"from", plan.source}, {"to", target}});
        plan.executable = error.isEmpty() && newName != sourceInfo.fileName();
    }
    summary.insert(QStringLiteral("renames"), renames);
    summary.insert(QStringLiteral("collisions"), collisions);
    summary.insert(QStringLiteral("valid"), error.isEmpty());
    summary.insert(QStringLiteral("error"), error);
    summary.insert(QStringLiteral("collisionFallback"), scope == assets::Scope::Asset && collisionFallback);
    summary.insert(QStringLiteral("refused"), scope == assets::Scope::Asset);
    if (scope == assets::Scope::Asset) {
        summary.insert(QStringLiteral("refusal"), QCoreApplication::translate("Rename", "Renaming a whole set of files together isn't available yet. If one file failed partway, the others couldn't be put back automatically."));
    }
    plan.summary = std::move(summary);
    return plan;
}

} // namespace

struct MutationService::Private {
    QThreadPool* pool = new QThreadPool; // heap-allocated so a worker wedged on a dead share can be abandoned at teardown
    std::stop_source planStop;
    quint64 nextId{}, planningId{}, readyId{}, runningId{};
    std::optional<RenamePlan> ready;

    Private() { pool->setMaxThreadCount(2); } // one preview and one rename; a wedged preview never blocks a rename
    ~Private() {
        planStop.request_stop();
        if (pool->waitForDone(5000)) delete pool;
    }
};

MutationService::MutationService(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>()) {}

MutationService::~MutationService() = default;

bool MutationService::isValidFileName(const QString& name, QString* error) {
    if (name.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Name cannot be empty.");
        return false;
    }

    if (name.length() > 255) {
        if (error) *error = QStringLiteral("Name cannot exceed 255 characters.");
        return false;
    }

    // ASCII control characters (< 32 or 127)
    for (const QChar& ch : name) {
        const ushort code = ch.unicode();
        if (code < 32 || code == 127) {
            if (error) *error = QStringLiteral("Name contains invalid control characters.");
            return false;
        }
    }

    // Forbidden filesystem characters: \ / : * ? " < > |
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar& ch : name) {
        if (forbidden.contains(ch)) {
            if (error) *error = QStringLiteral("Name cannot contain any of the following characters: \\ / : * ? \" < > |");
            return false;
        }
    }

    // Trailing period or space (invalid on Windows and causes issues in portable filesystems)
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        if (error) *error = QStringLiteral("Name cannot end with a period or space.");
        return false;
    }

    // Windows reserved device names (e.g. CON, PRN, AUX, NUL, COM1-9, LPT1-9, or con.txt, aux.png)
    const int dotIndex = name.indexOf(QLatin1Char('.'));
    const QString baseStem = (dotIndex >= 0) ? name.left(dotIndex) : name;
    if (isWindowsReservedStem(baseStem)) {
        if (error) *error = QStringLiteral("\"%1\" is a reserved system device name.").arg(baseStem);
        return false;
    }

    return true;
}

QVariantMap MutationService::validateTargetName(const QString& parentDirectory,
                                               const QString& newName,
                                               const QString& currentPath) const {
    return validateName(parentDirectory, newName, currentPath);
}

namespace {
QVariantMap validateName(const QString& parentDirectory, const QString& newName, const QString& currentPath) {
    QVariantMap result;
    result[QStringLiteral("valid")] = false;
    result[QStringLiteral("error")] = QString();
    result[QStringLiteral("isCaseOnly")] = false;

    QString syntaxError;
    if (!MutationService::isValidFileName(newName, &syntaxError)) {
        result[QStringLiteral("error")] = syntaxError;
        return result;
    }

    const QString cleanParent = clean(parentDirectory);
    const QString targetPath = clean(QDir(cleanParent).filePath(newName));

    if (!currentPath.isEmpty()) {
        const QString cleanCurrent = clean(currentPath);
        if (cleanCurrent == targetPath) {
            // Name is unchanged - valid
            result[QStringLiteral("valid")] = true;
            return result;
        }

        if (cleanCurrent.compare(targetPath, Qt::CaseInsensitive) == 0) {
            // Case-only rename of the current item - valid!
            result[QStringLiteral("valid")] = true;
            result[QStringLiteral("isCaseOnly")] = true;
            return result;
        }
    }

    // Check collision with another item in parent directory
    if (QFileInfo::exists(targetPath) || QDir(targetPath).exists()) {
        result[QStringLiteral("error")] = QStringLiteral("An item with this name already exists.");
        return result;
    }

    result[QStringLiteral("valid")] = true;
    return result;
}
}

QString MutationService::suggestNewFolderName(const QString& parentDirectory,
                                             const QString& baseName) const {
    const QString cleanParent = clean(parentDirectory);
    const QString initial = baseName.trimmed().isEmpty() ? QStringLiteral("New Folder") : baseName.trimmed();

    QString candidate = initial;
    QString targetPath = clean(QDir(cleanParent).filePath(candidate));
    if (!QFileInfo::exists(targetPath) && !QDir(targetPath).exists()) {
        return candidate;
    }

    int suffix = 2;
    while (suffix < 10000) {
        candidate = QStringLiteral("%1 (%2)").arg(initial).arg(suffix);
        targetPath = clean(QDir(cleanParent).filePath(candidate));
        if (!QFileInfo::exists(targetPath) && !QDir(targetPath).exists()) {
            return candidate;
        }
        ++suffix;
    }

    return candidate;
}

QVariantMap MutationService::renamePath(const QString& sourcePath, const QString& newName) {
    QVariantMap result = performRename(sourcePath, newName);
    const bool unchanged = result.take(QStringLiteral("unchanged")).toBool();
    if (result.value(QStringLiteral("success")).toBool() && !unchanged) {
        emit pathRenamed(result.value(QStringLiteral("oldPath")).toString(), result.value(QStringLiteral("newPath")).toString());
    }
    return result;
}

quint64 MutationService::planRename(const QString& sourcePath, const QString& newName, const QString& scope) {
    // A newer preview supersedes the one being built and the one waiting. Empty only withdraws.
    d->planStop.request_stop();
    d->planStop = std::stop_source{};
    d->ready.reset();
    d->readyId = d->planningId = 0;
    if (sourcePath.isEmpty()) return 0;
    const quint64 id = ++d->nextId;
    d->planningId = id;
    auto* watcher = new QFutureWatcher<RenamePlan>(this);
    connect(watcher, &QFutureWatcher<RenamePlan>::finished, this, [this, watcher, id] {
        RenamePlan plan = watcher->result();
        watcher->deleteLater();
        if (id != d->planningId) return; // superseded: an older name can't replace a newer preview
        d->planningId = 0;
        if (plan.cancelled) return;
        QVariantMap summary = plan.summary;
        summary.insert(QStringLiteral("id"), id);
        d->ready = std::move(plan);
        d->readyId = id;
        emit renamePlanReady(id, summary);
    });
    watcher->setFuture(QtConcurrent::run(d->pool, [sourcePath, newName, scope = assets::detail::scopeFromName(scope), cancel = d->planStop.get_token()] {
        return buildRenamePlan(sourcePath, newName, scope, cancel);
    }));
    return id;
}

bool MutationService::confirmRename(quint64 id) {
    // Asset scope is never executable until B2: it needs the journal to recover from a partial failure.
    if (id == 0 || id != d->readyId || !d->ready || !d->ready->executable || d->runningId) return false;
    RenamePlan plan = std::move(*d->ready);
    d->ready.reset();
    d->readyId = 0; // one confirmation per plan
    d->runningId = id;
    using Outcome = QVariantMap;
    auto* watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, id] {
        const QVariantMap result = watcher->result();
        watcher->deleteLater();
        d->runningId = 0;
        const bool success = result.value(QStringLiteral("success")).toBool();
        const QString from = result.value(QStringLiteral("oldPath")).toString(), to = result.value(QStringLiteral("newPath")).toString();
        if (success && !result.value(QStringLiteral("unchanged")).toBool()) emit pathRenamed(from, to);
        emit renameFinished(id, QVariantList{QVariantMap{{"from", from}, {"to", to}, {"success", success}, {"error", result.value(QStringLiteral("error"))}}});
    });
    watcher->setFuture(QtConcurrent::run(d->pool, [plan = std::move(plan)]() -> QVariantMap {
        // What the preview named or nothing: a replaced or vanished source is reported, never substituted.
        const auto state = plan.snapshot ? assets::detail::compareToSnapshot(*plan.snapshot) : assets::detail::SnapshotState::Missing;
        if (state != assets::detail::SnapshotState::Same) {
            return {{"success", false}, {"oldPath", plan.source}, {"newPath", QString()},
                    {"error", state == assets::detail::SnapshotState::Missing ? QStringLiteral("Missing: it no longer exists.")
                                                                              : QStringLiteral("Changed: it changed after the preview, so it wasn't renamed.")}};
        }
        return performRename(plan.source, plan.newName);
    }));
    return true;
}

namespace {
// Performs the rename and reports it, without touching the service, so a worker can run it too.
QVariantMap performRename(const QString& sourcePath, const QString& newName) {
    QVariantMap result;
    result[QStringLiteral("success")] = false;
    result[QStringLiteral("oldPath")] = clean(sourcePath);
    result[QStringLiteral("newPath")] = QString();
    result[QStringLiteral("error")] = QString();

    const QString cleanSource = clean(sourcePath);
    QFileInfo srcInfo(cleanSource);
    if (!srcInfo.exists()) {
        result[QStringLiteral("error")] = QStringLiteral("Source item does not exist.");
        return result;
    }

    const QString parentDir = clean(srcInfo.dir().absolutePath());
    const QVariantMap validation = validateName(parentDir, newName, cleanSource);
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        result[QStringLiteral("error")] = validation.value(QStringLiteral("error")).toString();
        return result;
    }

    const QString cleanTarget = clean(QDir(parentDir).filePath(newName));
    if (cleanSource == cleanTarget) {
        result[QStringLiteral("success")] = true;
        result[QStringLiteral("newPath")] = cleanTarget;
        result[QStringLiteral("unchanged")] = true;
        return result;
    }

    const bool isCaseOnly = validation.value(QStringLiteral("isCaseOnly")).toBool();

    if (isCaseOnly) {
        // Safe case-only rename via unique intermediate temporary path
        const QString tempName = QStringLiteral(".tmp_rename_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        const QString tempPath = clean(QDir(parentDir).filePath(tempName));

        bool step1 = false;
        if (srcInfo.isDir()) {
            step1 = QDir().rename(cleanSource, tempPath);
        } else {
            step1 = QFile::rename(cleanSource, tempPath);
        }

        if (!step1) {
            result[QStringLiteral("error")] = QStringLiteral("Failed to rename item: permission denied or file in use.");
            return result;
        }

        bool step2 = false;
        if (srcInfo.isDir()) {
            step2 = QDir().rename(tempPath, cleanTarget);
        } else {
            step2 = QFile::rename(tempPath, cleanTarget);
        }

        if (!step2) {
            // Attempt rollback
            if (srcInfo.isDir()) {
                QDir().rename(tempPath, cleanSource);
            } else {
                QFile::rename(tempPath, cleanSource);
            }
            result[QStringLiteral("error")] = QStringLiteral("Failed to complete case rename: destination unavailable.");
            return result;
        }
    } else {
        bool ok = false;
        if (srcInfo.isDir()) {
            ok = QDir().rename(cleanSource, cleanTarget);
        } else {
            ok = QFile::rename(cleanSource, cleanTarget);
        }

        if (!ok) {
            result[QStringLiteral("error")] = QStringLiteral("Failed to rename item: permission denied or file in use.");
            return result;
        }
    }

    result[QStringLiteral("success")] = true;
    result[QStringLiteral("newPath")] = cleanTarget;
    return result;
}
}

QVariantMap MutationService::createFolder(const QString& parentDirectory, const QString& folderName) {
    QVariantMap result;
    result[QStringLiteral("success")] = false;
    result[QStringLiteral("path")] = QString();
    result[QStringLiteral("error")] = QString();

    const QString cleanParent = clean(parentDirectory);
    QDir parentDir(cleanParent);
    if (!parentDir.exists()) {
        result[QStringLiteral("error")] = QStringLiteral("Parent directory does not exist.");
        return result;
    }

    const QVariantMap validation = validateTargetName(cleanParent, folderName);
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        result[QStringLiteral("error")] = validation.value(QStringLiteral("error")).toString();
        return result;
    }

    const QString cleanTarget = clean(parentDir.filePath(folderName));
    if (!parentDir.mkdir(folderName)) {
        result[QStringLiteral("error")] = QStringLiteral("Failed to create folder: permission denied.");
        return result;
    }

    result[QStringLiteral("success")] = true;
    result[QStringLiteral("path")] = cleanTarget;
    emit folderCreated(cleanTarget);
    return result;
}

} // namespace glancelot

