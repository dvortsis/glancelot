#include "glancelot/command_registry.hpp"
#include "binding_validation.hpp"
#include <vector>
#include <utility>

namespace glancelot {
std::span<const Command> commandRegistry() {
// The controller may call before main from another translation unit. A local
// static is initialized on that first use, rather than exposing an empty span.
static const std::vector<Command> registry {
    {"open","Open image","Ctrl+O"}, {"next","Next image","Right",true}, {"previous","Previous image","Left",true},
    {"next-folder","Next folder","PgDown"}, {"previous-folder","Previous folder","PgUp"},
    {"palette","Find a command","Ctrl+Shift+P"}, {"settings","Settings","Ctrl+,"},
    {"fullscreen","Toggle fullscreen","F11"}, {"fit","Fit image (shrink only)","F"},
    {"actual","Actual size (physical 1:1)","1"}, {"zoom-in","Zoom in","+",true}, {"zoom-out","Zoom out","-",true},
    {"rotate","Rotate view clockwise","R"}, {"background","Cycle canvas background","B"},
    {"grid","Toggle folder grid","G"}, {"frameless","Toggle window frame","Ctrl+F11"}, {"filmstrip","Toggle filmstrip","T"}, {"tree","Toggle folder tree","Ctrl+B"}, {"sort","Cycle file sort (name, modified, size, shuffle)","S"}, {"sort-reverse","Reverse file sort direction","Shift+S"},
    // Layout commands start unbound so existing users' shortcuts never collide.
    {"arrange-panes","Arrange panes",""}, {"layout-lock","Lock or unlock workspace layout",""}, {"layout-reset","Reset workspace layout",""},
    // Content modes (GRID-PREVIEW-01). Ctrl+Shift+G was free; saved maps that already use it keep their choice.
    {"view-preview","Show photo preview only",""}, {"view-grid","Show folder grid only",""},
    {"view-combined","Show grid and preview together","Ctrl+Shift+G"}, {"combined-orientation","Switch grid and preview side by side or stacked",""},
    {"reveal-in-file-manager", "Reveal in File Manager", "Ctrl+Alt+R"},
    {"open-terminal", "Open Terminal", "Ctrl+Alt+T"},
    {"copy-path", "Copy file path", "Ctrl+C"},
    {"copy-file", "Copy file", "Ctrl+Shift+C"},
    {"copy-image", "Copy image", "Ctrl+Alt+C"},
    {"select-all", "Select all items in grid", ""},
    {"clear-selection", "Clear grid selection", "Ctrl+D"},
    {"invert-selection", "Invert grid selection", "Ctrl+I"},
    {"grid-zoom-in", "Zoom in thumbnails", "Ctrl+="},
    {"grid-zoom-out", "Zoom out thumbnails", "Ctrl+-"},
    {"grid-zoom-reset", "Reset thumbnail size", "Ctrl+0"},
    // FS-WATCH-01: workspace command; generic routing preserves text/modal precedence.
    {"refresh", "Refresh", "F5"},
    // FAVORITES-01 (F003)
    {"add-to-favorites", "Add current folder to favorites", ""},
    // FOLDER-HISTORY-01 (F001)
    {"navigate-back", "Go back to previous folder", "Alt+Left"},
    {"navigate-forward", "Go forward to next folder", "Alt+Right"},
    // SHORTCUTS-HELP-01 (F056)
    {"shortcuts-help", "Keyboard shortcuts cheat sheet", "F1"},
    // IO-01 Storage Profile Commands
    {"storage-profile-auto", "Set storage profile to Auto", ""},
    {"storage-profile-ssd", "Set storage profile to SSD (Maximum concurrency)", ""},
    {"storage-profile-hdd", "Set storage profile to HDD (Serialized background I/O)", ""},
    {"storage-profile-network", "Set storage profile to Network (Throttled I/O)", ""},
    // FILTER-01
    {"filter", "Filter folder files", "Ctrl+F"},
    {"clear-filter", "Clear active filter", ""},
    {"toggle-filter-persistence", "Toggle filter persistence", ""},
    // META-HUD-01 (META-01 / UX-04)
    {"toggle-metadata", "Toggle metadata inspector", "I"},
    // ANIM-01 (ADV-06 / FMT-03 / F036)
    {"animation-toggle-play-pause", "Play or pause animation", "Space"},
    {"animation-next-frame", "Step to next frame / page", "."},
    {"animation-previous-frame", "Step to previous frame / page", ","},
    {"animation-speed-up", "Increase animation speed", "]"},
    {"animation-speed-down", "Decrease animation speed", "["},
    {"animation-toggle-loop", "Toggle animation loop", ""},
    // RENAME-01 (F018)
    {"rename", "Rename current file or folder", "F2"},
    {"new-folder", "Create new folder", "Ctrl+Shift+N"},
    // COPY-MOVE-01 (F020)
    {"cut-selection", "Cut selected files", "Ctrl+X"},
    {"copy-selection", "Copy selected files", ""},
    {"paste", "Paste files to current folder", "Ctrl+V"},
    // MONITOR-OUTPUT-01 (UX-10 / COLOR-01 / VIEW-04 / PERF-04 / SYS-02)
    {"presentation-toggle", "Toggle second display presentation", "F8"},
    {"presentation-mirror", "Presentation: Mirror current photo", ""},
    {"presentation-pin", "Presentation: Pin current photo", ""},
    {"presentation-compare", "Presentation: Comparison mode", ""},
    {"presentation-close", "Close presentation display", ""},
#ifdef Q_OS_WIN
    {"delete-selection","Move selected items to the Recycle Bin","Delete"},
    {"delete-current-file","Move current photo to the Recycle Bin","Ctrl+Delete"},
    {"delete-current-folder","Move current folder to the Recycle Bin","Ctrl+Shift+Delete"},
#else
    {"delete-selection","Move selected items to the Trash","Delete"},
    {"delete-current-file","Move current photo to the Trash","Ctrl+Delete"},
    {"delete-current-folder","Move current folder to the Trash","Ctrl+Shift+Delete"},
#endif
    {"quit","Quit","Ctrl+Q"}
};
    return {registry.data(),registry.size()};
}

BindingResolution resolveCommandBindings(const QJsonObject& stored) {
    using bindings::shortcutOverlap;
    const auto registry=commandRegistry();
    QJsonObject defaultBindings;
    for (const auto& command: registry) defaultBindings[QString::fromStdString(command.id)] = QString::fromStdString(command.defaultBinding);
    auto candidate=defaultBindings;
    for (auto it=stored.begin();it!=stored.end();++it) {
        if (candidate.contains(it.key()) && it.value().isString()) candidate[it.key()]=QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText).toString(QKeySequence::PortableText);
    }
    // A command added after the settings file was written gets its default only if that key is still free;
    // otherwise the user's explicit choice wins and the new command starts unassigned.
    for (auto it=candidate.begin();it!=candidate.end();++it) {
        if (stored.contains(it.key()) || it.value().toString().isEmpty()) continue;
        for (auto other=stored.begin();other!=stored.end();++other) {
            if (other.key()!=it.key() && candidate.contains(other.key()) && shortcutOverlap(QKeySequence::fromString(candidate.value(other.key()).toString(),QKeySequence::PortableText),QKeySequence::fromString(it.value().toString(),QKeySequence::PortableText))) { it.value()=QString(); break; }
        }
    }
    BindingResolution result;
    result.valid=bindings::usableBindings(candidate);
    result.effective=std::move(candidate);
    if(!result.valid)result.error="Saved shortcuts conflict or use reserved Escape; default shortcuts are active. Reset shortcuts in Settings to repair them.";
    return result;
}
}
