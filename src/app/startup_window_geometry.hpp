#pragma once
// Lead-frozen STARTUP-WINDOW-01 private value contract. Implemented only by its Codex packet.
#include <QRect>
#include <QSize>
#include <cstddef>
#include <span>

namespace glancelot::startup {
inline constexpr std::size_t maximumStartupScreens = 64;
enum class StartupPlacementStatus { Unchanged, Recovered, NoUsableScreen, TooManyScreens };
struct StartupPlacement {
    QRect geometry;
    StartupPlacementStatus status = StartupPlacementStatus::Unchanged;
};
// All coordinates are Qt logical desktop coordinates. No native-pixel/DPR conversion,
// screen enumeration, window mutation, persistent state or allocation occurs here.
[[nodiscard]] StartupPlacement recoverStartupWindowGeometry(
    const QRect& current, std::span<const QRect> availableScreens,
    std::size_t preferredScreen, const QSize& minimumSize);
}
