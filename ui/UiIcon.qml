import QtQuick
import QtQuick.Shapes
import Glancelot

// One outline icon family: 16-unit grid, 1.5 stroke, round caps and joins, recolored from theme tokens.
// Icons are decorative to assistive technology; the control that contains one carries the accessible name.
Item {
    id: icon
    property string name
    property color color: Theme.text
    property int size: Theme.iconSize
    // Stroke in 16-unit grid units. Large tile glyphs pass a thinner grid stroke so the drawn line stays light.
    property real lineWidth: 1.5
    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    readonly property var paths: ({
        "chevron-right": "M6 3.5 L10.5 8 L6 12.5",
        "chevron-down": "M3.5 6 L8 10.5 L12.5 6",
        "folder": "M2 4.5 Q2 3.5 3 3.5 H6.2 L7.7 5 H13 Q14 5 14 6 V12 Q14 13 13 13 H3 Q2 13 2 12 Z",
        "folder-open": "M2 12.5 V4.5 Q2 3.5 3 3.5 H6.2 L7.7 5 H12 Q13 5 13 6 V7.5 M2 12.5 L3.7 8.2 Q4 7.5 4.8 7.5 H14.2 Q15 7.5 14.7 8.3 L13.3 12.3 Q13 13 12.3 13 H2.5 Q2 13 2 12.5 Z",
        "drive": "M2 9.5 H14 V12 Q14 13 13 13 H3 Q2 13 2 12 Z M2 9.5 L3.8 4 Q4.1 3 5 3 H11 Q11.9 3 12.2 4 L14 9.5 M11 11.25 L11.01 11.25",
        "computer": "M2.5 3 H13.5 Q14 3 14 3.5 V10.5 Q14 11 13.5 11 H2.5 Q2 11 2 10.5 V3.5 Q2 3 2.5 3 Z M5.5 13.5 H10.5 M8 11 V13.5",
        "arrow-up": "M8 13 V3.5 M4 7.5 L8 3.5 L12 7.5",
        "arrow-down": "M8 3 V12.5 M4 8.5 L8 12.5 L12 8.5",
        "corner-up": "M13 13 H8 Q5 13 5 10 V3.5 M2 6.5 L5 3.5 L8 6.5",
        "lock": "M4 7.5 H12 Q12.5 7.5 12.5 8 V13 Q12.5 13.5 12 13.5 H4 Q3.5 13.5 3.5 13 V8 Q3.5 7.5 4 7.5 Z M5.5 7.5 V5.25 A2.5 2.5 0 0 1 10.5 5.25 V7.5",
        "pin":"M5.5 2.5 H10.5 M6.75 2.5 V6.5 L4.5 9.5 H11.5 L9.25 6.5 V2.5 M8 9.5 V14",
        "search": "M7 2.75 A4.25 4.25 0 1 0 7 11.25 A4.25 4.25 0 1 0 7 2.75 Z M10.2 10.2 L13.5 13.5",
        "check": "M3.5 8.5 L6.5 11.5 L12.5 4.5",
        "close": "M4 4 L12 12 M12 4 L4 12",
        "minimize": "M3.5 8 H12.5",
        "maximize": "M4.5 3.5 H11.5 Q12.5 3.5 12.5 4.5 V11.5 Q12.5 12.5 11.5 12.5 H4.5 Q3.5 12.5 3.5 11.5 V4.5 Q3.5 3.5 4.5 3.5 Z",
        "restore": "M4.5 6 H9.5 Q10.5 6 10.5 7 V12 Q10.5 13 9.5 13 H4.5 Q3.5 13 3.5 12 V7 Q3.5 6 4.5 6 Z M6 6 V4 Q6 3 7 3 H12 Q13 3 13 4 V9 Q13 10 12 10 H10.5",
        "appearance": "M8 2 A6 6 0 1 0 8 14 A6 6 0 1 0 8 2 Z M8 2 V14 M8 5 L11.6 3.4 M8 8.5 L13.8 6.5 M8 12 L13 10.6",
        "eye": "M1.5 8 Q8 0.8 14.5 8 Q8 15.2 1.5 8 Z M8 6 A2 2 0 1 0 8 10 A2 2 0 1 0 8 6 Z",
        "keyboard": "M2.5 4 H13.5 Q14.5 4 14.5 5 V11 Q14.5 12 13.5 12 H2.5 Q1.5 12 1.5 11 V5 Q1.5 4 2.5 4 Z M4.5 6.75 L4.51 6.75 M7 6.75 L7.01 6.75 M9.5 6.75 L9.51 6.75 M12 6.75 L12.01 6.75 M5 9.5 H11",
        "file": "M4.5 1.75 H9.25 L12.75 5.25 V13 Q12.75 14.25 11.5 14.25 H4.5 Q3.25 14.25 3.25 13 V3 Q3.25 1.75 4.5 1.75 Z M9.25 1.75 V4.25 Q9.25 5.25 10.25 5.25 H12.75",
        "link": "M6.5 9.5 L9.5 6.5 M7.25 4.75 L8.5 3.5 Q10.25 1.75 12 3.5 Q13.75 5.25 12 7 L10.75 8.25 M8.75 11.25 L7.5 12.5 Q5.75 14.25 4 12.5 Q2.25 10.75 4 9 L5.25 7.75",
        // Content modes: a photo, a grid of items, and both side by side.
        "image": "M3 3 H13 Q14 3 14 4 V12 Q14 13 13 13 H3 Q2 13 2 12 V4 Q2 3 3 3 Z M2 11 L5.5 7.5 L8.5 10.5 L10.5 8.5 L14 12 M10.5 5.5 L10.51 5.5",
        "grid": "M3 2.5 H6 Q6.5 2.5 6.5 3 V6 Q6.5 6.5 6 6.5 H3 Q2.5 6.5 2.5 6 V3 Q2.5 2.5 3 2.5 Z M10 2.5 H13 Q13.5 2.5 13.5 3 V6 Q13.5 6.5 13 6.5 H10 Q9.5 6.5 9.5 6 V3 Q9.5 2.5 10 2.5 Z M3 9.5 H6 Q6.5 9.5 6.5 10 V13 Q6.5 13.5 6 13.5 H3 Q2.5 13.5 2.5 13 V10 Q2.5 9.5 3 9.5 Z M10 9.5 H13 Q13.5 9.5 13.5 10 V13 Q13.5 13.5 13 13.5 H10 Q9.5 13.5 9.5 13 V10 Q9.5 9.5 10 9.5 Z",
        "split": "M3 3 H13 Q14 3 14 4 V12 Q14 13 13 13 H3 Q2 13 2 12 V4 Q2 3 3 3 Z M7 3 V13 M4 5.5 H5 M4 8 H5 M4 10.5 H5",
        "info":"M8 2 A6 6 0 1 0 8 14 A6 6 0 1 0 8 2 Z M8 7.5 V11 M8 5 L8.01 5",
        // The brand delete glyph (assets/brand/icons/ui/currentColor/delete.svg) redrawn on the 16-unit grid.
        "trash": "M2.75 4.75 H13.25 M6 4.75 V2.75 H10 V4.75 M12 4.75 L11.35 13.25 H4.65 L4 4.75 M6.75 7.25 V11 M9.25 7.25 V11",
        "alert": "M8 2.25 L14.25 13.25 H1.75 Z M8 6.5 V9.25 M8 11.25 L8.01 11.25",
        "spinner": "M8 2 A6 6 0 1 1 2 8",
        "star": "M8 1.5 L9.9 5.8 L14.5 6.3 L11.1 9.4 L12 14 L8 11.7 L4 14 L4.9 9.4 L1.5 6.3 L6.1 5.8 Z",
        "plus": "M8 3 V13 M3 8 H13"
    })

    Shape {
        width: 16; height: 16
        anchors.centerIn: parent
        scale: icon.size / 16
        preferredRendererType: Shape.GeometryRenderer
        antialiasing: true
        ShapePath {
            strokeWidth: icon.lineWidth
            strokeColor: icon.color
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
    // Loading is the one continuous motion in the UI; it runs only while the spinner is shown.
    RotationAnimator on rotation {
        running: icon.name === "spinner" && icon.visible
        from: 0; to: 360
        duration: 900
        loops: Animation.Infinite
    }
}
