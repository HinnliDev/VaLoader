#include "overlay/Menu.hpp"

#include "overlay/OverlaySettings.hpp"
#include "overlay/SkeletonOverlay.hpp"
#include "third_party/imgui/imgui.h"

#include <atomic>

namespace valoader::overlay {
namespace {

std::atomic<float> g_toggleX{0.0F};
std::atomic<float> g_toggleY{0.0F};
std::atomic<float> g_toggleSize{64.0F};

void drawToggle() {
    const float x = g_toggleX.load(std::memory_order_relaxed);
    const float y = g_toggleY.load(std::memory_order_relaxed);
    const float size = g_toggleSize.load(std::memory_order_relaxed);

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size, size), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, size * 0.28F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, size * 0.28F);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.76F, 0.12F, 0.19F, 0.94F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92F, 0.18F, 0.25F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60F, 0.08F, 0.13F, 1.0F));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##ValoaderToggle", nullptr, flags)) {
        ImGui::Button("V", ImVec2(size, size));
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

const char* shortStatus(const game::FrameSnapshot& frame) noexcept {
    if (frame.status == "Ready") {
        return "ESP ready";
    }
    if (frame.moduleBase == 0) {
        return "Waiting for game";
    }
    return "Waiting for match data";
}

} // namespace

OverlaySettings& settings() noexcept {
    static OverlaySettings instance;
    return instance;
}

bool Menu::toggle() noexcept {
    const bool next = !settings().menuVisible.load(std::memory_order_relaxed);
    settings().menuVisible.store(next, std::memory_order_release);
    return next;
}

bool Menu::visible() noexcept {
    return settings().menuVisible.load(std::memory_order_acquire);
}

void Menu::setToggleBounds(float x, float y, float size) noexcept {
    g_toggleX.store(x, std::memory_order_relaxed);
    g_toggleY.store(y, std::memory_order_relaxed);
    g_toggleSize.store(size, std::memory_order_release);
}

void Menu::draw() {
    drawToggle();
    if (!visible()) {
        return;
    }

    OverlaySettings& configuration = settings();
    ImGui::SetNextWindowSize(ImVec2(500.0F, 330.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50.0F, 50.0F), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Valoader ESP", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Skeleton", &configuration.skeletonEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Joints", &configuration.jointMarkersEnabled);
    ImGui::Checkbox("Bone names", &configuration.boneNamesEnabled);
    ImGui::SliderFloat(
        "Line thickness", &configuration.lineThickness, 1.0F, 6.0F, "%.1f");
    ImGui::SliderFloat(
        "Joint size", &configuration.jointRadius, 1.0F, 8.0F, "%.1f");
    ImGui::ColorEdit4(
        "Skeleton color", configuration.skeletonColor, ImGuiColorEditFlags_NoInputs);

    ImGui::Separator();
    ImGui::TextDisabled("%s", shortStatus(skeletonOverlay().lastSnapshot()));
    ImGui::TextDisabled("by hinnli");
    ImGui::End();
}

} // namespace valoader::overlay
