#pragma once
// E6: WCAG 2.2 contrast arithmetic and the roles derived from a catalog palette.
// Header-only so it needs no build-system change; included by theme_manager.cpp and the tests.
//
// The catalog roles stay byte-identical to the supplied branding (assets/brand/palette.json for slate-ice).
// Three roles are derived from them and added beside them, never in their place:
//   controlBorder  the catalog border moved toward the family's text colour until it reaches 3:1 against both
//                  the background and the surface (WCAG 1.4.11, the boundary of a field, button or card);
//   familyHover    a hover and pressed fill mixed from the family's own surface and text, so it follows warm and
//                  cool families instead of one fixed blue-grey (UX audit finding 19);
//   placeholder    the "Loading preview" and "Preview unavailable" tile fill, one step stronger than familyHover.
// A fill never drops text below 4.5:1, muted text below 4.5:1 or the primary marker below 3:1.
#include <QColor>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace glancelot::theme {

inline double relativeLuminance(const QColor& color) {
    const auto channel=[](double value) { return value<=0.04045 ? value/12.92 : std::pow((value+0.055)/1.055, 2.4); };
    return 0.2126*channel(color.redF())+0.7152*channel(color.greenF())+0.0722*channel(color.blueF());
}

inline double contrastRatio(const QColor& a, const QColor& b) {
    const auto x=relativeLuminance(a), y=relativeLuminance(b);
    return (std::max(x, y)+0.05)/(std::min(x, y)+0.05);
}

// Straight sRGB interpolation, rounded to 8 bits per channel: what is measured is what is drawn.
inline QColor mix(const QColor& from, const QColor& to, double amount) {
    const auto channel=[amount](int a, int b) { return int(std::lround(a+(b-a)*amount)); };
    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()), channel(from.blue(), to.blue()));
}

inline constexpr int mixSteps=100;
inline constexpr double nonTextMinimum=3.0;   // WCAG 2.2 SC 1.4.11
inline constexpr double textMinimum=4.5;      // WCAG 2.2 SC 1.4.3
inline constexpr double hoverStep[2]={1.25, 1.45};       // familyHover against the surface: {light, dark}
inline constexpr double placeholderStep[2]={1.4, 1.7};   // placeholder against the surface: {light, dark}

// The first mix from `start` toward `toward` that reaches `minimum` against every colour in `against`.
inline QColor raiseToContrast(const QColor& start, const QColor& toward, std::initializer_list<QColor> against, double minimum) {
    for(int step=0; step<=mixSteps; ++step) {
        const auto candidate=mix(start, toward, double(step)/mixSteps);
        if(std::all_of(against.begin(), against.end(), [&](const QColor& other) { return contrastRatio(candidate, other)>=minimum; }))
            return candidate;
    }
    return toward;
}

// A fill from the surface toward the text colour: the first mix that reaches `target` against the surface, or the
// strongest mix before text, muted text or the primary marker would fall below its minimum on it.
inline QColor tintedFill(const QColor& surface, const QColor& text, const QColor& muted, const QColor& primary, double target) {
    QColor best=surface;
    for(int step=0; step<=mixSteps; ++step) {
        const auto candidate=mix(surface, text, double(step)/mixSteps);
        if(contrastRatio(candidate, text)<textMinimum || contrastRatio(candidate, muted)<textMinimum
           || contrastRatio(candidate, primary)<nonTextMinimum)
            break;
        best=candidate;
        if(contrastRatio(candidate, surface)>=target) break;
    }
    return best;
}

inline QString hex(const QColor& color) { return color.name(QColor::HexRgb).toUpper(); }

// The catalog palette plus the derived roles. An incomplete palette (unknown family) is returned unchanged.
inline QVariantMap withDerivedRoles(QVariantMap palette, bool dark) {
    const auto role=[&](const char* name) { return QColor(palette.value(QLatin1String(name)).toString()); };
    const auto background=role("background"), surface=role("surface"), text=role("text"), muted=role("muted"),
               primary=role("primary"), border=role("border");
    if(!background.isValid() || !surface.isValid() || !text.isValid() || !muted.isValid() || !primary.isValid() || !border.isValid())
        return palette;
    const int mode=dark ? 1 : 0;
    palette.insert(QStringLiteral("controlBorder"), hex(raiseToContrast(border, text, {background, surface}, nonTextMinimum)));
    palette.insert(QStringLiteral("familyHover"), hex(tintedFill(surface, text, muted, primary, hoverStep[mode])));
    palette.insert(QStringLiteral("placeholder"), hex(tintedFill(surface, text, muted, primary, placeholderStep[mode])));
    return palette;
}

}  // namespace glancelot::theme
