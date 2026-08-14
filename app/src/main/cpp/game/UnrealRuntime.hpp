#pragma once

#include "core/Math.hpp"
#include "game/UnrealReflection.hpp"

#include <cstdint>
#include <chrono>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace valoader::game {

struct CharacterSnapshot {
    std::uintptr_t actor{};
    std::vector<math::Vec3> bones;
    // Empty for the known 88-bone player rig (VersionProfile supplies its
    // clean silhouette). Non-player rigs carry their own RefSkeleton tree.
    std::vector<std::pair<int, int>> connections;
};

struct FrameSnapshot {
    math::Camera camera{};
    std::vector<CharacterSnapshot> characters;
    std::string status;
    std::uintptr_t moduleBase{};
    std::uintptr_t world{};
};

// NPCs used by the training range are not ACharacter instances: their
// USkeletalMeshComponent is owned through an actor component collection rather
// than the stock ACharacter::Mesh field. Keep the resolved component together
// with the actor so frame capture does not repeat the expensive graph walk.
struct NpcMeshCandidate {
    std::uintptr_t actor{};
    std::uintptr_t mesh{};
};

class UnrealRuntime final {
public:
    [[nodiscard]] FrameSnapshot capture();

private:
    UnrealReflection reflection_;
    std::chrono::steady_clock::time_point nextDiscoveryAttempt_{};
    std::future<bool> discoveryFuture_;
    bool reflectionReady_{};
    std::string discoveryStatus_{"Reflection discovery is pending"};
    std::chrono::steady_clock::time_point nextNpcScan_{};
    std::vector<NpcMeshCandidate> cachedNpcCandidates_;
};

} // namespace valoader::game
