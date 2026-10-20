#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>

namespace glancelot {

// Mutation service (RENAME-01 / F018 / SYS-06):
// Provides safe, atomic in-browser file and folder renaming and new folder creation.
//
// Validation rules:
// - Rejects empty or whitespace-only names.
// - Rejects illegal filesystem characters (\ / : * ? " < > |) and ASCII control characters (< 32).
// - Rejects Windows reserved device names (CON, PRN, AUX, NUL, COM1..9, LPT1..9) with or without extensions.
// - Rejects trailing periods and trailing spaces on Windows.
// - Enforces 255 character component length limit.
//
// Collision & Case-only rules:
// - Live validation checks whether parentDirectory/newName already exists.
// - If currentPath is supplied and matches newName case-insensitively, it is treated as a valid
//   case-only rename of the existing item rather than a collision.
//
// State reconciliation:
// - On successful rename, emits pathRenamed(oldPath, newPath).
// - On successful folder creation, emits folderCreated(folderPath).
class MutationService : public QObject {
    Q_OBJECT
public:
    explicit MutationService(QObject* parent = nullptr);
    ~MutationService() override;

    // Checks whether `name` is a syntactically valid single filename or folder name.
    static bool isValidFileName(const QString& name, QString* error = nullptr);

    // Live validation for UI: checks syntax and collisions in `parentDirectory`.
    // If `currentPath` is supplied and points to the item being renamed, a case-only
    // rename of the same item is permitted rather than flagged as an existing collision.
    // Returns QVariantMap:
    //   "valid": bool
    //   "error": QString (empty if valid)
    //   "isCaseOnly": bool (true if this is a valid case-only rename)
    Q_INVOKABLE QVariantMap validateTargetName(const QString& parentDirectory,
                                             const QString& newName,
                                             const QString& currentPath = QString()) const;

    // Suggests an unused folder name in `parentDirectory` starting from `baseName`.
    // E.g. "New Folder", "New Folder (2)", etc.
    Q_INVOKABLE QString suggestNewFolderName(const QString& parentDirectory,
                                            const QString& baseName = QStringLiteral("New Folder")) const;

    // Renames `sourcePath` (file or folder) to `newName`.
    // Performs validation, case-only handling, and atomic filesystem rename.
    // Returns QVariantMap:
    //   "success": bool
    //   "oldPath": QString
    //   "newPath": QString
    //   "error": QString
    // Emits pathRenamed(oldPath, newPath) on success.
    Q_INVOKABLE QVariantMap renamePath(const QString& sourcePath, const QString& newName);

    // Creates a new folder named `folderName` inside `parentDirectory`.
    // Returns QVariantMap:
    //   "success": bool
    //   "path": QString
    //   "error": QString
    // Emits folderCreated(path) on success.
    Q_INVOKABLE QVariantMap createFolder(const QString& parentDirectory, const QString& folderName);

signals:
    void pathRenamed(const QString& oldPath, const QString& newPath);
    void folderCreated(const QString& folderPath);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace glancelot
