#pragma once

#include <android/native_window.h>

namespace valoader::platform {

class SurfaceRenderer final {
public:
    static void run(ANativeWindow* window, float density);
    static void stop() noexcept;
    static void resize(int width, int height) noexcept;
    static void submitTouch(float x, float y, int action) noexcept;
};

} // namespace valoader::platform
