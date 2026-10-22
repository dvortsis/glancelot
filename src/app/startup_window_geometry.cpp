#include "startup_window_geometry.hpp"
#include <algorithm>
#include <limits>

namespace glancelot::startup {
namespace {
struct Bounds {
    qint64 left, top, right, bottom;
    bool usable() const { return left < right && top < bottom; }
    qint64 width() const { return right - left; }
    qint64 height() const { return bottom - top; }
};
Bounds bounds(const QRect& rect) {
    // QRect stores inclusive endpoints. Read them before widening, never add in int arithmetic.
    return {rect.left(), rect.top(), qint64(rect.right()) + 1, qint64(rect.bottom()) + 1};
}
bool intersects(Bounds a, Bounds b) {
    return a.usable() && b.usable() && std::max(a.left,b.left) < std::min(a.right,b.right)
        && std::max(a.top,b.top) < std::min(a.bottom,b.bottom);
}
int origin(qint64 start, qint64 available, qint64 extent) {
    const auto centered = start + std::max(qint64(0), (available - extent) / 2);
    // Keep both inclusive QRect endpoints representable, even at artificial integer edges.
    return int(std::clamp(centered, qint64(std::numeric_limits<int>::min()),
                          qint64(std::numeric_limits<int>::max()) - extent + 1));
}
}
StartupPlacement recoverStartupWindowGeometry(const QRect& current, std::span<const QRect> screens,
                                               std::size_t preferred, const QSize& minimum) {
    if (screens.size() > maximumStartupScreens) return {current, StartupPlacementStatus::TooManyScreens};
    const auto currentBounds = bounds(current);
    std::size_t chosen = screens.size();
    for (std::size_t i = 0; i < screens.size(); ++i) {
        const auto screen = bounds(screens[i]);
        if (!screen.usable()) continue;
        if (intersects(currentBounds,screen)) return {current,StartupPlacementStatus::Unchanged};
        if (chosen == screens.size()) chosen = i;
    }
    if (chosen == screens.size()) return {current,StartupPlacementStatus::NoUsableScreen};
    if (preferred < screens.size() && bounds(screens[preferred]).usable()) chosen = preferred;
    const auto screen = bounds(screens[chosen]);
    const qint64 minWidth = std::max(1,minimum.width()), minHeight = std::max(1,minimum.height());
    const auto width = std::max(minWidth,std::min(screen.width(), currentBounds.usable() ?
        std::min(currentBounds.width(),qint64(std::numeric_limits<int>::max())) : minWidth));
    const auto height = std::max(minHeight,std::min(screen.height(), currentBounds.usable() ?
        std::min(currentBounds.height(),qint64(std::numeric_limits<int>::max())) : minHeight));
    const auto x=origin(screen.left,screen.width(),width), y=origin(screen.top,screen.height(),height);
    return {QRect(QPoint(x,y),QPoint(int(qint64(x)+width-1),int(qint64(y)+height-1))),
            StartupPlacementStatus::Recovered};
}
}
