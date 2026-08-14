#include "overlay/SkeletonOverlay.hpp"

#include "game/VersionProfile.hpp"
#include "overlay/OverlaySettings.hpp"
#include "third_party/imgui/imgui.h"

#include <array>
#include <cstdio>
#include <unordered_set>
#include <string_view>

namespace valoader::overlay {
namespace {

struct NamedBone {
    int index;
    std::string_view name;
};

constexpr std::array<NamedBone, 17> kNamedBones{{
    {8, "Head"}, {7, "Neck"}, {6, "Chest"}, {5, "Spine"}, {2, "Pelvis"},
    {20, "L shoulder"}, {21, "L elbow"}, {23, "L hand"},
    {43, "R shoulder"}, {44, "R elbow"}, {46, "R hand"},
    {68, "L thigh"}, {69, "L knee"}, {71, "L foot"},
    {72, "R thigh"}, {73, "R knee"}, {75, "R foot"},
}};

} // namespace

SkeletonOverlay& skeletonOverlay() noexcept {
    static SkeletonOverlay instance;
    return instance;
}

void SkeletonOverlay::draw(float width, float height) {
    OverlaySettings& configuration = settings();
    if (!configuration.skeletonEnabled) {
        return;
    }

    lastSnapshot_ = runtime_.capture();
    for (const game::CharacterSnapshot& character : lastSnapshot_.characters) {
        drawCharacter(character, lastSnapshot_.camera, width, height);
    }
}

const game::FrameSnapshot& SkeletonOverlay::lastSnapshot() const noexcept {
    return lastSnapshot_;
}

void SkeletonOverlay::drawCharacter(
    const game::CharacterSnapshot& character,
    const math::Camera& camera,
    float width,
    float height
) const {
    OverlaySettings& configuration = settings();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
        configuration.skeletonColor[0],
        configuration.skeletonColor[1],
        configuration.skeletonColor[2],
        configuration.skeletonColor[3]
    ));
    const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
        configuration.nameColor[0],
        configuration.nameColor[1],
        configuration.nameColor[2],
        configuration.nameColor[3]
    ));

    const auto drawConnections = [&](const auto& connections) {
        for (const auto& [parentIndex, childIndex] : connections) {
            if (parentIndex < 0 || childIndex < 0 ||
                static_cast<std::size_t>(parentIndex) >= character.bones.size() ||
                static_cast<std::size_t>(childIndex) >= character.bones.size()) {
                continue;
            }
            math::Vec2 parentScreen{};
            math::Vec2 childScreen{};
            if (!math::worldToScreen(
                    character.bones[static_cast<std::size_t>(parentIndex)], camera,
                    width, height, parentScreen) ||
                !math::worldToScreen(
                    character.bones[static_cast<std::size_t>(childIndex)], camera,
                    width, height, childScreen)) {
                continue;
            }
            drawList->AddLine(
                ImVec2(parentScreen.x, parentScreen.y),
                ImVec2(childScreen.x, childScreen.y),
                lineColor,
                configuration.lineThickness
            );
        }
    };
    if (character.connections.empty()) {
        drawConnections(game::kDefaultSkeletonConnections);
    } else {
        drawConnections(character.connections);
    }


    // Named indices only describe the published 88-bone player rig. For a
    // runtime RefSkeleton (training NPCs), mark the actual tree joints instead.
    if (!character.connections.empty()) {
        if (!configuration.jointMarkersEnabled) {
            return;
        }
        std::unordered_set<int> drawn;
        for (const auto& [parentIndex, childIndex] : character.connections) {
            for (const int index : {parentIndex, childIndex}) {
                if (index < 0 || static_cast<std::size_t>(index) >= character.bones.size() ||
                    !drawn.insert(index).second) {
                    continue;
                }
                math::Vec2 position{};
                if (math::worldToScreen(
                        character.bones[static_cast<std::size_t>(index)], camera,
                        width, height, position)) {
                    drawList->AddCircleFilled(
                        ImVec2(position.x, position.y),
                        configuration.jointRadius, lineColor);
                }
            }
        }
        return;
    }

    for (const NamedBone& bone : kNamedBones) {
        if (bone.index < 0 || static_cast<std::size_t>(bone.index) >= character.bones.size()) {
            continue;
        }
        math::Vec2 position{};
        if (!math::worldToScreen(character.bones[static_cast<std::size_t>(bone.index)], camera, width, height, position)) {
            continue;
        }
        if (configuration.jointMarkersEnabled) {
            drawList->AddCircleFilled(ImVec2(position.x, position.y), configuration.jointRadius, lineColor);
        }
        if (configuration.boneNamesEnabled) {
            char label[48]{};
            std::snprintf(label, sizeof(label), "%.*s [%d]", static_cast<int>(bone.name.size()), bone.name.data(), bone.index);
            drawList->AddText(ImVec2(position.x + 5.0F, position.y - 8.0F), textColor, label);
        }
    }
}

} // namespace valoader::overlay
