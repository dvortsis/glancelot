#pragma once
// Matches the operating-system title bar of a decorated (framed) window to the selected theme.
// Header-only so it needs no build-system change; included by app/main.cpp and the tests.
//
// Windows only, through DwmSetWindowAttribute:
//   - DWMWA_USE_IMMERSIVE_DARK_MODE (20; 19 before Windows 10 20H1) selects dark caption buttons and frame.
//   - DWMWA_BORDER_COLOR (34), DWMWA_CAPTION_COLOR (35) and DWMWA_TEXT_COLOR (36) need Windows 11 (build 22000);
//     Windows 10 rejects them, so there only the light/dark frame follows the theme.
// Every other platform is a no-op: Linux/X11/Wayland and macOS decorations are drawn by the window manager or
// compositor and stay as they are.
#include "glancelot/theme_manager.hpp"
#include <QColor>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QStyleHints>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi")
#endif

namespace glancelot {

// Theme roles for the native caption: the caption continues the toolbar surface; an inactive window shows its
// title in the muted role (both reach 4.5:1 on surface in every catalog variant, see theme_tests).
struct TitleBarColors {
    bool dark=false;
    QColor caption, text, border;
};

inline TitleBarColors titleBarColors(const QVariantMap& colors, bool dark, bool active) {
    return {dark, QColor(colors.value("surface").toString()),
            QColor(colors.value(active ? "text" : "muted").toString()), QColor(colors.value("border").toString())};
}

// COLORREF is 0x00BBGGRR, the reverse of QColor::rgb()'s 0xAARRGGBB channel order.
inline unsigned long toColorRef(const QColor& color) {
    return static_cast<unsigned long>(color.red()) | (static_cast<unsigned long>(color.green()) << 8)
           | (static_cast<unsigned long>(color.blue()) << 16);
}

// Which attributes the OS accepted; all false off Windows or for an invalid handle.
struct TitleBarResult {
    bool darkMode=false, caption=false, text=false, border=false;
};

inline TitleBarResult applyNativeTitleBar(quintptr handle, const TitleBarColors& colors) {
    TitleBarResult result;
#ifdef Q_OS_WIN
    const auto hwnd=reinterpret_cast<HWND>(handle);
    if(!hwnd || !IsWindow(hwnd)) return result;
    const BOOL dark=colors.dark ? TRUE : FALSE;
    result.darkMode=SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark))
                    || SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &dark, sizeof dark));
    const auto set=[&](DWORD attribute, const QColor& color) {
        const COLORREF value=toColorRef(color);
        return SUCCEEDED(DwmSetWindowAttribute(hwnd, attribute, &value, sizeof value));
    };
    result.border=set(34, colors.border);
    result.caption=set(35, colors.caption);
    result.text=set(36, colors.text);
#else
    Q_UNUSED(handle); Q_UNUSED(colors);
#endif
    return result;
}

// Keeps one window's native caption in step with the theme, the window's active state and native-window
// recreation (toggling the frameless flag can recreate the platform window). Qt's own dark-frame handling
// re-applies the OS color scheme when it changes, so the theme is re-applied after that too.
class NativeTitleBarSync final : public QObject {
public:
    NativeTitleBarSync(QWindow* window, ThemeManager* theme) : QObject(window), m_window(window), m_theme(theme) {
        if(!window || !theme) return;
        window->installEventFilter(this);
        connect(theme, &ThemeManager::changed, this, [this] { apply(); });
        connect(window, &QWindow::activeChanged, this, [this] { apply(); });
        if(qGuiApp)
            connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this,
                    [this] { QTimer::singleShot(0, this, [this] { apply(); }); });
        apply();
    }
    TitleBarResult last() const { return m_last; }
    void apply() {
        // Only real Windows windows have an HWND; offscreen/minimal test platforms are skipped.
        if(!m_window || !m_theme || QGuiApplication::platformName() != QLatin1String("windows") || !m_window->handle())
            return;
        m_last=applyNativeTitleBar(m_window->winId(), titleBarColors(m_theme->colors(), m_theme->dark(), m_window->isActive()));
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if(watched == m_window) {
            if(event->type() == QEvent::Show)
                apply();
            else if(event->type() == QEvent::PlatformSurface
                    && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
                QTimer::singleShot(0, this, [this] { apply(); });
        }
        return QObject::eventFilter(watched, event);
    }
private:
    QPointer<QWindow> m_window;
    QPointer<ThemeManager> m_theme;
    TitleBarResult m_last;
};

}
