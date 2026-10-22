pragma Singleton
import QtQuick

// The one source of UI design tokens. Colors are the active catalog family from ThemeManager (the `theme`
// context property); geometry, spacing, type and motion are fixed here so every surface shares them.
// The photo canvas background is deliberately not a theme token.
QtObject {
    // Color roles (project-defined; never hard-code product colors in components)
    readonly property bool dark: theme.dark
    readonly property color background: theme.colors.background
    readonly property color surface: theme.colors.surface
    readonly property color text: theme.colors.text
    readonly property color muted: theme.colors.muted
    readonly property color primary: theme.colors.primary
    readonly property color textOnPrimary: theme.colors.onPrimary // "on…" names would read as signal handlers
    readonly property color accent: theme.colors.accent
    readonly property color border: theme.colors.border
    readonly property color hover: theme.colors.hover
    readonly property color danger: theme.colors.danger
    // Derived roles
    readonly property color pressed: Qt.tint(hover, Qt.alpha(text, 0.08))
    readonly property color selection: Qt.alpha(primary, 0.18)
    readonly property color focusRing: text // distinct from primary, which marks selection and the current item
    readonly property color scrim: Qt.rgba(0, 0, 0, dark ? 0.6 : 0.4)
    readonly property real disabledOpacity: 0.4

    // Spacing ramp (4px base)
    readonly property int gapXxs: 2
    readonly property int gapXs: 4
    readonly property int gapSm: 8
    readonly property int gapMd: 12
    readonly property int gapLg: 16
    readonly property int gapXl: 24
    readonly property int gapXxl: 32

    // Geometry
    readonly property int radiusSmall: 4    // tags, swatches, thumbnails inside a cell
    readonly property int radiusControl: 8  // buttons, fields, rows, menus
    readonly property int radiusCard: 12    // cards, toasts
    readonly property int radiusLarge: 16   // dialogs
    readonly property int borderWidth: 1
    readonly property int focusWidth: 2
    readonly property int controlHeight: 32
    readonly property int rowHeight: 28
    readonly property int barHeight: 48
    readonly property int iconSize: 16

    // Typography & Accessibility (UI-SETTINGS-01 / UX-11)
    property real textScale: (typeof viewer !== "undefined" && viewer && viewer.textScale) ? viewer.textScale : 1.0
    property bool reducedMotion: (typeof viewer !== "undefined" && viewer && viewer.reducedMotion) ? viewer.reducedMotion : false

    // Type: one sans family for UI, one mono family for shortcuts; five sizes plus a display size
    readonly property string fontFamily: pick(["Geist", "SF Pro Display", "Helvetica Neue", "Switzer", "Segoe UI Variable Text", "Segoe UI", "Noto Sans"])
    readonly property string monoFamily: pick(["Geist Mono", "SF Mono", "JetBrains Mono", "Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Noto Sans Mono"])
    readonly property int textCaption: Math.round(11 * textScale)
    readonly property int textSmall: Math.round(12 * textScale)
    readonly property int textBody: Math.round(13 * textScale)
    readonly property int textTitle: Math.round(15 * textScale)
    readonly property int textHeading: Math.round(20 * textScale)
    readonly property int textDisplay: Math.round(36 * textScale)

    // Motion: routine state changes only; reducedMotion zeroes animations
    readonly property int durationFast: reducedMotion ? 0 : 150
    readonly property int durationNormal: reducedMotion ? 0 : 200

    function pick(candidates) {
        const installed = Qt.fontFamilies()
        for (const name of candidates) if (installed.indexOf(name) >= 0) return name
        return Qt.application.font.family
    }
}
