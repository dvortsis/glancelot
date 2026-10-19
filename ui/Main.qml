import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQml.Models
import Glancelot

ApplicationWindow {
    id: app
    objectName: "viewerWindow"
    width: 1180
    height: 780
    minimumWidth: 540
    minimumHeight: 360
    visible: true
    title: viewer.fileName ? viewer.fileName + " — Glancelot" : "Glancelot"
    color: Theme.background
    property bool frameless: false
    property bool hudVisible: true
    property bool gridMode: false
    property bool layoutEditing: false
    property var resizePreview: ({})
    // ---- GRID-PREVIEW-01 content modes. `gridMode` keeps meaning "grid only" and mirrors paneLayout.contentMode,
    // so the existing grid toggle, Escape and tests keep their behaviour; combined mode shows grid and preview.
    readonly property bool combinedMode: paneLayout.contentMode === "combined" && !gridMode
    property string focusedPane: "grid"        // combined mode: the content pane that owns keys
    property real splitDrag: -1                // transient split ratio while the handle is dragged (never persisted)
    property bool previewFollowsCursor: false  // false: the preview shows what the viewer itself navigated to
    property string cursorKey: ""
    property string splitMessage: ""
    readonly property bool modeCompact: width < 760 || !topPanel.horizontalBar || topPanel.modeSpace < modeSelector.implicitWidth + barGridtopbar.columnSpacing
    readonly property var contentModes: [
        { id: "preview", command: "view-preview", label: "Photo", icon: "image", tip: "Photo only" },
        { id: "grid", command: "view-grid", label: "Grid", icon: "grid", tip: "Folder grid only" },
        { id: "combined", command: "view-combined", label: "Both", icon: "split", tip: "Grid and photo together" }
    ]
    // Explicit state of the combined preview: "" shows the decoded photo; "loading" keeps the canvas under a delayed
    // indicator; every other state covers the canvas so a stale photo is never presented as the cursor's item.
    readonly property string previewState: {
        if (!combinedMode) return ""
        const follow = previewFollowsCursor && workspace._gridShown
        if (follow) {
            if (viewer.directoryPending) return "loading-folder"
            if (fileGrid.directory.length === 0) return "no-folder"
            const grid = photoGrid.stateName
            if (grid === "loading") return "loading-folder"
            if (grid === "inaccessible" || grid === "empty") return grid
            const row = fileGrid.cursorRow, count = fileGrid.count
            if (row < 0 || row >= count) return "none"
            const kind = fileGrid.kindAt(row)
            if (kind === "folder") return "folder"
            if (kind !== "image") return "unsupported"
            if (pathKey(viewer.filePath) !== pathKey(fileGrid.pathAt(row)) || viewer.loading) return "loading"
            return viewer.imageReady ? "" : "failed"
        }
        if (viewer.filePath.length === 0) return viewer.directory.length === 0 && fileGrid.directory.length === 0 ? "no-folder" : "none"
        if (viewer.loading) return "loading"
        return viewer.imageReady ? "" : "failed"
    }
    readonly property string previewSubject: previewFollowsCursor && workspace._gridShown && fileGrid.cursorRow >= 0 ? fileGrid.pathAt(fileGrid.cursorRow) : viewer.filePath
    function pathKey(path) { const p = String(path).replace(/\\/g, "/"); return Qt.platform.os === "windows" ? p.toLowerCase() : p }
    function leafName(path) { const p = String(path).replace(/\\/g, "/").replace(/\/+$/, ""); return p.slice(p.lastIndexOf("/") + 1) }
    function syncMode() { gridMode = paneLayout.contentMode === "grid" }
    onGridModeChanged: if (gridMode !== (paneLayout.contentMode === "grid")) paneLayout.setContentMode(gridMode ? "grid" : "preview")
    onCombinedModeChanged: {
        previewDebounce.stop(); splitDrag = -1
        if (combinedMode) { previewFollowsCursor = false; focusedPane = "grid" }
    }
    Component.onCompleted: syncMode()
    Connections {
        target: paneLayout
        function onChanged() { app.syncMode(); if (paneLayout.locked) app.splitDrag = -1 }
    }
    function setContentMode(mode) {
        canvas.endLoupe()
        paneLayout.setContentMode(mode); syncMode(); wakeHud()
        Qt.callLater(focusContent)
    }
    function toggleOrientation() {
        const next = paneLayout.centerSplit.orientation === "vertical" ? "horizontal" : "vertical"
        if (!paneLayout.setCenterOrientation(next)) showSplitMessage(paneLayout.feedback)
        else if (!combinedMode) showSplitMessage(next === "vertical" ? "Grid and photo will be stacked in the Both view." : "Grid and photo will sit side by side in the Both view.")
    }
    function showSplitMessage(text) { splitMessage = text; splitMessageTimer.restart() }
    // Moves keys between the grid and the preview. A pane revealed by the single-pane fallback becomes visible
    // with the binding update, so focus is retried once on the next turn.
    function focusPane(name) {
        focusedPane = name
        const go = () => { if (!app.combinedMode) return; if (name === "grid") photoGrid.focusGrid(); else canvas.forceActiveFocus(Qt.TabFocusReason) }
        go(); Qt.callLater(go)
    }
    onActiveFocusItemChanged: {
        if (!combinedMode || !activeFocusItem) return
        if (focusWithin(photoGrid)) focusedPane = "grid"
        else if (activeFocusItem === canvas) focusedPane = "preview"
    }
    // A cursor move made in the grid (keys, pointer, entering a folder) drives the preview; a relisting while the
    // preview has focus does not. Multi-selection changes that leave the cursor in place never reach the preview.
    function cursorMaybeMoved() {
        const key = fileGrid.cursorRow >= 0 ? pathKey(fileGrid.pathAt(fileGrid.cursorRow)) : ""
        if (key === cursorKey) return
        cursorKey = key
        if (!combinedMode || !focusWithin(photoGrid)) return
        previewFollowsCursor = true
        schedulePreview()
    }
    // Debounced so a held arrow key issues one decode for the item it stops on; the controller's generation drops
    // any result that a newer open has replaced.
    function cursorImage() {
        const row = fileGrid.cursorRow
        return combinedMode && previewFollowsCursor && workspace._gridShown && !viewer.directoryPending && row >= 0
            && row < fileGrid.count && fileGrid.kindAt(row) === "image" ? fileGrid.pathAt(row) : ""
    }
    function schedulePreview() {
        const path = cursorImage()
        if (path.length && !(pathKey(viewer.filePath) === pathKey(path) && (viewer.loading || viewer.imageReady))) previewDebounce.restart()
        else previewDebounce.stop()
    }
    Connections {
        target: fileGrid
        function onSelectionChanged() { Qt.callLater(app.cursorMaybeMoved) }
    }
    Connections {
        target: viewer
        function onStateChanged() { if (viewer.directoryPending) previewDebounce.stop() } // a folder change discards a pending preview
    }
    Timer {
        id: previewDebounce
        objectName: "previewDebounce"
        interval: 120
        onTriggered: {
            const path = app.cursorImage()
            if (path.length && !(app.pathKey(viewer.filePath) === app.pathKey(path) && (viewer.loading || viewer.imageReady))) viewer.open(path)
        }
    }
    Timer { id: splitMessageTimer; interval: 5000; onTriggered: app.splitMessage = "" }
    property bool previewLoadingShown: false
    onPreviewStateChanged: { previewLoadingShown = false; if (previewState === "loading") previewLoadingDelay.restart(); else previewLoadingDelay.stop() }
    Timer { id: previewLoadingDelay; interval: 200; onTriggered: app.previewLoadingShown = true }
    // ---- end GRID-PREVIEW-01 state
    readonly property var workspace: {
        const state=JSON.parse(JSON.stringify(paneLayout.panes))
        // Content panes fill the center wherever they are saved. In the combined view the grid's saved edge decides
        // which side (left/top) or end (right/bottom) of the split it takes.
        const gridEdge=state.grid.region,gridOn=gridMode||combinedMode,previewOn=!gridMode
        state.grid.region="center";state.preview.region="center"
        const all=["topbar","statusbar","filmstrip","tree","preview","grid"]
        // shown: present whenever the HUD is awake. docked: stays up regardless of auto-hide and so reserves canvas
        // space. Auto-hiding bars overlay the photo (as before PANES-01) so the canvas never reflows with the HUD.
        const shown={tree:(viewer.treeVisible||state.tree.frozen)&&state.tree.visible,
            topbar:state.topbar.visible||frameless, statusbar:state.statusbar.visible,
            filmstrip:state.filmstrip.visible&&!gridOn&&(viewer.filmstripVisible||state.filmstrip.frozen)&&filmstrip.count>0,
            preview:previewOn,grid:gridOn}
        const docked={tree:shown.tree&&(viewer.treePinned||state.tree.frozen),
            topbar:shown.topbar&&(gridOn||state.topbar.frozen),
            statusbar:shown.statusbar&&(gridOn||state.statusbar.frozen),
            filmstrip:shown.filmstrip&&state.filmstrip.frozen,
            preview:shown.preview,grid:shown.grid}
        const active={tree:shown.tree&&(docked.tree||hudVisible||frameless),
            topbar:shown.topbar&&(docked.topbar||hudVisible||frameless),
            statusbar:shown.statusbar&&(docked.statusbar||hudVisible),
            filmstrip:shown.filmstrip&&(docked.filmstrip||hudVisible||frameless),
            preview:shown.preview,grid:shown.grid}
        const ids=all.filter(id=>active[id])
        function horizontal(id) { return ["top","bottom"].indexOf(state[id].region)>=0 }
        function overlay(id) { return id==="tree"&&(width<720||!viewer.treePinned)&&["left","right"].indexOf(state[id].region)>=0 }
        function minimum(id) {
            if(id==="topbar"||id==="statusbar")return horizontal(id)?48:200
            if(id==="tree")return 200
            if(id==="filmstrip")return horizontal(id)?68:100
            return 120
        }
        function group(list,horizontalAxis) { return list.filter(id=>state[id].region!=="center"&&!overlay(id)&&horizontal(id)===horizontalAxis) }
        const shownIds=all.filter(id=>shown[id])
        const reservedCenter=shownIds.some(id=>overlay(id))?320:120
        // Decided on the HUD-independent set so recovery cannot flip on and off with auto-hide.
        let recovered=group(shownIds,true).reduce((n,id)=>n+minimum(id),0)>height-120 || group(shownIds,false).reduce((n,id)=>n+minimum(id),0)>width-reservedCenter
        if(recovered) {
            const defaults={topbar:["top",0,48],statusbar:["bottom",0,48],filmstrip:["top",1,92],tree:["left",0,280],preview:["center",0,640],grid:["center",1,640]}
            for(const id of Object.keys(defaults)) {
                state[id].region=defaults[id][0];state[id].order=defaults[id][1];state[id].extent=defaults[id][2]
            }
        }
        function preferred(id) { return Math.max(minimum(id),!recovered&&resizePreview[id]!==undefined?resizePreview[id]:state[id].extent) }
        // Stacks edge panes outward-in by order (ties keep the stable id order) and returns the remaining center.
        function place(list) {
            const extents={}
            for(const axis of [true,false]) {
                const members=group(list,axis),budget=axis?height-120:width-reservedCenter
                const base=members.reduce((n,id)=>n+minimum(id),0)
                const extra=members.reduce((n,id)=>n+preferred(id)-minimum(id),0)
                const scale=Math.min(1,Math.max(0,budget-base)/Math.max(1,extra))
                for(const id of members)extents[id]=minimum(id)+(preferred(id)-minimum(id))*scale
            }
            const bounds={x:0,y:0,width:width,height:height},rects={}
            // Docked panes stack outermost so they keep the place the canvas reserved for them while auto-hiding
            // bars come and go; within each group, saved order then the stable id order.
            const ordered=list.slice().sort((a,b)=>(docked[a]?0:1)-(docked[b]?0:1)||state[a].order-state[b].order||all.indexOf(a)-all.indexOf(b))
            for(const region of ["top","bottom","left","right"]) {
                for(const id of ordered.filter(id=>state[id].region===region&&!overlay(id))) {
                    const h=horizontal(id),extent=extents[id]
                    const rect={x:bounds.x,y:bounds.y,width:h?bounds.width:extent,height:h?extent:bounds.height}
                    if(region==="bottom")rect.y+=bounds.height-extent
                    if(region==="right")rect.x+=bounds.width-extent
                    rects[id]=rect
                    if(h){if(region==="top")bounds.y+=extent;bounds.height-=extent}
                    else{if(region==="left")bounds.x+=extent;bounds.width-=extent}
                }
            }
            return {rects:rects,bounds:bounds}
        }
        const live=place(ids),bounds=place(all.filter(id=>docked[id])).bounds
        const result=live.rects
        for(const id of ids.filter(id=>state[id].region==="center"))result[id]=Object.assign({},bounds)
        // Combined view (GRID-PREVIEW-01): split the center using the saved orientation and ratio (the grid's share).
        // Minimum extents win; a center too small for the saved orientation tries the other one, then shows
        // only the focused pane. These fallbacks are display-only and are never persisted.
        const split={fallback:"",orientation:"",gridFirst:gridEdge!=="right"&&gridEdge!=="bottom",origin:0,lead:0,total:0,gridShare:0.5,
            handle:{x:0,y:0,width:0,height:0}}
        if(combinedMode&&result.grid&&result.preview) {
            const mins={grid:{width:280,height:200},preview:{width:240,height:160}}
            const fits=o=>o==="horizontal"?bounds.width>=mins.grid.width+mins.preview.width&&bounds.height>=mins.grid.height
                :bounds.height>=mins.grid.height+mins.preview.height&&bounds.width>=mins.grid.width
            let o=paneLayout.centerSplit.orientation==="vertical"?"vertical":"horizontal"
            if(!fits(o)) { const other=o==="horizontal"?"vertical":"horizontal"; o=fits(other)?other:""; split.fallback=o.length?"reoriented":"single" }
            if(o.length) {
                const h=o==="horizontal",first=split.gridFirst?"grid":"preview",second=split.gridFirst?"preview":"grid"
                const total=h?bounds.width:bounds.height,gridMin=h?mins.grid.width:mins.grid.height,previewMin=h?mins.preview.width:mins.preview.height
                // `ratio` is the grid's share, so moving the grid to the other edge keeps its size.
                const share=split.fallback===""&&splitDrag>=0?splitDrag:paneLayout.centerSplit.ratio
                const gridExtent=Math.round(Math.max(gridMin,Math.min(total-previewMin,total*share)))
                const lead=split.gridFirst?gridExtent:total-gridExtent
                split.gridShare=gridExtent/total
                result[first]=h?{x:bounds.x,y:bounds.y,width:lead,height:bounds.height}:{x:bounds.x,y:bounds.y,width:bounds.width,height:lead}
                result[second]=h?{x:bounds.x+lead,y:bounds.y,width:total-lead,height:bounds.height}:{x:bounds.x,y:bounds.y+lead,width:bounds.width,height:total-lead}
                split.handle=h?{x:bounds.x+lead-4,y:bounds.y,width:8,height:bounds.height}:{x:bounds.x,y:bounds.y+lead-4,width:bounds.width,height:8}
                split.orientation=o;split.origin=h?bounds.x:bounds.y;split.lead=lead;split.total=total
            } else result[focusedPane==="preview"?"grid":"preview"]={x:0,y:0,width:0,height:0}
        }
        // content: the canvas area minus an overlay tree. clear: what is unobstructed right now (also minus the
        // auto-hiding bars that are visible), for notices and toasts that must not cover controls.
        const contentId=gridMode||(combinedMode&&result.preview&&result.preview.width===0)?"grid":"preview"
        let content=Object.assign({},result[contentId]||bounds),clear=Object.assign({},live.bounds)
        function uncover(area,cover) {
            const intersection=Math.max(0,Math.min(area.x+area.width,cover.x+cover.width)-Math.max(area.x,cover.x))
            if(cover.y<area.y+area.height && cover.y+cover.height>area.y && intersection>0) {
                if(cover.x<=area.x)area.x+=intersection
                area.width-=intersection
            }
        }
        for(const id of ids.filter(id=>overlay(id))) {
            // Overlays sit beside the bars that are visible now, not only the docked ones.
            const area=live.bounds,extent=Math.min(preferred(id),Math.max(0,area.width-120))
            result[id]={x:state[id].region==="right"?area.x+area.width-extent:area.x,y:area.y,width:extent,height:area.height}
            uncover(content,result[id]);uncover(clear,result[id])
        }
        for(const id of Object.keys(active))if(!active[id])result[id]={x:0,y:0,width:0,height:0}
        result._state=state;result._recovered=recovered;result._content=content;result._clear=clear;result._split=split
        result._gridShown=gridOn&&result.grid.width>0&&result.grid.height>0
        result._previewShown=previewOn&&result.preview.width>0&&result.preview.height>0
        return result
    }
    function paneRect(id) { return workspace[id]||{x:0,y:0,width:0,height:0} }
    function paneState(id) { return workspace._state[id] }

    readonly property bool editingText: activeFocusItem !== null && typeof activeFocusItem.selectAll === "function" && typeof activeFocusItem.text === "string"
    property bool modalOpen: settingsDialog.opened || settingsDialog.visible || openDialog.visible || deleteFlow.modal
    property real pointerX: width / 2
    property real pointerY: height / 2
    property real wheelRemainder: 0
    flags: Qt.Window | (frameless ? Qt.FramelessWindowHint : 0)
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBody
    // Basic-style controls that are not restyled individually (tool tips, busy states) read these roles.
    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: Theme.background
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.brightText: Theme.textOnPrimary
    palette.highlight: Theme.primary
    palette.highlightedText: Theme.textOnPrimary
    palette.light: Theme.hover
    palette.midlight: Theme.hover
    palette.mid: Theme.border
    palette.dark: Theme.muted
    palette.shadow: Theme.scrim
    palette.placeholderText: Theme.muted
    palette.toolTipBase: Theme.text
    palette.toolTipText: Theme.background
    palette.link: Theme.primary

    // Leaving arrangement returns keyboard focus to content so no hidden grip keeps editing the layout.
    onLayoutEditingChanged: { if(layoutEditing)wakeHud();else { resizePreview=({}); if(!modalOpen)Qt.callLater(focusContent) } }
    Connections { target: paneLayout; function onChanged() { if(paneLayout.locked)app.resizePreview=({}) } }
    function wakeHud() { hudVisible = true; hideHud.restart() }
    function focusWithin(item) {
        for (let focused = activeFocusItem; focused; focused = focused.parent)
            if (focused === item) return true
        return false
    }
    // Explicit routes (toolbar, command search) run canvas commands in the combined view whatever pane has focus;
    // window shortcuts follow the focused pane (`gridScope`).
    function invoke(id) {
        if (gridMode && !gridAllowsCommand(id)) return
        if (combinedMode && ["next", "previous", "next-folder", "previous-folder"].indexOf(id) >= 0) previewFollowsCursor = false
        viewer.trigger(id); wakeHud()
    }
    readonly property bool gridScope: gridMode || (combinedMode && focusedPane === "grid")
    function focusContent() { if (gridMode || (combinedMode && focusedPane === "grid")) photoGrid.focusGrid(); else canvas.forceActiveFocus() }
    function showCanvas() {
        if (combinedMode) { previewFollowsCursor = false; focusPane("preview"); wakeHud(); return } // an explicit open
        gridMode = false; canvas.forceActiveFocus(); wakeHud()
    }
    function gridAllowsCommand(id) {
        return ["open", "settings", "palette", "quit", "grid", "tree", "fullscreen", "frameless", "sort", "sort-reverse", "arrange-panes", "layout-lock", "layout-reset",
                "view-preview", "view-grid", "view-combined", "combined-orientation",
                "reveal-in-file-manager", "open-terminal",
                "copy-path", "copy-file", "copy-image",
                "delete-selection", "delete-current-file", "delete-current-folder",
                "select-all", "clear-selection", "invert-selection",
                "grid-zoom-in", "grid-zoom-out", "grid-zoom-reset"].indexOf(id) >= 0
    }
    function fuzzyMatch(text, query) {
        text = text.toLowerCase(); query = query.toLowerCase()
        let offset = 0
        for (let i = 0; i < query.length; ++i) {
            offset = text.indexOf(query[i], offset)
            if (offset < 0) return false
            ++offset
        }
        return true
    }
    function shortcutHint(id) { const binding = viewer.bindingFor(id); return binding.length ? " (" + binding + ")" : "" }
    // Kept for --show-themes: appearance now lives in Settings.
    function openThemes() { settingsDialog.openSection("appearance", false) }
    function handleViewCommand(id) {
        canvas.endLoupe()
        if (id === "grid") { gridMode = !gridMode; wakeHud(); if (!gridMode) canvas.forceActiveFocus() }
        else if (id === "open") openDialog.open()
        else if (id === "fullscreen") visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
        else if (id === "palette") settingsDialog.openSection("controls", true)
        else if (id === "settings") settingsDialog.openSection(settingsDialog.section, false)
        else if (id === "fit") canvas.fit("shrink")
        else if (id === "actual") canvas.actualSize()
        else if (id === "zoom-in") canvas.zoomAt(1.25, pointerX, pointerY)
        else if (id === "zoom-out") canvas.zoomAt(0.8, pointerX, pointerY)
        else if (id === "rotate") canvas.rotateClockwise()
        else if (id === "background") canvas.cycleBackground()
        else if (id === "frameless") { frameless = !frameless; wakeHud() }
        else if (id === "arrange-panes") layoutEditing = true // a locked layout explains itself inside arrangement
        else if (id === "layout-lock") { paneLayout.locked = !paneLayout.locked; wakeHud() }
        else if (id === "layout-reset") paneLayout.reset()
        else if (id === "view-preview" || id === "view-grid" || id === "view-combined") setContentMode(id.slice(5))
        else if (id === "combined-orientation") toggleOrientation()
        else if (id === "delete-selection") {
            const treeFolder = treePanel.currentSelectedFolder()
            if (treeFolder.length > 0) {
                deleteFlow.request([treeFolder], "tree", "")
            } else if (focusWithin(photoGrid) || (workspace._gridShown && fileGrid.count > 0)) {
                if (photoGrid.deletionTargets().length > 0) {
                    deleteFlow.request(photoGrid.deletionTargets(), "command", fileGrid.directory)
                } else if (workspace._previewShown && viewer.filePath.length > 0) {
                    deleteFlow.request([viewer.filePath], "command", viewer.directory)
                } else {
                    deleteFlow.showNotice("Select items in the grid to move them to the " + deleteFlow.trashName + ".")
                }
            } else if (viewer.filePath.length > 0) {
                deleteFlow.request([viewer.filePath], "command", viewer.directory)
            } else {
                deleteFlow.showNotice("Select items in the grid to move them to the " + deleteFlow.trashName + ".")
            }
        }
        else if (id === "delete-current-file") {
            if (viewer.filePath.length > 0) {
                deleteFlow.request([viewer.filePath], "command", viewer.directory)
            } else {
                deleteFlow.showNotice("No photo is currently open to move to the " + deleteFlow.trashName + ".")
            }
        }
        else if (id === "delete-current-folder") {
            const folder = viewer.directory.length > 0 ? viewer.directory : fileGrid.directory
            if (folder.length > 0) {
                deleteFlow.request([folder], "command", "")
            } else {
                deleteFlow.showNotice("No folder is currently open to move to the " + deleteFlow.trashName + ".")
            }
        }
        else if (id === "reveal-in-file-manager") {
            let target = ""
            const hasGridFocusOrSelection = focusWithin(photoGrid) || gridScope || photoGrid.selectedCount > 0
            const targets = photoGrid.deletionTargets()
            if (hasGridFocusOrSelection && (photoGrid.selectedPath.length > 0 || targets.length > 0)) {
                target = photoGrid.selectedPath.length > 0 ? photoGrid.selectedPath : targets[0]
            } else if (viewer.filePath.length > 0) {
                target = viewer.filePath
            } else {
                target = viewer.directory.length > 0 ? viewer.directory : fileGrid.directory
            }
            viewer.revealInFileManager(target)
        }
        else if (id === "open-terminal") {
            const folder = viewer.directory.length > 0 ? viewer.directory : fileGrid.directory
            viewer.openTerminal(folder)
        }
        else if (id === "copy-path" || id === "copy-file") {
            let targets = []
            const gridActive = focusWithin(photoGrid) || gridScope || gridMode || (workspace._gridShown && fileGrid.count > 0) || photoGrid.selectedCount > 0
            if (gridActive) {
                const dt = photoGrid.deletionTargets()
                if (dt.length > 0) targets = dt
                else if (photoGrid.selectedPath.length > 0) targets = [photoGrid.selectedPath]
            }
            if (targets.length === 0 && viewer.filePath.length > 0) {
                targets = [viewer.filePath]
            }
            if (id === "copy-path") viewer.copyFilePath(targets)
            else viewer.copyFileUrls(targets)
        }
        else if (id === "copy-image") {
            viewer.copyImageBitmap()
        }
        else if (id === "select-all") { if (workspace._gridShown || gridScope) fileGrid.selectAll() }
        else if (id === "clear-selection") { if (workspace._gridShown || gridScope) fileGrid.clearSelection() }
        else if (id === "invert-selection") { if (workspace._gridShown || gridScope) fileGrid.invertSelection() }
        else if (id === "grid-zoom-in") { if (workspace._gridShown || gridScope) photoGrid.zoomIn() }
        else if (id === "grid-zoom-out") { if (workspace._gridShown || gridScope) photoGrid.zoomOut() }
        else if (id === "grid-zoom-reset") { if (workspace._gridShown || gridScope) photoGrid.resetZoom() }
        else if (id === "quit") close()
    }
    onActiveChanged: if (!active) canvas.endLoupe()
    onModalOpenChanged: if (modalOpen) canvas.endLoupe()
    Connections {
        target: viewer
        function onViewCommand(id) { app.handleViewCommand(id) }
        function onTreeShown() { app.hudVisible = true; hideHud.restart(); Qt.callLater(treePanel.focusList) }
    }
    Instantiator {
        model: viewer.commands
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.binding
            context: Qt.WindowShortcut
            autoRepeat: false
            enabled: !app.modalOpen && !app.editingText && modelData.binding.length > 0 && (!app.gridScope || app.gridAllowsCommand(modelData.id))
            onActivated: app.invoke(modelData.id)
        }
    }
    Shortcut {
        sequence: "Escape"
        // Text fields own Escape (the address field reverts, then leaves arrangement itself).
        enabled: !app.modalOpen && !app.editingText
        onActivated: {
            canvas.endLoupe()
            if(app.layoutEditing) { app.layoutEditing=false; return }
            if (app.gridMode) app.showCanvas()
            else if (app.combinedMode && app.focusedPane === "preview" && app.visibility !== Window.FullScreen) app.focusPane("grid")
            else if (app.visibility === Window.FullScreen) app.showNormal()
            else app.wakeHud()
        }
    }
    Timer {
        id: hideHud
        interval: 2400
        running: true
        onTriggered: {
            if (!app.layoutEditing && !app.gridMode && !app.combinedMode && viewer.fileCount > 0 && !app.modalOpen && !topBar.hovered && !bottomHover.hovered && !app.focusWithin(topPanel) && !app.focusWithin(bottomPanel) && !filmstripPanel.keepOpen && !treePanel.keepOpen)
                app.hudVisible = false
            else restart()
        }
    }
    component BarLabel: Label {
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        font.features: { "tnum": 1 }
        verticalAlignment: Text.AlignVCenter
    }
    // Window controls for the application-drawn title bar (frameless or full screen). Glyphs follow the active state
    // like a native caption: text when the window is active, muted when it is not.
    component WindowControl: UiButton {
        iconOnly: true
        foreground: dangerFilled ? Theme.background : (app.active ? Theme.text : Theme.muted)
    }
    // The application owns the title bar when there is no system one: frameless mode, or full screen.
    readonly property bool ownsTitleBar: frameless || visibility === Window.FullScreen
    function toggleMaximized() { if (visibility === Window.Maximized) showNormal(); else showMaximized() }
    component ModeMenuItem: MenuItem { // compact content-mode menu row: check, icon, name and its shortcut
        id: modeItem
        required property var mode
        readonly property bool current: paneLayout.contentMode === mode.id
        objectName: "contentModeItem-" + mode.id
        implicitHeight: Theme.controlHeight
        leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
        text: mode.tip
        indicator: null
        Accessible.role: Accessible.MenuItem
        Accessible.checked: current
        onTriggered: app.invoke(mode.command)
        contentItem: RowLayout {
            spacing: Theme.gapSm
            UiIcon { name: "check"; color: Theme.primary; opacity: modeItem.current ? 1 : 0 }
            UiIcon { name: modeItem.mode.icon; color: Theme.text }
            Label { Layout.fillWidth: true; text: modeItem.mode.tip; color: Theme.text; font.pixelSize: Theme.textBody; elide: Text.ElideRight }
            Label {
                visible: text.length > 0
                text: (viewer.commands, viewer.bindingFor(modeItem.mode.command)) // re-read when bindings change
                color: Theme.muted
                font.family: Theme.monoFamily
                font.pixelSize: Theme.textSmall
            }
        }
        background: Rectangle { radius: Theme.radiusSmall; color: modeItem.highlighted || modeItem.hovered ? Theme.hover : "transparent" }
    }
    component BarDivider: Rectangle {
        implicitWidth: Theme.borderWidth
        implicitHeight: Theme.iconSize + Theme.gapXs
        color: Theme.border
        Layout.leftMargin: Theme.gapXs
        Layout.rightMargin: Theme.gapXs
    }

    // A pinned folder panel makes room instead of covering the photo, except in narrow windows.
    readonly property int treeInset: treePanel.visible && viewer.treePinned && app.width >= 720 ? treePanel.width : 0
    // Width of the canvas hidden by the folder panel, pinned or overlaid; centred overlays avoid it.
    readonly property int canvasCover: treePanel.visible ? treePanel.width : 0
    ImageCanvas {
        id: canvas
        objectName: "canvas"
        x: app.paneRect("preview").x; y: app.paneRect("preview").y
        width: app.paneRect("preview").width; height: app.paneRect("preview").height
        image: viewer.image
        background: "#191919" // photo backdrop, deliberately independent of the interface palette
        focus: !app.gridMode
        visible: app.workspace._previewShown
        // Keyboard route into the auto-hiding filmstrip: Tab wakes the HUD and focuses the current photo's cell.
        // Shift+Tab: keyboard route into the folder tree (shows it when hidden).
        Keys.onBacktabPressed: event => {
            if (!viewer.treeVisible) viewer.treeVisible = true
            else { app.hudVisible = true; hideHud.restart(); Qt.callLater(treePanel.focusList) }
            event.accepted = true
        }
        // Combined view: F6 returns to the grid; Tab continues to the split handle (its keyboard resize), then the grid.
        Keys.onPressed: event => { if (app.combinedMode && event.key === Qt.Key_F6) { app.focusPane("grid"); event.accepted = true } }
        Keys.onTabPressed: event => {
            if (app.combinedMode) {
                if (splitHandle.visible) splitHandle.forceActiveFocus(Qt.TabFocusReason); else app.focusPane("grid")
                event.accepted = true; return
            }
            if (viewer.filmstripVisible && filmstrip.count > 0) {
                app.hudVisible = true; hideHud.restart()
                Qt.callLater(filmstripPanel.focusCurrent)
                event.accepted = true
            }
        }
        Accessible.name: "Photo canvas"
        Accessible.description: viewer.fileName.length ? viewer.fileName + ", " + viewer.imageWidth + " by " + viewer.imageHeight + " pixels" : "Open or drop a photo to begin"
    }
    MouseArea {
        id: interaction
        visible: app.workspace._previewShown
        anchors.fill: canvas
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        property real lastX: 0
        property real lastY: 0
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPositionChanged: mouse => {
            app.pointerX = mouse.x; app.pointerY = mouse.y
            if (pressed && !(pressedButtons & Qt.RightButton)) canvas.panBy(mouse.x-lastX, mouse.y-lastY)
            lastX=mouse.x; lastY=mouse.y
            app.wakeHud()
        }
        onPressed: mouse => {
            canvas.forceActiveFocus(); lastX=mouse.x; lastY=mouse.y
            if (mouse.button === Qt.RightButton) canvas.beginLoupe(mouse.x,mouse.y)
        }
        onReleased: canvas.endLoupe()
        onCanceled: canvas.endLoupe()
        onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) app.invoke("fullscreen") }
        onWheel: wheel => {
            app.wakeHud()
            let delta=wheel.angleDelta.y || wheel.angleDelta.x
            let pixel=wheel.pixelDelta.y || wheel.pixelDelta.x
            if ((wheel.modifiers & Qt.ControlModifier) || (!(wheel.modifiers & Qt.ShiftModifier) && viewer.wheelMode === "zoom")) {
                canvas.zoomAt(Math.pow(1.0015, pixel !== 0 ? pixel*3 : delta), wheel.x, wheel.y)
            } else if ((wheel.modifiers & Qt.ShiftModifier) || viewer.wheelMode === "pan") {
                let amount=pixel !== 0 ? pixel : delta/3
                canvas.panBy((wheel.modifiers & Qt.ShiftModifier) ? amount : wheel.pixelDelta.x, (wheel.modifiers & Qt.ShiftModifier) ? 0 : amount)
            } else {
                app.wheelRemainder += pixel !== 0 ? pixel*2 : delta
                if (Math.abs(app.wheelRemainder) >= 120) app.previewFollowsCursor = false
                while (app.wheelRemainder >= 120) { viewer.previous(); app.wheelRemainder-=120 }
                while (app.wheelRemainder <= -120) { viewer.next(); app.wheelRemainder+=120 }
            }
            wheel.accepted=true
        }
    }
    PhotoGrid {
        id: photoGrid
        objectName: "photoGrid"
        x: app.paneRect("grid").x; y: app.paneRect("grid").y
        width: app.paneRect("grid").width; height: app.paneRect("grid").height
        visible: app.workspace._gridShown
        inputEnabled: !app.modalOpen
        // Combined view: Enter on a photo moves keys to the preview and stays combined; Escape does the same.
        onItemActivated: { previewDebounce.stop(); if (app.combinedMode) app.focusPane("preview"); else app.showCanvas() }
        onDismissed: { if(app.layoutEditing)app.layoutEditing=false;else if(app.combinedMode)app.focusPane("preview");else app.showCanvas() }
        onFoldersRequested: { if (!viewer.treeVisible) viewer.treeVisible = true; else treePanel.focusList() }
        tabHandler: app.combinedMode ? (() => app.focusPane("preview")) : null
        Keys.onPressed: event => { if (app.combinedMode && event.key === Qt.Key_F6) { app.focusPane("preview"); event.accepted = true } }
        deleteBlocked: deleteFlow.isReadOnly(fileGrid.directory)
        onDeleteRequested: paths => deleteFlow.request(paths, "grid", fileGrid.directory)
    }
    // ---- GRID-PREVIEW-01 combined view: split handle, explicit preview states, pane focus rings and notices.
    Item {
        id: splitHandle
        objectName: "contentSplitHandle"
        readonly property var split: app.workspace._split
        readonly property bool horizontal: split.orientation === "horizontal"
        // The split is geometry: locked layouts and frozen center panes keep it; display fallbacks never persist it.
        readonly property bool editable: !paneLayout.locked && !paneLayout.panes.preview.frozen && !paneLayout.panes.grid.frozen && split.fallback === ""
        readonly property real ratio: split.gridShare // the grid's share of the split, as shown
        // Pointer and arrow keys move the divider; converted to the grid's share whichever side the grid takes.
        function shareAt(leadShare) { return split.gridFirst ? leadShare : 1 - leadShare }
        x: split.handle.x; y: split.handle.y; width: split.handle.width; height: split.handle.height
        visible: app.combinedMode && split.orientation.length > 0
        z: 6
        Accessible.role: Accessible.Separator
        Accessible.name: "Resize grid and photo"
        Accessible.description: editable ? (horizontal ? "Left and Right arrows" : "Up and Down arrows") + " move the divider. Home centres it." : "The layout is locked or the window is too small to resize the split."
        function commit(value) {
            app.splitDrag = -1
            const next = Math.round(Math.max(0.1, Math.min(0.9, value)) * 1000) / 1000
            if (!paneLayout.setCenterRatio(next)) app.showSplitMessage(paneLayout.feedback)
        }
        Keys.onShortcutOverride: event => {
            if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_Escape].indexOf(event.key) >= 0) event.accepted = true
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) { if (splitMouse.pressed) app.splitDrag = -1; else app.focusPane(app.focusedPane); event.accepted = true; return }
            if (event.key === Qt.Key_F6) { app.focusPane(app.focusedPane === "grid" ? "preview" : "grid"); event.accepted = true; return }
            const step = { [Qt.Key_Left]: -0.05, [Qt.Key_Up]: -0.05, [Qt.Key_Right]: 0.05, [Qt.Key_Down]: 0.05 }[event.key]
            if (event.key === Qt.Key_Home) { commit(0.5); event.accepted = true }
            else if (step !== undefined) { commit(ratio + (split.gridFirst ? step : -step)); event.accepted = true }
        }
        Keys.onTabPressed: event => { app.focusPane("grid"); event.accepted = true }
        Keys.onBacktabPressed: event => { app.focusPane("preview"); event.accepted = true }
        Rectangle { // the divider itself; a grip and the focus ring appear when it can be used
            x: splitHandle.horizontal ? (parent.width - width) / 2 : 0
            y: splitHandle.horizontal ? 0 : (parent.height - height) / 2
            width: splitHandle.horizontal ? Theme.borderWidth : parent.width
            height: splitHandle.horizontal ? parent.height : Theme.borderWidth
            color: splitMouse.pressed || splitHandle.activeFocus ? Theme.primary : Theme.border
        }
        Rectangle {
            anchors.centerIn: parent
            visible: splitHandle.editable && (splitMouse.containsMouse || splitMouse.pressed || splitHandle.activeFocus)
            // Keyboard focus enlarges the grip and draws it in the focus colour so the active divider is unmistakable.
            readonly property int length: splitHandle.activeFocus ? 3 * Theme.iconSize : 2 * Theme.iconSize
            readonly property int thickness: splitHandle.activeFocus ? Theme.gapXs + Theme.gapXxs : Theme.gapXs
            width: splitHandle.horizontal ? thickness : length
            height: splitHandle.horizontal ? length : thickness
            radius: thickness / 2
            color: splitHandle.activeFocus ? Theme.focusRing : splitMouse.pressed ? Theme.primary : Theme.muted
        }
        MouseArea {
            id: splitMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: splitHandle.editable
            cursorShape: splitHandle.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
            property bool moved: false
            onPressed: moved = false
            onPositionChanged: mouse => {
                if (!pressed) return
                const point = mapToItem(null, mouse.x, mouse.y), split = splitHandle.split
                moved = true
                app.splitDrag = Math.max(0.1, Math.min(0.9, splitHandle.shareAt(((splitHandle.horizontal ? point.x : point.y) - split.origin) / Math.max(1, split.total))))
            }
            // Persisted once, on release, as the ratio actually shown (minimum extents may have clamped it).
            onReleased: { if (moved && app.splitDrag >= 0) splitHandle.commit(splitHandle.ratio); else app.splitDrag = -1 }
            onCanceled: app.splitDrag = -1
            onDoubleClicked: splitHandle.commit(0.5)
        }
        ToolTip.visible: splitMouse.containsMouse && !splitMouse.pressed
        ToolTip.delay: 600
        ToolTip.text: editable ? "Drag to resize. Double-click to centre." : paneLayout.locked ? "Unlock the layout to resize the split" : "Unfreeze the grid and photo panes to resize the split"
    }
    Rectangle { // explicit preview states: folder, unsupported file, no selection, empty/inaccessible/no folder, failure
        id: previewStatePanel
        objectName: "previewState"
        readonly property string stateName: app.previewState
        readonly property string subjectName: app.leafName(app.previewSubject)
        readonly property string extension: {
            const dot = subjectName.lastIndexOf(".")
            return dot > 0 && dot < subjectName.length - 1 ? subjectName.slice(dot + 1).toUpperCase().slice(0, 5) : ""
        }
        x: app.paneRect("preview").x; y: app.paneRect("preview").y
        width: app.paneRect("preview").width; height: app.paneRect("preview").height
        visible: app.combinedMode && app.workspace._previewShown && stateName.length > 0 && stateName !== "loading"
        color: Theme.background
        z: 1
        Accessible.role: Accessible.StaticText
        Accessible.name: stateTitle.text + (stateDetail.text.length ? ". " + stateDetail.text : "")
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton; onPressed: app.focusPane("preview") }
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.max(0, Math.min(360, parent.width - 2 * Theme.gapXl))
            spacing: Theme.gapSm
            Column {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.gapXs
                UiIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    readonly property int glyph: previewStatePanel.height < 260 ? 32 : 48
                    name: ({ folder: "folder", unsupported: "file", failed: "file", inaccessible: "lock", empty: "folder",
                             "loading-folder": "spinner" })[previewStatePanel.stateName] || "image"
                    size: glyph
                    lineWidth: 1.5 * 22 / glyph
                    color: previewStatePanel.stateName === "folder" ? Theme.text : Theme.muted
                }
                Label {
                    objectName: "previewExtension"
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: previewStatePanel.stateName === "unsupported" && previewStatePanel.extension.length > 0
                    text: previewStatePanel.extension
                    color: Theme.muted
                    font.pixelSize: Theme.textCaption
                    font.weight: Font.Medium
                    font.letterSpacing: 0.4
                }
            }
            Label {
                id: stateTitle
                objectName: "previewStateTitle"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                text: ({ folder: previewStatePanel.subjectName, unsupported: previewStatePanel.subjectName, failed: previewStatePanel.subjectName,
                         none: "Nothing selected", empty: "Nothing to preview", inaccessible: "Nothing to preview",
                         "loading-folder": "Loading folder…", "no-folder": "Nothing to preview" })[previewStatePanel.stateName] || ""
                color: previewStatePanel.stateName === "no-folder" ? Theme.muted : Theme.text
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
                Accessible.role: Accessible.Heading
            }
            Label {
                id: stateDetail
                objectName: "previewStateDetail"
                Layout.fillWidth: true
                visible: text.length > 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                lineHeight: 1.4
                text: ({ folder: "Folder", unsupported: "Glancelot can't preview this file type",
                         failed: viewer.status.length && !viewer.loading ? viewer.status : "This photo could not be shown.",
                         none: "Select a photo in the grid to preview it here.", empty: "This folder is empty.",
                         inaccessible: "This folder can't be opened." })[previewStatePanel.stateName] || ""
                color: Theme.muted
                font.pixelSize: Theme.textSmall
            }
            Label { // the one action a folder offers, worded as its key
                objectName: "previewStateHint"
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.gapXs
                visible: previewStatePanel.stateName === "folder"
                text: "Press Enter to open"
                leftPadding: Theme.gapSm; rightPadding: Theme.gapSm; topPadding: Theme.gapXxs; bottomPadding: Theme.gapXxs
                color: Theme.text
                font.pixelSize: Theme.textSmall
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.width: Theme.borderWidth; border.color: Theme.border }
            }
        }
    }
    Rectangle { // a pending photo keeps the previous one only behind a delayed scrim, so quick steps never flash
        objectName: "previewLoading"
        x: app.paneRect("preview").x; y: app.paneRect("preview").y
        width: app.paneRect("preview").width; height: app.paneRect("preview").height
        visible: app.combinedMode && app.workspace._previewShown && app.previewState === "loading" && app.previewLoadingShown
        color: Qt.alpha(Theme.background, viewer.imageWidth > 0 ? 0.55 : 1)
        z: 1
        Rectangle {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: Theme.gapLg }
            width: Math.min(parent.width - 2 * Theme.gapMd, loadingRow.implicitWidth + 2 * Theme.gapMd)
            height: Theme.controlHeight
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: Theme.borderWidth
            border.color: Theme.border
            RowLayout {
                id: loadingRow
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 2 * Theme.gapMd)
                spacing: Theme.gapSm
                UiIcon { name: "spinner"; color: Theme.muted }
                Label {
                    Layout.fillWidth: true
                    text: "Loading " + app.leafName(app.previewSubject) + "…"
                    elide: Text.ElideMiddle
                    color: Theme.text
                    font.pixelSize: Theme.textSmall
                    Accessible.role: Accessible.AlertMessage
                }
            }
        }
    }
    Repeater { // the pane that owns keys in the combined view carries the focus ring
        model: ["grid", "preview"]
        delegate: Rectangle {
            required property string modelData
            objectName: "paneFocusRing-" + modelData
            readonly property var rect: app.paneRect(modelData)
            x: rect.x + 1; y: rect.y + 1; width: Math.max(0, rect.width - 2); height: Math.max(0, rect.height - 2)
            visible: app.combinedMode && app.focusedPane === modelData && rect.width > 0
                     && (modelData === "grid" ? app.focusWithin(photoGrid) : app.activeFocusItem === canvas)
            color: "transparent"
            border.width: Theme.focusWidth
            border.color: Theme.focusRing
            z: 5
        }
    }
    Label {
        objectName: "combinedNotice"
        readonly property var split: app.workspace._split
        readonly property var area: app.workspace._content
        // A fallback is announced when it starts or the shown pane changes, then gets out of the way.
        readonly property string fallbackKey: app.combinedMode && split.fallback.length ? split.fallback + "/" + split.orientation + "/" + app.focusedPane : ""
        onFallbackKeyChanged: if (fallbackKey.length) fallbackNoticeTimer.restart(); else fallbackNoticeTimer.stop()
        Timer { id: fallbackNoticeTimer; interval: 5000 }
        text: app.splitMessage.length ? app.splitMessage
            : !fallbackNoticeTimer.running ? ""
            : split.fallback === "single" ? (area.width < 320 ? "Showing the " + (app.focusedPane === "grid" ? "grid" : "photo") + ". Tab switches."
                : "Grid and photo don't both fit this window. Showing the " + (app.focusedPane === "grid" ? "grid" : "photo") + "; press Tab to switch.")
            : split.fallback === "reoriented" ? (split.orientation === "vertical" ? "Stacked to fit this window." : "Placed side by side to fit this window.")
            : ""
        visible: text.length > 0 && !app.layoutEditing && !app.modalOpen
        x: area.x + (area.width - width) / 2
        y: area.y + area.height - height - Theme.gapLg
        width: Math.max(0, Math.min(implicitWidth, area.width - 2 * Theme.gapMd))
        wrapMode: Text.Wrap
        color: Theme.text
        font.pixelSize: Theme.textSmall
        padding: Theme.gapSm
        leftPadding: Theme.gapMd; rightPadding: Theme.gapMd
        background: Rectangle { color: Theme.surface; radius: Theme.radiusCard; border.width: Theme.borderWidth; border.color: Theme.border }
        z: 15
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
    // ---- end GRID-PREVIEW-01 combined view
    Label {
        objectName: "layoutRecoveryNotice"
        visible: app.workspace._recovered
        x: app.workspace._clear.x+Theme.gapSm
        y: app.workspace._clear.y+Theme.gapSm
        width: Math.max(0,app.workspace._clear.width-2*Theme.gapSm)
        text: app.workspace._clear.width < 280 ? "Layout adapted. Reset in Settings." : "Layout adapted to fit this window. Saved preferences are kept. Change or reset it in Settings → Browsing."
        wrapMode: Text.Wrap
        color: Theme.text
        padding: Theme.gapSm
        background: Rectangle { color: Theme.surface;radius: Theme.radiusControl }
        z: 15
        Accessible.name: text
    }
    PaneArrangement { anchors.fill: parent; host: app; visible: app.layoutEditing; z: 20 }
    DropArea {
        anchors.fill: parent
        onEntered: drag => { drag.accepted = drag.hasUrls }
        onDropped: drop => { if (drop.hasUrls && drop.urls.length) { viewer.open(drop.urls[0].toString()); app.showCanvas(); drop.acceptProposedAction() } }
        Rectangle { anchors.fill: parent; visible: parent.containsDrag; color: Qt.alpha(Theme.primary, 0.15); border.width: Theme.focusWidth; border.color: Theme.primary }
    }
    Rectangle { // with no photo there is nothing to protect, so the empty state sits on the interface background
        anchors.fill: canvas
        visible: emptyState.visible
        color: Theme.background
    }
    ColumnLayout { // empty state: one message, one action
        id: emptyState
        readonly property bool compactContent: app.workspace._content.width < 280 || app.workspace._content.height < 320
        x: app.workspace._content.x+(app.workspace._content.width-width)/2
        y: app.workspace._content.y+(app.workspace._content.height-height)/2
        spacing: Theme.gapSm
        visible: !app.gridMode && !app.combinedMode && viewer.fileCount === 0 && !viewer.loading
        width: Math.max(0,Math.min(440,app.workspace._content.width-2*Theme.gapMd))
        Image {
            visible: !emptyState.compactContent
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: app.height < 480 ? 96 : 144
            Layout.preferredHeight: Layout.preferredWidth
            source: "qrc:/brand/mascots/transparent/01_mascot_standing.png"
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 288; sourceSize.height: 288
            Accessible.ignored: true
        }
        Label {
            visible: !emptyState.compactContent
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.gapSm
            text: "Glancelot"
            color: Theme.text
            font.pixelSize: Theme.textDisplay
            font.weight: Font.Light
            font.letterSpacing: -0.02 * Theme.textDisplay
            lineHeight: 1.1
            Accessible.role: Accessible.Heading
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: !emptyState.compactContent
            text: "Your photographs, front and center."
            color: Theme.muted
            font.pixelSize: Theme.textTitle
            wrapMode: Text.Wrap
        }
        UiButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: emptyState.compactContent ? 0 : Theme.gapLg
            Layout.maximumWidth: emptyState.width
            Layout.minimumWidth: 0
            variant: "primary"
            text: emptyState.compactContent ? "Open" : "Open a photo"
            Accessible.name: "Open a photo"
            tip: "Open a photo" + app.shortcutHint("open")
            onClicked: app.invoke("open")
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Theme.gapSm
            horizontalAlignment: Text.AlignHCenter
            visible: !emptyState.compactContent
            text: "or drop a JPEG or PNG here. Arrow keys browse the folder."
            color: Theme.muted
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            lineHeight: 1.4
        }
    }
    Rectangle {
        id: topPanel
        objectName: "topBar"
        x: app.paneRect("topbar").x; y: app.paneRect("topbar").y
        width: app.paneRect("topbar").width; height: app.paneRect("topbar").height
        color: Theme.surface
        visible: width > 0 && height > 0
        HoverHandler { id: topBar }
        // A horizontal bar collapses optional items by measured fit rather than fixed window widths, so the window
        // controls and a minimum title (the frameless drag target) always keep their room. Yield order: the lock
        // label first, then "Show frame", then Sort. Implicit widths do not depend on visibility, so no binding loop.
        readonly property bool horizontalBar: barGridtopbar.columns !== 1
        readonly property real titleMinimum: 72
        readonly property real modeSpace: {
            const gap = barGridtopbar.columnSpacing
            let need = 2 * Theme.gapMd + brandMark.Layout.preferredWidth + brandMark.Layout.rightMargin + gap
            for (const item of [openButton, foldersButton, settingsButton])
                need += item.implicitWidth + gap
            const divider = Theme.borderWidth + 2 * Theme.gapXs + gap // BarDivider carries its own side margins
            need += divider + titleMinimum + gap
            if (exitFullScreenButton.visible) need += exitFullScreenButton.implicitWidth + gap
            if (app.ownsTitleBar) {
                need += divider + minimizeButton.implicitWidth + gap + closeButton.implicitWidth + gap
                if (maximizeButton.visible) need += maximizeButton.implicitWidth + gap
            }
            if (paneLayout.locked) need += Theme.controlHeight + gap
            return topbarScroll.availableWidth - need
        }
        readonly property real fitBudget: modeSpace - (app.modeCompact ? modeMenuButton.implicitWidth : modeSelector.implicitWidth) - barGridtopbar.columnSpacing
        readonly property bool fitSort: !horizontalBar || fitBudget >= sortButton.implicitWidth + barGridtopbar.columnSpacing
        readonly property real afterSort: fitBudget - (fitSort ? sortButton.implicitWidth + barGridtopbar.columnSpacing : 0)
        readonly property bool fitShowFrame: !horizontalBar || afterSort >= showFrameButton.implicitWidth + barGridtopbar.columnSpacing
        readonly property real afterShowFrame: afterSort - (app.frameless && fitShowFrame ? showFrameButton.implicitWidth + barGridtopbar.columnSpacing : 0)
        readonly property bool fitLockLabel: !horizontalBar || afterShowFrame >= lockLabelMeasure.implicitWidth - Theme.controlHeight
        UiButton { id: lockLabelMeasure; visible: false; iconName: "lock"; text: "Layout locked" } // width of the labelled lock
        Rectangle { // edge against the canvas when no filmstrip sits below
            objectName: "topBarEdge"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: Theme.borderWidth
            color: Theme.border
            visible: !filmstripPanel.visible
        }
        ScrollView {
            id: topbarScroll
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth
            contentHeight: barGridtopbar.implicitHeight
            ScrollBar.vertical: UiScrollBar {}
        Item { // in the frameless mode, empty bar space is the title bar: drag moves, double-click maximizes
            objectName: "titleBarDragArea"
            width: topbarScroll.availableWidth
            height: Math.max(topbarScroll.availableHeight, barGridtopbar.implicitHeight)
            enabled: app.frameless && app.visibility !== Window.FullScreen // full screen has no title bar to move
            DragHandler { target: null; onActiveChanged: if (active) app.startSystemMove() }
            TapHandler { onDoubleTapped: app.toggleMaximized() }
        }
        GridLayout {
            id: barGridtopbar
            columns: app.paneState("topbar").region === "left" || app.paneState("topbar").region === "right" ? 1 : children.length
            rowSpacing: Theme.gapXs; columnSpacing: Theme.gapXs
            width: topbarScroll.availableWidth-2*Theme.gapMd
            x: Theme.gapMd
            y: Math.max(0,(topbarScroll.availableHeight-implicitHeight)/2)
            Image {
                id: brandMark
                objectName: "brandMark"
                Layout.preferredWidth: 24; Layout.preferredHeight: 24
                Layout.rightMargin: Theme.gapXs
                source: "qrc:/brand/icons/app/" + (Theme.dark ? "dark" : "light") + "/glancelot-64.png"
                sourceSize.width: 48; sourceSize.height: 48
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
            }
            UiButton { id: openButton; text: "Open"; tip: "Open a photo" + app.shortcutHint("open"); onClicked: app.invoke("open") }
            UiButton {
                id: foldersButton
                objectName: "foldersToggle"
                text: "Folders"
                checkable: true
                checked: viewer.treeVisible
                tip: (viewer.treeVisible ? "Hide the folder tree" : "Show the folder tree") + app.shortcutHint("tree")
                Accessible.description: viewer.treeVisible ? "Hide the folder tree" : "Show the folder tree"
                onClicked: { checked = Qt.binding(() => viewer.treeVisible); app.invoke("tree") }
            }
            Rectangle { // content mode: one segmented choice (GRID-PREVIEW-01); a menu in narrow windows
                id: modeSelector
                objectName: "contentModeSegments"
                visible: !app.modeCompact
                implicitWidth: modeRow.implicitWidth + 2 * Theme.gapXxs
                implicitHeight: Theme.controlHeight
                radius: Theme.radiusControl
                color: "transparent"
                border.width: Theme.borderWidth
                border.color: Theme.border
                Accessible.role: Accessible.Grouping
                Accessible.name: "View"
                Row {
                    id: modeRow
                    x: Theme.gapXxs
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.gapXxs
                    Repeater {
                        model: app.contentModes
                        delegate: UiButton {
                            required property var modelData
                            objectName: "contentMode-" + modelData.id
                            implicitHeight: Theme.controlHeight - 2 * Theme.gapXxs
                            cornerRadius: Theme.radiusControl - Theme.gapXxs // concentric with the group outline
                            iconName: modelData.icon
                            text: modelData.label
                            selected: paneLayout.contentMode === modelData.id
                            tip: modelData.tip + app.shortcutHint(modelData.command)
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: selected
                            Accessible.name: modelData.tip
                            onClicked: app.invoke(modelData.command)
                        }
                    }
                }
            }
            UiButton {
                id: modeMenuButton
                objectName: "contentModeMenu"
                visible: app.modeCompact
                readonly property var current: app.contentModes.find(mode => mode.id === paneLayout.contentMode) || app.contentModes[0]
                text: current.label
                leftPadding: Theme.gapSm + Theme.gapXxs; rightPadding: Theme.gapSm + Theme.gapXxs
                tip: modeMenu.visible ? "" : "Change view: photo, grid or both"
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: "View: " + current.tip + ". Change view"
                onClicked: modeMenu.opened ? modeMenu.close() : modeMenu.open()
                Menu {
                    id: modeMenu
                    objectName: "contentModeMenuPopup"
                    y: parent.height + Theme.gapXs
                    margins: Theme.gapSm // kept inside narrow windows
                    padding: Theme.gapXs
                    background: Rectangle {
                        implicitWidth: 344 // the longest row (name and shortcut) never elides
                        radius: Theme.radiusControl
                        color: Theme.surface
                        border.width: Theme.borderWidth
                        border.color: Theme.border
                    }
                    ModeMenuItem { mode: app.contentModes[0] }
                    ModeMenuItem { mode: app.contentModes[1] }
                    ModeMenuItem { mode: app.contentModes[2] }
                }

            }
            UiButton {
                id: sortButton
                objectName: "sortButton"
                visible: topPanel.fitSort
                iconName: viewer.sortDescending ? "arrow-down" : "arrow-up"
                text: ({ name: "Name", modified: "Date", size: "Size", shuffle: "Shuffle" }[viewer.sortKey])
                enabled: viewer.fileCount > 0
                onClicked: app.invoke("sort")
                tip: "Cycle file order" + app.shortcutHint("sort") + (viewer.bindingFor("sort-reverse").length ? "; reverse with " + viewer.bindingFor("sort-reverse") : "")
                Accessible.name: "File order: " + ({ name: "name", modified: "date modified", size: "size", shuffle: "shuffle" }[viewer.sortKey]) + (viewer.sortDescending ? ", descending" : ", ascending")
                Accessible.description: "Changes the order of photos inside folders, not the folder order"
            }
            BarDivider { id: topBarDivider; objectName: "topBarDivider" }
            Label {
                objectName: "topBarTitle"
                text: viewer.fileName || "No photo open"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                // Like a native caption, an inactive window shows its title in the muted role (still >= 4.5:1).
                color: viewer.fileName && app.active ? Theme.text : Theme.muted
                font.pixelSize: Theme.textBody
            }
            UiButton { // lock state stays visible outside Settings; activating it unlocks
                objectName: "layoutLockIndicator"
                visible: paneLayout.locked
                iconName: "lock"
                // In a narrow horizontal bar the label yields so the window controls keep their room; the name stays
                // the tooltip and accessible name.
                iconOnly: !topPanel.fitLockLabel
                text: "Layout locked"
                tip: "Workspace layout is locked. Select to unlock" + app.shortcutHint("layout-lock")
                Accessible.name: "Workspace layout locked. Unlock layout"
                onClicked: app.invoke("layout-lock")
            }
            UiButton {
                id: settingsButton
                objectName: "settingsButton"
                text: "Settings"
                tip: "Settings, palettes and shortcuts" + app.shortcutHint("settings") + (viewer.bindingFor("palette").length ? ". Find a command: " + viewer.bindingFor("palette") : "")
                onClicked: app.invoke("settings")
            }
            UiButton { id: exitFullScreenButton; text: "Exit full screen"; visible: app.visibility === Window.FullScreen; tip: "Exit full screen" + app.shortcutHint("fullscreen"); onClicked: app.invoke("fullscreen") }
            // Window controls must never be pushed out of a narrow bar, so "Show frame" yields its room there; the
            // command stays in Settings and command search, with its shortcut.
            UiButton { id: showFrameButton; objectName: "showFrameButton"; text: "Show frame"; visible: app.frameless && topPanel.fitShowFrame; tip: "Restore the system title bar" + app.shortcutHint("frameless"); onClicked: app.invoke("frameless") }
            BarDivider { id: windowDivider; visible: app.ownsTitleBar }
            WindowControl { id: minimizeButton; objectName: "minimizeButton"; text: "Minimize"; iconName: "minimize"; visible: app.ownsTitleBar; onClicked: app.showMinimized() }
            WindowControl {
                id: maximizeButton
                objectName: "maximizeButton"
                readonly property bool maximized: app.visibility === Window.Maximized
                text: maximized ? "Restore" : "Maximize"
                iconName: maximized ? "restore" : "maximize"
                visible: app.frameless && app.visibility !== Window.FullScreen
                onClicked: app.toggleMaximized()
            }
            WindowControl { id: closeButton; objectName: "closeButton"; variant: "danger-quiet"; text: "Close window"; iconName: "close"; visible: app.ownsTitleBar; onClicked: app.close() }
        }
        }
    }
    Filmstrip {
        id: filmstripPanel
        objectName: "filmstrip"
        compact: app.height < 480
        x: app.paneRect("filmstrip").x; y: app.paneRect("filmstrip").y
        width: app.paneRect("filmstrip").width; height: app.paneRect("filmstrip").height
        vertical: app.paneState("filmstrip").region === "left" || app.paneState("filmstrip").region === "right"
        visible: width > 0 && height > 0
        onItemActivated: canvas.forceActiveFocus()
    }
    FolderTree {
        id: treePanel
        objectName: "folderTree"
        compact: app.width < 720 || app.height < 480
        x: app.paneRect("tree").x; y: app.paneRect("tree").y
        width: app.paneRect("tree").width; height: app.paneRect("tree").height
        maximumWidth: Math.max(200, Math.round(app.width * 0.45))
        layoutFrozen: true
        arrangementMode: app.layoutEditing
        onResizeCommitted: extent => paneLayout.resize("tree", extent)
        visible: width > 0 && height > 0
        onDismissed: { if(app.layoutEditing)app.layoutEditing=false;app.focusContent() }
        // Hiding a focused panel must not strand keyboard input on an invisible item.
        onVisibleChanged: if (!visible && !app.modalOpen) app.focusContent()
        onDeleteRequested: path => deleteFlow.request([path], "tree", "")
    }
    DeleteFlow { // DELETE-01 confirmation, progress, notice and results
        id: deleteFlow
        objectName: "deleteFlow"
        host: app
        anchors.fill: parent
        z: 25
        onFlowClosed: if (!app.modalOpen) Qt.callLater(app.focusContent)
    }
    Rectangle {
        id: bottomPanel
        x: app.paneRect("statusbar").x; y: app.paneRect("statusbar").y
        width: app.paneRect("statusbar").width; height: app.paneRect("statusbar").height
        visible: width > 0 && height > 0
        color: Theme.surface
        HoverHandler { id: bottomHover }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.borderWidth
            color: Theme.border
        }
        ScrollView {
            id: statusbarScroll
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth
            contentHeight: barGridstatusbar.implicitHeight
            ScrollBar.vertical: UiScrollBar {}
        GridLayout {
            id: barGridstatusbar
            columns: app.paneState("statusbar").region === "left" || app.paneState("statusbar").region === "right" ? 1 : children.length
            rowSpacing: Theme.gapXs; columnSpacing: Theme.gapXs
            width: statusbarScroll.availableWidth-2*Theme.gapMd
            x: Theme.gapMd
            y: Math.max(0,(statusbarScroll.availableHeight-implicitHeight)/2)
            UiButton { text: "Previous"; visible: !app.gridMode && !app.combinedMode; enabled: viewer.fileCount > 0; tip: "Previous photo" + app.shortcutHint("previous"); onClicked: app.invoke("previous") }
            BarLabel {
                visible: !app.gridMode && !app.combinedMode
                text: viewer.fileCount > 0 ? viewer.currentIndex + " of " + viewer.fileCount : "No photo"
                Layout.leftMargin: Theme.gapXs; Layout.rightMargin: Theme.gapXs
            }
            UiButton { text: "Next"; visible: !app.gridMode && !app.combinedMode; enabled: viewer.fileCount > 0; tip: "Next photo" + app.shortcutHint("next"); onClicked: app.invoke("next") }
            BarLabel {
                visible: app.workspace._gridShown
                font.features: { "tnum": 1 }
                text: photoGrid.selectedCount > 1 ? photoGrid.selectedCount + " selected"
                    : photoGrid.selectedIndex >= 0 ? (photoGrid.selectedIndex + 1) + " of " + fileGrid.count : "No selection"
                Layout.leftMargin: Theme.gapXs
            }
            BarLabel {
                visible: app.workspace._gridShown && app.width > 680
                Layout.maximumWidth: Math.max(120, app.width - (app.combinedMode ? 760 : 520))
                Layout.leftMargin: Theme.gapMd
                elide: Text.ElideRight
                text: viewer.status.length > 0 ? viewer.status : "Arrow keys move · Shift or Ctrl selects · Enter opens · Backspace goes up"
                color: viewer.status.length > 0 ? Theme.text : Theme.muted
            }
            Item { Layout.fillWidth: true }
            UiButton { visible: app.gridMode; text: "Back to photo"; tip: "Back to photo (Esc)"; onClicked: app.showCanvas() }
            UiButton { objectName: "gridOpenSelected"; visible: app.workspace._gridShown; variant: "primary"; text: fileGrid.kindAt(photoGrid.selectedIndex) === "folder" ? "Open folder" : "Open"; enabled: photoGrid.selectedIndex >= 0 && fileGrid.kindAt(photoGrid.selectedIndex) !== "file"; tip: "Open the item under the cursor (Enter)"; onClicked: photoGrid.activate() }
            BarLabel { visible: !app.gridMode && app.width > 780 && viewer.imageWidth > 0; text: viewer.imageWidth + " × " + viewer.imageHeight; Layout.rightMargin: Theme.gapSm }
            UiButton {
                visible: !app.gridMode
                text: Math.round(canvas.zoom*100) + "%"
                font.features: { "tnum": 1 }
                enabled: viewer.imageWidth > 0
                onClicked: app.invoke("actual")
                tip: "Actual size: one image pixel per display pixel" + app.shortcutHint("actual")
                Accessible.name: "Zoom " + Math.round(canvas.zoom*100) + " percent. Show actual size"
            }
            UiButton { text: "Fit"; visible: !app.gridMode; enabled: viewer.imageWidth > 0; onClicked: app.invoke("fit"); tip: "Fit without enlarging" + app.shortcutHint("fit") }
            UiButton { text: "Rotate"; visible: !app.gridMode && app.width > 680; enabled: viewer.imageWidth > 0; onClicked: app.invoke("rotate"); tip: "Rotate the view clockwise" + app.shortcutHint("rotate") }
        }
        }
    }
    Rectangle { // status toast, centred on the part of the canvas no panel covers
        objectName: "statusToast"
        // In the combined view only a renderer failure shows here, over the preview; the status bar carries the rest.
        readonly property var area: app.combinedMode ? app.paneRect("preview") : app.workspace._clear
        x: area.x+(area.width-width)/2
        y: area.y+area.height-height-Theme.gapLg
        width: Math.max(0,Math.min(area.width-2*Theme.gapMd,statusText.implicitWidth+2*Theme.gapLg))
        height: statusText.implicitHeight + 2 * Theme.gapSm + Theme.gapXxs
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: canvas.rendererError ? Theme.danger : Theme.border
        // Only the controller's default "how to begin" hint is suppressed while the empty state says the same thing.
        // Every other status, including settings-load errors present at start-up, still shows (tested).
        readonly property bool defaultHint: viewer.status === "Open a JPEG or PNG to begin."
        visible: (app.combinedMode && app.workspace._previewShown && canvas.rendererError.length > 0) || !app.gridMode && !app.combinedMode && (viewer.loading || canvas.rendererError.length > 0
                 || (viewer.status.length > 0 && app.hudVisible && !(emptyState.visible && defaultHint)))
        Label {
            id: statusText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 2 * Theme.gapLg)
            text: canvas.rendererError || viewer.status
            color: canvas.rendererError ? Theme.danger : Theme.text
            wrapMode: Text.Wrap
            font.pixelSize: Theme.textSmall
            Accessible.role: Accessible.AlertMessage
        }
    }
    FileDialog {
        id: openDialog
        title: "Open a photo"
        nameFilters: ["Photographs (*.jpg *.jpeg *.png *.JPG *.JPEG *.PNG)", "All files (*)"]
        onAccepted: { viewer.open(selectedFile.toString()); app.showCanvas() }
        onRejected: app.focusContent()
    }
    SettingsDialog {
        id: settingsDialog
        host: app
        photoCanvas: canvas
        anchors.centerIn: parent
        width: Math.min(800, app.width - 2 * Theme.gapLg)
        height: Math.min(640, app.height - 2 * Theme.gapLg)
        onClosed: app.focusContent()
    }
    // Native system resizing remains available even in the optional frameless mode.
    Repeater {
        model: app.frameless && app.visibility !== Window.FullScreen && app.visibility !== Window.Maximized ? 8 : 0
        delegate: MouseArea {
            required property int index
            readonly property bool edgeLeft: index === 0 || index === 4 || index === 6
            readonly property bool edgeRight: index === 1 || index === 5 || index === 7
            readonly property bool edgeTop: index === 2 || index === 4 || index === 5
            readonly property bool edgeBottom: index === 3 || index === 6 || index === 7
            x: edgeRight ? app.width-6 : (edgeLeft ? 0 : 6)
            y: edgeBottom ? app.height-6 : (edgeTop ? 0 : 6)
            width: edgeLeft || edgeRight ? 6 : app.width-12
            height: edgeTop || edgeBottom ? 6 : app.height-12
            z: 20
            cursorShape: (edgeLeft && edgeTop) || (edgeRight && edgeBottom) ? Qt.SizeFDiagCursor : (edgeRight && edgeTop) || (edgeLeft && edgeBottom) ? Qt.SizeBDiagCursor : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: app.startSystemResize((edgeLeft ? Qt.LeftEdge : 0) | (edgeRight ? Qt.RightEdge : 0) | (edgeTop ? Qt.TopEdge : 0) | (edgeBottom ? Qt.BottomEdge : 0))
        }
    }
}
