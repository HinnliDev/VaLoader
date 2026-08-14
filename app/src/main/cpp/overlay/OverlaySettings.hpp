#pragma once

#include <atomic>

namespace valoader::overlay {

struct OverlaySettings {
    std::atomic_bool menuVisible{false};
    bool skeletonEnabled{true};
    bool boneNamesEnabled{false};
    bool jointMarkersEnabled{true};
    float lineThickness{2.4F};
    float jointRadius{3.5F};
    float skeletonColor[4]{0.96F, 0.22F, 0.30F, 1.0F};
    float nameColor[4]{1.0F, 1.0F, 1.0F, 0.95F};
};

[[nodiscard]] OverlaySettings& settings() noexcept;

} // namespace valoader::overlay
