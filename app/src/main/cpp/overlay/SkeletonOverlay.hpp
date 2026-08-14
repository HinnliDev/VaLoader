#pragma once

#include "game/UnrealRuntime.hpp"

namespace valoader::overlay {

class SkeletonOverlay final {
public:
    void draw(float width, float height);
    [[nodiscard]] const game::FrameSnapshot& lastSnapshot() const noexcept;

private:
    void drawCharacter(
        const game::CharacterSnapshot& character,
        const math::Camera& camera,
        float width,
        float height
    ) const;
    game::UnrealRuntime runtime_;
    game::FrameSnapshot lastSnapshot_;
};

[[nodiscard]] SkeletonOverlay& skeletonOverlay() noexcept;

} // namespace valoader::overlay
