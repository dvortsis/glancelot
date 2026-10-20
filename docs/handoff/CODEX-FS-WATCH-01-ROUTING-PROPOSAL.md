# FS-WATCH-01 workspace routing contract correction

Lead inspection found that ui/Main.qml::gridAllowsCommand is a fixed allow-list.
The frozen plan assumed generic routing would handle refresh; this is true in
preview but false in grid focus. The backend cannot satisfy workspace F5 without
one shared route addition. Do not work around modal/text precedence with a second
shortcut dispatcher.

Proposed narrow lead amendment under the next FS integration lock: assign Codex
ownership of adding only the existing refresh command ID to that allow-list in
this packet. All other Main.qml behavior remains with its current UI packet owner.
The assigned source worktree is codex-fs-watch-01. The lead freezes this exception
before applying it, and enables the dedicated fs_watch_ui test target in the
existing frozen CMake fragment. No public C++ header or new product control is needed.

New real-QML tests are in tests/fs_watch_ui_tests.cpp, never added to grid_ui_tests.
They exercise actual F5 in preview/grid/combined modes and preserve text/modal
precedence. The independent Claude review must cover the route and native UI
skill requirements. Until amendment, route change and gates complete, the packet
remains implementing and full workspace F5 is not claimed.
