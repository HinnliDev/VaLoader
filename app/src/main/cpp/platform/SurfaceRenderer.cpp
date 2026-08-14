#include "platform/SurfaceRenderer.hpp"

#include "core/Log.hpp"
#include "overlay/Menu.hpp"
#include "overlay/SkeletonOverlay.hpp"
#include "third_party/imgui/backends/imgui_impl_opengl3.h"
#include "third_party/imgui/imgui.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace valoader::platform {
namespace {

struct TouchState {
    std::atomic<float> x{-1.0F};
    std::atomic<float> y{-1.0F};
    std::atomic_bool down{false};
    std::atomic_bool changed{false};
};

std::atomic_bool g_running{false};
std::atomic<int> g_width{0};
std::atomic<int> g_height{0};
TouchState g_touch;

void applyStyle(float density) {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0F;
    style.FrameRounding = 6.0F;
    style.GrabRounding = 6.0F;
    style.ScaleAllSizes(density > 0.1F ? density : 1.0F);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055F, 0.063F, 0.082F, 0.96F);
    style.Colors[ImGuiCol_Header] = ImVec4(0.75F, 0.16F, 0.22F, 0.75F);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.92F, 0.22F, 0.29F, 0.85F);
    style.Colors[ImGuiCol_Button] = ImVec4(0.75F, 0.16F, 0.22F, 0.80F);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.92F, 0.22F, 0.29F, 1.0F);
}

void applyInput() {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(g_touch.x.load(std::memory_order_relaxed), g_touch.y.load(std::memory_order_relaxed));
    if (g_touch.changed.exchange(false, std::memory_order_acq_rel)) {
        io.AddMouseButtonEvent(0, g_touch.down.load(std::memory_order_relaxed));
    }
}

} // namespace

void SurfaceRenderer::run(ANativeWindow* window, float density) {
    if (window == nullptr || g_running.exchange(true, std::memory_order_acq_rel)) {
        if (window != nullptr) {
            ANativeWindow_release(window);
        }
        return;
    }

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config{};
    bool initialized = false;

    do {
        if (display == EGL_NO_DISPLAY || eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
            VALOADER_LOG_ERROR("Unable to initialize EGL display");
            break;
        }

        constexpr EGLint configAttributes[]{
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0,
            EGL_NONE
        };
        EGLint configCount{};
        if (eglChooseConfig(display, configAttributes, &config, 1, &configCount) != EGL_TRUE || configCount == 0) {
            VALOADER_LOG_ERROR("No transparent EGL configuration is available");
            break;
        }

        EGLint format{};
        eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
        ANativeWindow_setBuffersGeometry(window, 0, 0, format);
        surface = eglCreateWindowSurface(display, config, window, nullptr);
        constexpr EGLint contextAttributes[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
        if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
            eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
            VALOADER_LOG_ERROR("Unable to create overlay EGL surface");
            break;
        }

        eglSwapInterval(display, 1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.Fonts->AddFontFromFileTTF(
            "/system/fonts/Roboto-Regular.ttf",
            16.0F * density,
            nullptr,
            io.Fonts->GetGlyphRangesCyrillic()
        );
        applyStyle(density);
        if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
            VALOADER_LOG_ERROR("Unable to initialize ImGui OpenGL backend");
            break;
        }
        initialized = true;
        VALOADER_LOG_INFO("ImGui overlay started");

        auto previousFrame = std::chrono::steady_clock::now();
        while (g_running.load(std::memory_order_acquire)) {
            int width = g_width.load(std::memory_order_relaxed);
            int height = g_height.load(std::memory_order_relaxed);
            if (width <= 0 || height <= 0) {
                width = ANativeWindow_getWidth(window);
                height = ANativeWindow_getHeight(window);
            }

            const auto now = std::chrono::steady_clock::now();
            io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
            io.DeltaTime = std::chrono::duration<float>(now - previousFrame).count();
            previousFrame = now;
            applyInput();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            overlay::skeletonOverlay().draw(io.DisplaySize.x, io.DisplaySize.y);
            overlay::Menu::draw();
            ImGui::Render();

            glViewport(0, 0, width, height);
            glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (eglSwapBuffers(display, surface) != EGL_TRUE) {
                VALOADER_LOG_WARN("Overlay EGL surface was lost");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    } while (false);

    if (initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
    }
    ANativeWindow_release(window);
    g_running.store(false, std::memory_order_release);
    VALOADER_LOG_INFO("ImGui overlay stopped");
}

void SurfaceRenderer::stop() noexcept {
    g_running.store(false, std::memory_order_release);
}

void SurfaceRenderer::resize(int width, int height) noexcept {
    g_width.store(width, std::memory_order_relaxed);
    g_height.store(height, std::memory_order_relaxed);
}

void SurfaceRenderer::submitTouch(float x, float y, int action) noexcept {
    g_touch.x.store(x, std::memory_order_relaxed);
    g_touch.y.store(y, std::memory_order_relaxed);
    if (action == 0) {
        g_touch.down.store(true, std::memory_order_relaxed);
        g_touch.changed.store(true, std::memory_order_release);
    } else if (action == 1 || action == 3) {
        g_touch.down.store(false, std::memory_order_relaxed);
        g_touch.changed.store(true, std::memory_order_release);
    }
}

} // namespace valoader::platform
