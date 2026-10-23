#pragma once
// Private app helper (not a public contract): the one place that names the QML context properties ui/Main.qml reads.
// app/main.cpp and every shipped-QML test harness (tests/ui_test_support.hpp, grid, fs_watch and monitor UI tests)
// call it, so a service added here reaches the app and the tests together. References: every service is required.
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/favorites_model.hpp"
#include "glancelot/file_operation_service.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_history.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/monitor_customization.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QQmlContext>

struct ViewerContext {
    ViewerController& viewer;
    PaneLayout& paneLayout;
    glancelot::MonitorCustomization& monitorCustomization;
    FilmstripModel& filmstrip;
    FolderTreeModel& folderTree;
    FavoritesModel& favoritesModel;
    FolderHistory& folderHistory;
    DirectoryEntryModel& fileGrid;
    ThemeManager& theme;
    FileOperationService& fileOperations;
};

inline void installViewerContext(QQmlContext& context, const ViewerContext& services) {
    context.setContextProperty(QStringLiteral("viewer"), &services.viewer);
    context.setContextProperty(QStringLiteral("paneLayout"), &services.paneLayout);
    context.setContextProperty(QStringLiteral("monitorCustomization"), &services.monitorCustomization);
    context.setContextProperty(QStringLiteral("filmstrip"), &services.filmstrip);
    context.setContextProperty(QStringLiteral("folderTree"), &services.folderTree);
    context.setContextProperty(QStringLiteral("favoritesModel"), &services.favoritesModel);
    context.setContextProperty(QStringLiteral("folderHistory"), &services.folderHistory);
    context.setContextProperty(QStringLiteral("fileGrid"), &services.fileGrid);
    context.setContextProperty(QStringLiteral("theme"), &services.theme);
    context.setContextProperty(QStringLiteral("fileOperations"), &services.fileOperations);
}
