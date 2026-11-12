#pragma once
// E6 (F052/F053): the operating system's high-contrast setting, read natively and followed while the app runs.
// Header-only so it needs no build-system change; included by theme_manager.cpp and the tests.
//
// Qt 6.8.3 has no contrast hint (QStyleHints::colorScheme only; Qt::ContrastPreference arrives in a later Qt), so:
//   - Windows: SystemParametersInfoW(SPI_GETHIGHCONTRAST) and HCF_HIGHCONTRASTON. The polarity of the active
//     high-contrast theme comes from GetSysColor(COLOR_WINDOW). Changes arrive as WM_SETTINGCHANGE with
//     SPI_SETHIGHCONTRAST, WM_SYSCOLORCHANGE or WM_THEMECHANGED on the app's top-level windows.
//   - Linux and the other freedesktop systems: GNOME's org.gnome.desktop.a11y.interface high-contrast key (GNOME 42
//     and later), the older org.gnome.desktop.interface gtk-theme "HighContrast"/"HighContrastInverse" themes, and the
//     GTK_THEME environment variable. GSettings is loaded at run time from libgio (already part of every GNOME session),
//     so no library is linked; without it, or without the schemas, the state is "not high contrast". Changes arrive as
//     GSettings "changed" signals, which Qt's GLib event loop delivers on the GUI thread.
//   - Every platform also re-reads the state when Qt reports a colour-scheme change.
// Reading is synchronous and cheap (one system call, or a few in-memory GSettings reads); nothing here decodes,
// scans directories or blocks on the network. The offscreen and minimal platforms have no desktop, so they report
// "not high contrast" unless a test installs a probe.
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStyleHints>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN) && !defined(Q_OS_ANDROID)
#define GLANCELOT_CONTRAST_GSETTINGS 1
#include <QLibrary>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace glancelot {

struct SystemContrast {
    bool high=false;
    std::optional<bool> dark;  // the system high-contrast theme's polarity, when the platform states it
    QString source;            // what reported it: "windows", "gnome-a11y", "gtk-theme", "GTK_THEME" or "test"
    bool operator==(const SystemContrast&) const=default;
};

