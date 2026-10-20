#include "glancelot/mutation_service.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QRegularExpression>

namespace glancelot {

namespace {

QString clean(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString fold(const QString& path) {
    const QString c = clean(path);
#ifdef Q_OS_WIN
    return c.toLower();
#else
    return c;
#endif
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

} // namespace

struct MutationService::Private {
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
    QVariantMap result;
    result[QStringLiteral("valid")] = false;
    result[QStringLiteral("error")] = QString();
    result[QStringLiteral("isCaseOnly")] = false;

    QString syntaxError;
    if (!isValidFileName(newName, &syntaxError)) {
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

        if (fold(cleanCurrent) == fold(targetPath)) {
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
    const QVariantMap validation = validateTargetName(parentDir, newName, cleanSource);
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        result[QStringLiteral("error")] = validation.value(QStringLiteral("error")).toString();
        return result;
    }

    const QString cleanTarget = clean(QDir(parentDir).filePath(newName));
    if (cleanSource == cleanTarget) {
        result[QStringLiteral("success")] = true;
        result[QStringLiteral("newPath")] = cleanTarget;
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
    emit pathRenamed(cleanSource, cleanTarget);
    return result;
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

