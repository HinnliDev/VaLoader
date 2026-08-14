#pragma once

namespace valoader::overlay {

class Menu final {
public:
    static void draw();
    static void setToggleBounds(float x, float y, float size) noexcept;
    static bool toggle() noexcept;
    [[nodiscard]] static bool visible() noexcept;
};

} // namespace valoader::overlay