namespace contrast_detail {

// A GTK theme name such as "HighContrast", "HighContrastInverse" or "HighContrast:dark".
inline std::optional<SystemContrast> fromGtkThemeName(const QString& name, const QString& source) {
    if(!name.contains(QLatin1String("highcontrast"), Qt::CaseInsensitive)) return std::nullopt;
    const bool inverse=name.contains(QLatin1String("inverse"), Qt::CaseInsensitive) || name.endsWith(QLatin1String(":dark"), Qt::CaseInsensitive);
    return SystemContrast{true, inverse, source};
}

#ifdef GLANCELOT_CONTRAST_GSETTINGS
// The handful of GIO entry points used, resolved once. GSettings aborts on an unknown schema or key, so every
// lookup goes through the schema source first.
struct Gio {
    using Callback=void (*)();
    void* (*sourceDefault)()=nullptr;
    void* (*sourceLookup)(void*, const char*, int)=nullptr;
    int (*schemaHasKey)(void*, const char*)=nullptr;
    void (*schemaUnref)(void*)=nullptr;
    void* (*settingsNewFull)(void*, void*, const char*)=nullptr;
    int (*getBoolean)(void*, const char*)=nullptr;
    char* (*getString)(void*, const char*)=nullptr;
    void (*free)(void*)=nullptr;
    unsigned long (*connect)(void*, const char*, Callback, void*, void*, int)=nullptr;
    void (*disconnect)(void*, unsigned long)=nullptr;
    void (*unref)(void*)=nullptr;
    bool ok=false;
    static const Gio& get() {
        static const Gio gio=[] {
            Gio api;
            QLibrary library(QStringLiteral("gio-2.0"), 0);  // libgio-2.0.so.0; stays loaded for the process
            if(!library.load()) return api;
            const auto resolve=[&](auto& slot, const char* name) {
                slot=reinterpret_cast<std::remove_reference_t<decltype(slot)>>(library.resolve(name)); return slot!=nullptr;
            };
            api.ok=resolve(api.sourceDefault, "g_settings_schema_source_get_default")
                   && resolve(api.sourceLookup, "g_settings_schema_source_lookup")
                   && resolve(api.schemaHasKey, "g_settings_schema_has_key")
                   && resolve(api.schemaUnref, "g_settings_schema_unref")
                   && resolve(api.settingsNewFull, "g_settings_new_full")
                   && resolve(api.getBoolean, "g_settings_get_boolean")
                   && resolve(api.getString, "g_settings_get_string")
                   && resolve(api.free, "g_free")
                   && resolve(api.connect, "g_signal_connect_data")
                   && resolve(api.disconnect, "g_signal_handler_disconnect")
                   && resolve(api.unref, "g_object_unref");
            return api;
        }();
        return gio;
    }
};

// One GSettings object for a schema that has `key`, watched for changes to that key; empty when unavailable.
class GSettingsKey {
public:
    GSettingsKey(const char* schemaId, const char* key, void (*changed)(void*, const char*, void*), void* context) : m_key(key) {
        const auto& gio=Gio::get();
        if(!gio.ok) return;
        void* source=gio.sourceDefault();
        if(!source) return;
        void* schema=gio.sourceLookup(source, schemaId, 1);
        if(!schema) return;
        if(gio.schemaHasKey(schema, key)) m_settings=gio.settingsNewFull(schema, nullptr, nullptr);
        gio.schemaUnref(schema);
        if(m_settings && changed) {
            const QByteArray signal="changed::"+QByteArray(key);
            m_handler=gio.connect(m_settings, signal.constData(), reinterpret_cast<Gio::Callback>(changed), context, nullptr, 0);
        }
    }
    ~GSettingsKey() {
        const auto& gio=Gio::get();
        if(!m_settings) return;
        if(m_handler) gio.disconnect(m_settings, m_handler);
        gio.unref(m_settings);
    }
    GSettingsKey(const GSettingsKey&)=delete;
    GSettingsKey& operator=(const GSettingsKey&)=delete;
    bool available() const { return m_settings!=nullptr; }
    bool boolean() const { return m_settings && Gio::get().getBoolean(m_settings, m_key); }
    QString string() const {
        if(!m_settings) return {};
        char* value=Gio::get().getString(m_settings, m_key);
        const auto text=QString::fromUtf8(value);
        Gio::get().free(value);
        return text;
    }
private:
    const char* m_key;
    void* m_settings=nullptr;
    unsigned long m_handler=0;
};
#endif

#ifdef Q_OS_WIN
inline SystemContrast probeWindows() {
    HIGHCONTRASTW contrast{sizeof contrast};
    if(!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof contrast, &contrast, 0) || !(contrast.dwFlags & HCF_HIGHCONTRASTON))
        return {};
    const COLORREF window=GetSysColor(COLOR_WINDOW);
    const QColor background(GetRValue(window), GetGValue(window), GetBValue(window));
    // The window background is what the app paints, so its relative luminance picks the dark token set (Aquatic,
    // Dusk, Night sky) or the light one (Desert); 0.18 is the luminance of middle grey.
    const auto channel=[](double value) { return value<=0.04045 ? value/12.92 : std::pow((value+0.055)/1.055, 2.4); };
    const double luminance=0.2126*channel(background.redF())+0.7152*channel(background.greenF())+0.0722*channel(background.blueF());
    return {true, luminance<0.18, QStringLiteral("windows")};
}
#endif

inline std::function<SystemContrast()>& testProbe() { static std::function<SystemContrast()> probe; return probe; }

}  // namespace contrast_detail

class SystemContrastWatch;
namespace contrast_detail {
inline std::vector<SystemContrastWatch*>& watches() { static std::vector<SystemContrastWatch*> list; return list; }
}

