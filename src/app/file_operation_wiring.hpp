#pragma once
// Private app helper (not a public contract): reconciles the viewer, grid, filmstrip/thumbnails and folder tree after
// FileOperationService moved items to the trash. app/main.cpp and the grid UI test harness both call it.
#include "glancelot/file_operation_service.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QDir>
#include <QSet>

namespace file_operation_wiring {
inline QString key(const QString& path) {
    QString value = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    value = value.toCaseFolded();
#endif
    return value;
}
// Relists every expanded tree row that was the parent of a removed path (collapse + expand reloads that one level on
// the tree's worker); removed rows disappear with it. Bounded: one pass over the rows per distinct parent, at most 64.
inline void refreshTreeParents(FolderTreeModel& tree, const QStringList& removed) {
    QSet<QString> parents;
    for (const auto& path : removed) {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
        const int cut = clean.lastIndexOf(QLatin1Char('/'));
        if (cut <= 0) continue;
        QString parent = clean.left(cut);
        if (parent.endsWith(QLatin1Char(':'))) parent += QLatin1Char('/'); // a drive root keeps its separator
        parents.insert(key(parent));
        if (parents.size() >= 64) break;
    }
    for (const auto& parent : parents) {
        for (int row = 0; row < tree.count(); ++row) {
            if (key(tree.pathAt(row)) != parent) continue;
            if (tree.rowInfo(row).value(QStringLiteral("expanded")).toBool()) { tree.collapse(row); tree.expand(row); }
            break;
        }
    }
}
inline void follow(FileOperationService& operations, ViewerController& viewer, FolderTreeModel& tree) {
    QObject::connect(&operations, &FileOperationService::pathsRemoved, &viewer, [&viewer, &tree](const QStringList& paths) {
        // filesInvalidated (emitted by the controller) relists the grid, keeping surviving selection keys, and advances
        // the shared thumbnail epoch through the filmstrip; stale listings of older generations are discarded.
        viewer.reconcileRemovedPaths(paths);
        refreshTreeParents(tree, paths);
    });
}
}
