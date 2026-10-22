#pragma once
// Private app helper (not a public contract): the single place that wires the mixed file grid to the controller
// and the filmstrip. app/main.cpp and the grid UI test harness both call it, so tests exercise the shipped wiring.
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QDir>

namespace file_grid_wiring {
inline QString directoryKey(const QString& path) {
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
// The grid follows the controller's folder (compared by key, so stepping between photos never relists), its sort
// and seed, and the opened photo. An on-disk change refreshes the listing in place.
inline void followViewer(ViewerController& viewer, DirectoryEntryModel& grid) {
    const auto sync = [&viewer, &grid] {
        grid.setSort(viewer.sortKey(), viewer.sortDescending(), viewer.sortSeed());
        if (directoryKey(viewer.directory()) != directoryKey(grid.directory())) grid.setDirectory(viewer.directory());
        grid.setCurrentImage(viewer.filePath());
    };
    QObject::connect(&viewer, &ViewerController::stateChanged, &grid, sync);
    QObject::connect(&viewer, &ViewerController::filesInvalidated, &grid, [&grid] { grid.refresh(); });
    QObject::connect(&viewer, &ViewerController::filterChanged, &grid, [&viewer, &grid] {
        if (grid.filterPattern() != viewer.filterPattern() || grid.isFilterPersistent() != viewer.isFilterPersistent()) {
            grid.setFilter(viewer.filterPattern(), viewer.isFilterPersistent());
        }
    });
    QObject::connect(&grid, &DirectoryEntryModel::filterChanged, &viewer, [&viewer, &grid] {
        if (viewer.filterPattern() != grid.filterPattern() || viewer.isFilterPersistent() != grid.isFilterPersistent()) {
            viewer.setFilter(grid.filterPattern(), grid.isFilterPersistent());
        }
    });
}
// ThumbnailService accepts exactly one content epoch, which the filmstrip drives (folder changes and on-disk
// invalidation). The grid's thumbnail URLs must carry the same epoch or every grid request is rejected.
inline void lockThumbnailEpoch(FilmstripModel& filmstrip, DirectoryEntryModel& grid) {
    QObject::connect(&filmstrip, &FilmstripModel::epochChanged, &grid, [&filmstrip, &grid] {
        while (grid.epoch() < filmstrip.epoch()) grid.invalidate();
    });
    while (grid.epoch() < filmstrip.epoch()) grid.invalidate();
}
}