// Holds the current state for one owner and calls `changed` whenever it differs from the last reading. GUI thread only.
// Not a Q_OBJECT: it needs no signals of its own, and it stays header-only.
class SystemContrastWatch final : public QObject, public QAbstractNativeEventFilter {
public:
    static constexpr const char* objectNameValue="glancelotSystemContrast";
    SystemContrastWatch(QObject* parent, std::function<void()> changed) : QObject(parent), m_changed(std::move(changed)) {
        setObjectName(QLatin1String(objectNameValue));
        m_refresh.setSingleShot(true);
        m_refresh.setInterval(0);  // coalesces a burst (one WM_SETTINGCHANGE per top-level window) into one reading
        QObject::connect(&m_refresh, &QTimer::timeout, this, [this] { refresh(); });
        contrast_detail::watches().push_back(this);
        m_native=nativeAllowed();
        if(m_native) {
#ifdef Q_OS_WIN
            qGuiApp->installNativeEventFilter(this);
#endif
#ifdef GLANCELOT_CONTRAST_GSETTINGS
            m_a11y=std::make_unique<contrast_detail::GSettingsKey>("org.gnome.desktop.a11y.interface", "high-contrast", &gsettingsChanged, this);
            m_gtkTheme=std::make_unique<contrast_detail::GSettingsKey>("org.gnome.desktop.interface", "gtk-theme", &gsettingsChanged, this);
#endif
            QObject::connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { scheduleRefresh(); });
        }
        m_state=probe();
    }
    ~SystemContrastWatch() override {
        auto& list=contrast_detail::watches();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
#ifdef Q_OS_WIN
        if(m_native && QCoreApplication::instance()) qGuiApp->removeNativeEventFilter(this);
#endif
    }
    const SystemContrast& state() const { return m_state; }
    // Re-reads now; calls `changed` only when the reading differs.
    void refresh() {
        const auto next=probe();
        if(next==m_state) return;
        m_state=next;
        if(m_changed) m_changed();
    }
    void scheduleRefresh() { m_refresh.start(); }
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
#ifdef Q_OS_WIN
        if(eventType=="windows_generic_MSG" && message) {
            const auto* msg=static_cast<const MSG*>(message);
            if((msg->message==WM_SETTINGCHANGE && msg->wParam==SPI_SETHIGHCONTRAST) || msg->message==WM_SYSCOLORCHANGE
               || msg->message==WM_THEMECHANGED)
                scheduleRefresh();
        }
#else
        Q_UNUSED(eventType); Q_UNUSED(message);
#endif
        return false;
    }
    // The desktop's state, or the test probe's; never cached across calls.
    SystemContrast probe() const {
        if(const auto& fake=contrast_detail::testProbe()) return fake();
        if(!m_native) return {};
#ifdef Q_OS_WIN
        return contrast_detail::probeWindows();
#elif defined(GLANCELOT_CONTRAST_GSETTINGS)
        if(const auto fromEnvironment=contrast_detail::fromGtkThemeName(qEnvironmentVariable("GTK_THEME"), QStringLiteral("GTK_THEME")))
            return *fromEnvironment;
        if(m_a11y && m_a11y->boolean()) return {true, std::nullopt, QStringLiteral("gnome-a11y")};
        if(m_gtkTheme) if(const auto fromTheme=contrast_detail::fromGtkThemeName(m_gtkTheme->string(), QStringLiteral("gtk-theme"))) return *fromTheme;
        return {};
#else
        return {};
#endif
    }
    static bool nativeAllowed() {
        if(!qGuiApp) return false;
        const auto platform=QGuiApplication::platformName();
        return platform!=QLatin1String("offscreen") && platform!=QLatin1String("minimal");
    }
private:
#ifdef GLANCELOT_CONTRAST_GSETTINGS
    static void gsettingsChanged(void*, const char*, void* context) { static_cast<SystemContrastWatch*>(context)->scheduleRefresh(); }
    std::unique_ptr<contrast_detail::GSettingsKey> m_a11y, m_gtkTheme;
#endif
    std::function<void()> m_changed;
    QTimer m_refresh;
    SystemContrast m_state;
    bool m_native=false;
};

// Test seam: replaces the desktop reading for every watch (an empty function restores it) and re-reads now.
inline void setSystemContrastProbeForTesting(std::function<SystemContrast()> probe) {
    contrast_detail::testProbe()=std::move(probe);
    for(auto* watch:std::vector<SystemContrastWatch*>(contrast_detail::watches())) watch->refresh();
}

// What `owner`'s watch last read (a default state when it has none).
inline SystemContrast systemContrastOf(const QObject* owner) {
    if(!owner) return {};
    const auto* child=owner->findChild<QObject*>(QLatin1String(SystemContrastWatch::objectNameValue), Qt::FindDirectChildrenOnly);
    const auto* watch=dynamic_cast<const SystemContrastWatch*>(child);
    return watch ? watch->state() : SystemContrast{};
}

}  // namespace glancelot
