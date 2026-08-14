#include "game/UnrealRuntime.hpp"

#include "core/Memory.hpp"
#include "core/Log.hpp"
#include "game/VersionProfile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_set>

namespace valoader::game {
namespace {

struct PointerArray {
    std::uintptr_t data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct MeshBoneInfo {
    std::uint32_t comparisonIndex{};
    std::uint32_t number{};
    std::int32_t parentIndex{-1};
};

static_assert(sizeof(MeshBoneInfo) == 0x0C);

struct CaptureStats {
    std::size_t actors{};
    std::size_t nonNullActors{};
    std::size_t meshPointers{};
    std::size_t boneHeaders{};
    std::size_t boneArrays{};
    std::size_t componentTransforms{};
    std::size_t captured{};
    std::array<std::size_t, 4> transformOffsetHits{};
};

bool readComponentSpacePose(
    const PointerArray& header,
    std::vector<math::Transform>& transforms
) {
    if (header.count < 20 || header.count > 1024 ||
        header.capacity < header.count || header.capacity > 2048) {
        return false;
    }
    transforms.resize(static_cast<std::size_t>(header.count));
    if (!memory::readBytesSafe(
            header.data, transforms.data(),
            transforms.size() * sizeof(math::Transform))) {
        return false;
    }
    math::Vec3 minimum{INFINITY, INFINITY, INFINITY};
    math::Vec3 maximum{-INFINITY, -INFINITY, -INFINITY};
    for (const math::Transform& transform : transforms) {
        if (!math::isFinite(transform.translation) ||
            !math::isFinite(transform.scale)) {
            return false;
        }
        minimum.x = std::min(minimum.x, transform.translation.x);
        minimum.y = std::min(minimum.y, transform.translation.y);
        minimum.z = std::min(minimum.z, transform.translation.z);
        maximum.x = std::max(maximum.x, transform.translation.x);
        maximum.y = std::max(maximum.y, transform.translation.y);
        maximum.z = std::max(maximum.z, transform.translation.z);
    }
    const float maximumExtent = std::max({
        maximum.x - minimum.x,
        maximum.y - minimum.y,
        maximum.z - minimum.z
    });
    // A component-space humanoid pose spans roughly 170 UE units. Local-space
    // ref-pose arrays only contain short individual segments and caused all
    // joints to collapse around the actor origin.
    return maximumExtent >= 50.0F && maximumExtent <= 1000.0F;
}

std::optional<CharacterSnapshot> captureCharacter(
    std::uintptr_t actor,
    std::uintptr_t resolvedMesh,
    CaptureStats& stats
) {
    const bool useRuntimeHierarchy = resolvedMesh != 0;
    std::uintptr_t mesh = resolvedMesh;
    if (mesh == 0) {
        mesh = memory::readSafe<std::uintptr_t>(
            actor + kProfile2911.characterMesh).value_or(0);
    }
    if (mesh == 0) {
        return std::nullopt;
    }
    ++stats.meshPointers;

    const std::array<std::size_t, 4> transformOffsets{{
        kProfile2911.meshComponentSpaceTransforms,
        kProfile2911.meshComponentSpaceTransforms + 0x10,
        0x7A8,
        0x820
    }};
    std::optional<PointerArray> boneArray;
    std::vector<math::Transform> localTransforms;
    std::size_t transformOffsetIndex = 0;
    for (std::size_t index = 0; index < transformOffsets.size(); ++index) {
        const auto candidate = memory::readSafe<PointerArray>(mesh + transformOffsets[index]);
        if (!candidate) {
            continue;
        }
        ++stats.boneHeaders;
        std::vector<math::Transform> candidateTransforms;
        if (readComponentSpacePose(*candidate, candidateTransforms)) {
            boneArray = candidate;
            localTransforms = std::move(candidateTransforms);
            transformOffsetIndex = index;
            break;
        }
    }
    if (!boneArray) {
        return std::nullopt;
    }
    ++stats.boneArrays;
    ++stats.transformOffsetHits[transformOffsetIndex];

    const auto componentToWorld = memory::readSafe<math::Transform>(
        mesh + kProfile2911.meshComponentToWorld);
    if (!componentToWorld || !math::isFinite(componentToWorld->translation) ||
        !math::isFinite(componentToWorld->scale)) {
        return std::nullopt;
    }
    ++stats.componentTransforms;

    CharacterSnapshot result;
    result.actor = actor;
    result.bones.reserve(static_cast<std::size_t>(boneArray->count));
    for (const math::Transform& local : localTransforms) {
        const math::Vec3 world = componentToWorld->transformPosition(local.translation);
        if (!math::isFinite(world)) {
            return std::nullopt;
        }
        result.bones.push_back(world);
    }

    // UE4.26: USkinnedMeshComponent::SkeletalMesh at +0x550 and
    // USkeletalMesh::RefSkeleton.RawRefBoneInfo at +0x1C0. The latter stores
    // 0x0C-byte FMeshBoneInfo records (FName + ParentIndex). Training targets
    // use a different rig, so preserve their actual parent tree.
    const std::uintptr_t skeletalAsset = memory::readSafe<std::uintptr_t>(
        mesh + 0x550).value_or(0);
    const auto refBones = skeletalAsset != 0
        ? memory::readSafe<PointerArray>(skeletalAsset + 0x1C0)
        : std::nullopt;
    if (useRuntimeHierarchy && refBones &&
        refBones->count > 1 && refBones->count <= 512 &&
        refBones->capacity >= refBones->count && refBones->capacity <= 1024) {
        std::vector<MeshBoneInfo> hierarchy(
            static_cast<std::size_t>(refBones->count));
        if (memory::readBytesSafe(
                refBones->data, hierarchy.data(),
                hierarchy.size() * sizeof(MeshBoneInfo))) {
            const std::size_t usableCount = std::min(
                hierarchy.size(), result.bones.size());
            const bool matchesKnownRig = std::all_of(
                kDefaultSkeletonConnections.begin(),
                kDefaultSkeletonConnections.end(),
                [&hierarchy, usableCount](const auto& connection) {
                    const auto [parent, child] = connection;
                    return parent >= 0 && child >= 0 &&
                        static_cast<std::size_t>(child) < usableCount &&
                        hierarchy[static_cast<std::size_t>(child)].parentIndex == parent;
                });
            if (!matchesKnownRig) {
                result.connections.reserve(usableCount - 1);
                for (std::size_t child = 1; child < usableCount; ++child) {
                    const std::int32_t parent = hierarchy[child].parentIndex;
                    if (parent >= 0 && static_cast<std::size_t>(parent) < usableCount &&
                        static_cast<std::size_t>(parent) != child) {
                        result.connections.emplace_back(
                            static_cast<int>(parent), static_cast<int>(child));
                    }
                }
            }
        }
    }
    ++stats.captured;
    return result;
}

std::optional<CharacterSnapshot> captureCharacter(
    std::uintptr_t actor,
    CaptureStats& stats
) {
    return captureCharacter(actor, 0, stats);
}

bool looksLikePoseMesh(
    std::uintptr_t mesh,
    std::int32_t& boneCount,
    bool& hasPoseSignature
) {
    hasPoseSignature = false;
    if (mesh < 0x10000) {
        return false;
    }
    const std::array<std::size_t, 4> transformOffsets{{
        kProfile2911.meshComponentSpaceTransforms,
        kProfile2911.meshComponentSpaceTransforms + 0x10,
        0x7A8,
        // IDA: property registration adjacent to CachedBoneSpaceTransforms;
        // descriptor encodes CachedComponentSpaceTransforms offset 0x820.
        0x820
    }};
    std::optional<PointerArray> pose;
    for (const std::size_t offset : transformOffsets) {
        const auto candidate = memory::readSafe<PointerArray>(mesh + offset);
        // Human rigs in this build have 88 bones. The wider range also accepts
        // the reduced training-dummy rig without matching ordinary components.
        std::vector<math::Transform> transforms;
        if (candidate && candidate->count >= 65 && candidate->count <= 160 &&
            candidate->capacity >= candidate->count && candidate->capacity <= 320 &&
            readComponentSpacePose(*candidate, transforms)) {
            pose = candidate;
            break;
        }
    }
    if (!pose) {
        return false;
    }
    hasPoseSignature = true;
    const auto componentToWorld = memory::readSafe<math::Transform>(
        mesh + kProfile2911.meshComponentToWorld);
    if (!componentToWorld || !math::isFinite(componentToWorld->translation) ||
        !math::isFinite(componentToWorld->scale)) {
        return false;
    }
    const float quaternionNorm =
        componentToWorld->rotation.x * componentToWorld->rotation.x +
        componentToWorld->rotation.y * componentToWorld->rotation.y +
        componentToWorld->rotation.z * componentToWorld->rotation.z +
        componentToWorld->rotation.w * componentToWorld->rotation.w;
    const auto saneScale = [](float value) {
        const float magnitude = std::abs(value);
        return magnitude >= 0.01F && magnitude <= 100.0F;
    };
    if (!std::isfinite(quaternionNorm) || quaternionNorm < 0.5F ||
        quaternionNorm > 1.5F ||
        !saneScale(componentToWorld->scale.x) ||
        !saneScale(componentToWorld->scale.y) ||
        !saneScale(componentToWorld->scale.z)) {
        return false;
    }

    const std::uintptr_t skeletalAsset = memory::readSafe<std::uintptr_t>(
        mesh + 0x550).value_or(0);
    const auto refBones = skeletalAsset != 0
        ? memory::readSafe<PointerArray>(skeletalAsset + 0x1C0)
        : std::nullopt;
    if (!refBones || refBones->count != pose->count ||
        refBones->capacity < refBones->count || refBones->capacity > 1024) {
        return false;
    }
    std::vector<MeshBoneInfo> hierarchy(static_cast<std::size_t>(refBones->count));
    if (!memory::readBytesSafe(
            refBones->data, hierarchy.data(), hierarchy.size() * sizeof(MeshBoneInfo)) ||
        hierarchy.empty() || hierarchy.front().parentIndex != -1) {
        return false;
    }
    for (std::size_t child = 1; child < hierarchy.size(); ++child) {
        if (hierarchy[child].parentIndex < 0 ||
            static_cast<std::size_t>(hierarchy[child].parentIndex) >= child) {
            return false;
        }
    }
    boneCount = pose->count;
    return true;
}

std::vector<NpcMeshCandidate> discoverNpcCharacters(
    std::uintptr_t world,
    std::uintptr_t localPawn,
    std::optional<std::int32_t> localTeam
) {
    std::vector<std::uintptr_t> levels;
    std::unordered_set<std::uintptr_t> seenLevels;
    const auto addLevel = [&](std::uintptr_t level) {
        if (level != 0 && seenLevels.insert(level).second) {
            levels.push_back(level);
        }
    };
    addLevel(memory::readSafe<std::uintptr_t>(
        world + kProfile2911.worldPersistentLevel).value_or(0));
    if (const auto levelArray = memory::readSafe<PointerArray>(
            world + kProfile2911.worldLevels);
        levelArray && levelArray->count > 0 && levelArray->count <= 512 &&
        levelArray->capacity >= levelArray->count && levelArray->capacity <= 1024) {
        std::vector<std::uintptr_t> streamingLevels(
            static_cast<std::size_t>(levelArray->count));
        if (memory::readBytesSafe(
                levelArray->data,
                streamingLevels.data(),
                streamingLevels.size() * sizeof(std::uintptr_t))) {
            for (const std::uintptr_t level : streamingLevels) {
                addLevel(level);
            }
        }
    }

    std::vector<NpcMeshCandidate> result;
    std::unordered_set<std::uintptr_t> seenActors;
    std::unordered_set<std::uintptr_t> seenMeshes;
    std::size_t actorCount = 0;
    std::size_t componentPointers = 0;
    std::size_t componentArrays = 0;
    constexpr std::size_t actorProbeSize = 0x800;
    std::array<std::byte, actorProbeSize> actorBytes{};
    for (const std::uintptr_t level : levels) {
        const auto actors = memory::readSafe<PointerArray>(
            level + kProfile2911.levelActorArray);
        if (!actors || actors->count <= 0 || actors->count > 65536 ||
            actors->capacity < actors->count || actors->capacity > 131072) {
            continue;
        }
        std::vector<std::uintptr_t> snapshot(static_cast<std::size_t>(actors->count));
        if (!memory::readBytesSafe(
                actors->data,
                snapshot.data(),
                snapshot.size() * sizeof(std::uintptr_t))) {
            continue;
        }
        actorCount += snapshot.size();
        for (const std::uintptr_t actor : snapshot) {
            if (actor == 0 || actor == localPawn || !seenActors.insert(actor).second) {
                continue;
            }
            if (!memory::readBytesSafe(actor, actorBytes.data(), actorBytes.size())) {
                continue;
            }

            bool foundForActor = false;
            bool sawPoseObject = false;
            std::vector<std::pair<std::size_t, std::uintptr_t>> actorPointers;
            const auto acceptMesh = [&](std::uintptr_t mesh, std::size_t sourceOffset,
                                        const char* sourceKind) {
                if (mesh == 0 || !seenMeshes.insert(mesh).second) {
                    return false;
                }
                ++componentPointers;
                std::int32_t boneCount = 0;
                bool hasPoseSignature = false;
                if (!looksLikePoseMesh(mesh, boneCount, hasPoseSignature)) {
                    if (hasPoseSignature) {
                        sawPoseObject = true;
                    }
                    return false;
                }
                const auto state = memory::readSafe<std::uintptr_t>(
                    actor + kProfile2911.pawnPlayerState);
                const auto team = state && *state != 0
                    ? memory::readSafe<std::int32_t>(
                        *state + kProfile2911.playerStateCampGroup)
                    : std::nullopt;
                if (localTeam && team && *team == *localTeam) {
                    return false;
                }
                result.push_back({actor, mesh});
                VALOADER_LOG_INFO(
                    "NPC pose mesh: actor=%p mesh=%p source=%s+0x%zx bones=%d",
                    reinterpret_cast<void*>(actor), reinterpret_cast<void*>(mesh),
                    sourceKind, sourceOffset, boneCount);
                return true;
            };

            // Fast path for the CodeV shooting-range target wrapper. This was
            // resolved from a live actor graph and avoids a depth-2 search for
            // every target on each cache refresh.
            std::uintptr_t trainingHolder{};
            std::memcpy(&trainingHolder,
                actorBytes.data() + kProfile2911.trainingNpcHolder,
                sizeof(trainingHolder));
            const std::uintptr_t trainingMesh = trainingHolder >= 0x10000
                ? memory::readSafe<std::uintptr_t>(
                    trainingHolder + kProfile2911.trainingNpcVisibleMesh).value_or(0)
                : 0;
            if (acceptMesh(
                    trainingMesh, kProfile2911.trainingNpcHolder,
                    "training-holder")) {
                continue;
            }

            // First try every direct UObject pointer in the actor. This covers
            // native fields such as ACharacter::Mesh and custom CodeV pawns.
            for (std::size_t offset = 0x100;
                 offset + sizeof(std::uintptr_t) <= actorBytes.size();
                 offset += alignof(std::uintptr_t)) {
                std::uintptr_t pointer{};
                std::memcpy(&pointer, actorBytes.data() + offset, sizeof(pointer));
                if (pointer >= 0x10000) {
                    actorPointers.emplace_back(offset, pointer);
                }
                if (acceptMesh(pointer, offset, "field")) {
                    foundForActor = true;
                    break;
                }
            }
            if (foundForActor) {
                continue;
            }

            // CodeV training targets wrap the visible 88-bone component in a
            // custom holder (observed: actor+0x780 -> holder+0x760). Only walk
            // this second level when the actor already exposed a pose-like
            // object, keeping the map-wide scan bounded.
            if (sawPoseObject) {
                std::array<std::byte, actorProbeSize> nestedBytes{};
                for (const auto& [ownerOffset, owner] : actorPointers) {
                    if (!memory::readBytesSafe(
                            owner, nestedBytes.data(), nestedBytes.size())) {
                        continue;
                    }
                    for (std::size_t nestedOffset = 0x100;
                         nestedOffset + sizeof(std::uintptr_t) <= nestedBytes.size();
                         nestedOffset += alignof(std::uintptr_t)) {
                        std::uintptr_t component{};
                        std::memcpy(&component,
                            nestedBytes.data() + nestedOffset, sizeof(component));
                        if (acceptMesh(component, ownerOffset, "nested")) {
                            foundForActor = true;
                            break;
                        }
                    }
                    if (foundForActor) {
                        break;
                    }
                }
            }
            if (foundForActor) {
                continue;
            }

            // Blueprint actors commonly own their mesh through TArray/TSet
            // component collections. Probe the first pointer of each element;
            // the stride variants cover TArray<UObject*> and UE4 TSet slots.
            for (std::size_t offset = 0x100;
                 offset + sizeof(PointerArray) <= actorBytes.size();
                 offset += alignof(std::uintptr_t)) {
                PointerArray collection{};
                std::memcpy(&collection, actorBytes.data() + offset, sizeof(collection));
                if (collection.data < 0x10000 || collection.count <= 0 ||
                    collection.count > 128 || collection.capacity < collection.count ||
                    collection.capacity > 256) {
                    continue;
                }
                ++componentArrays;
                for (const std::size_t stride : {8U, 16U, 24U, 32U}) {
                    const std::size_t byteCount =
                        static_cast<std::size_t>(collection.count) * stride;
                    std::vector<std::byte> elements(byteCount);
                    if (!memory::readBytesSafe(
                            collection.data, elements.data(), elements.size())) {
                        continue;
                    }
                    for (std::int32_t index = 0; index < collection.count; ++index) {
                        std::uintptr_t component{};
                        std::memcpy(&component,
                            elements.data() + static_cast<std::size_t>(index) * stride,
                            sizeof(component));
                        if (acceptMesh(component, offset, "collection")) {
                            foundForActor = true;
                            break;
                        }
                    }
                    if (foundForActor) {
                        break;
                    }
                }
                if (foundForActor) {
                    break;
                }
            }
            if (result.size() >= 128) {
                break;
            }
        }
        if (result.size() >= 128) {
            break;
        }
    }
    VALOADER_LOG_INFO(
        "NPC actor scan: levels=%zu actors=%zu pointers=%zu arrays=%zu poseMeshes=%zu",
        levels.size(), actorCount, componentPointers, componentArrays, result.size());
    return result;
}

} // namespace

FrameSnapshot UnrealRuntime::capture() {
    FrameSnapshot frame;
    frame.moduleBase = memory::moduleBase("libUE4.so");
    if (frame.moduleBase == 0) {
        frame.status = "Waiting for libUE4.so";
        return frame;
    }
    if (!reflectionReady_) {
        const auto now = std::chrono::steady_clock::now();
        if (discoveryFuture_.valid() &&
            discoveryFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const bool discovered = discoveryFuture_.get();
            reflectionReady_ = discovered;
            discoveryStatus_ = discovered ? "Reflection ready" : "Reflection discovery retry";
            VALOADER_LOG_INFO("UE reflection discovery: %s", discovered ? "ready" : "not found");
            nextDiscoveryAttempt_ = now + std::chrono::seconds(discovered ? 60 : 5);
        }
        if (!reflectionReady_ && !discoveryFuture_.valid() && now >= nextDiscoveryAttempt_) {
            discoveryStatus_ = "Reflection discovery is running";
            const std::uintptr_t moduleBase = frame.moduleBase;
            discoveryFuture_ = std::async(std::launch::async, [this, moduleBase] {
                const auto started = std::chrono::steady_clock::now();
                const bool result = reflection_.discover(moduleBase);
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                VALOADER_LOG_INFO("UE reflection background scan finished in %lld ms",
                    static_cast<long long>(elapsed));
                return result;
            });
        }
        if (!reflectionReady_) {
            frame.status = discoveryStatus_;
            return frame;
        }
    }

    const std::uintptr_t world = reflection_.world();
    if (world == 0) {
        frame.status = "GWorld is not available";
        return frame;
    }
    frame.world = world;

    const auto gameInstance = memory::read<std::uintptr_t>(world + kProfile2911.worldGameInstance);
    const auto localPlayers = gameInstance
        ? memory::read<PointerArray>(*gameInstance + kProfile2911.gameInstanceLocalPlayers)
        : std::nullopt;
    const auto localPlayer = localPlayers && localPlayers->count > 0
        ? memory::read<std::uintptr_t>(localPlayers->data)
        : std::nullopt;
    const auto controller = localPlayer
        ? memory::read<std::uintptr_t>(*localPlayer + kProfile2911.localPlayerController)
        : std::nullopt;
    const auto localPawn = controller
        ? memory::read<std::uintptr_t>(*controller + kProfile2911.controllerAcknowledgedPawn)
        : std::nullopt;
    const auto cameraManager = controller
        ? memory::read<std::uintptr_t>(*controller + kProfile2911.controllerCameraManager)
        : std::nullopt;
    if (!cameraManager || *cameraManager == 0) {
        frame.status = "Player camera is not available";
        return frame;
    }

    const std::uintptr_t view = *cameraManager + kProfile2911.cameraManagerCameraCache +
        kProfile2911.cameraCachePov;
    const auto location = memory::read<math::Vec3>(view + kProfile2911.cameraPovLocation);
    const auto rotation = memory::read<math::Vec3>(view + kProfile2911.cameraPovRotation);
    const auto fieldOfView = memory::read<float>(view + kProfile2911.cameraPovFov);
    if (!location || !rotation || !fieldOfView || !math::isFinite(*location) || !math::isFinite(*rotation)) {
        frame.status = "Camera cache is invalid";
        return frame;
    }
    frame.camera = {*location, *rotation, *fieldOfView};

    if (!localPawn || *localPawn == 0) {
        frame.status = "Local pawn is not available";
        return frame;
    }
    CaptureStats captureStats;
    CaptureStats localCaptureStats;
    const auto localCharacterProbe = captureCharacter(*localPawn, localCaptureStats);
    const auto localPlayerState = memory::readSafe<std::uintptr_t>(
        *localPawn + kProfile2911.pawnPlayerState);
    const auto localTeam = localPlayerState
        ? memory::readSafe<std::int32_t>(
            *localPlayerState + kProfile2911.playerStateCampGroup)
        : std::nullopt;
    const auto gameState = memory::readSafe<std::uintptr_t>(
        world + kProfile2911.worldGameState);
    const auto playerArray = gameState
        ? memory::readSafe<PointerArray>(*gameState + kProfile2911.gameStatePlayerArray)
        : std::nullopt;
    if (!localPlayerState || !localTeam || !gameState || !playerArray ||
        playerArray->count <= 0 || playerArray->count > 128 ||
        playerArray->capacity < playerArray->count || playerArray->capacity > 256) {
        frame.status = "PlayerArray is unavailable";
        return frame;
    }

    std::vector<std::uintptr_t> playerStates(
        static_cast<std::size_t>(playerArray->count));
    if (!memory::readBytesSafe(
            playerArray->data, playerStates.data(),
            playerStates.size() * sizeof(std::uintptr_t))) {
        frame.status = "PlayerArray snapshot is unavailable";
        return frame;
    }
    captureStats.actors = playerStates.size();
    frame.characters.reserve(playerStates.size());
    for (const std::uintptr_t playerState : playerStates) {
        if (playerState == 0) {
            continue;
        }
        ++captureStats.nonNullActors;
        const auto team = memory::readSafe<std::int32_t>(
            playerState + kProfile2911.playerStateCampGroup);
        const auto lifeStatus = memory::readSafe<std::uint8_t>(
            playerState + kProfile2911.playerStateLifeStatus);
        const auto character = memory::readSafe<std::uintptr_t>(
            playerState + kProfile2911.playerStateControlledViewTarget);
        if (!team || !lifeStatus || *lifeStatus != 0 || !character ||
            *character == 0 || *character == *localPawn || *team == *localTeam) {
            continue;
        }
        if (auto snapshot = captureCharacter(*character, captureStats)) {
            frame.characters.push_back(std::move(*snapshot));
        }
    }
    const auto npcNow = std::chrono::steady_clock::now();
    if (frame.characters.empty() && npcNow >= nextNpcScan_) {
        cachedNpcCandidates_ = discoverNpcCharacters(world, *localPawn, localTeam);
        nextNpcScan_ = npcNow + std::chrono::seconds(5);
    } else if (!frame.characters.empty()) {
        cachedNpcCandidates_.clear();
    }
    for (const NpcMeshCandidate& candidate : cachedNpcCandidates_) {
        const std::uintptr_t character = candidate.actor;
        if (character == 0 || candidate.mesh == 0 || character == *localPawn) {
            continue;
        }
        const auto state = memory::readSafe<std::uintptr_t>(
            character + kProfile2911.pawnPlayerState);
        const auto team = state && *state != 0
            ? memory::readSafe<std::int32_t>(
                *state + kProfile2911.playerStateCampGroup)
            : std::nullopt;
        if (localTeam && team && *team == *localTeam) {
            continue;
        }
        if (auto snapshot = captureCharacter(character, candidate.mesh, captureStats)) {
            frame.characters.push_back(std::move(*snapshot));
        }
    }
    frame.status = "Ready";
    static std::size_t lastCount = static_cast<std::size_t>(-1);
    if (frame.characters.size() != lastCount) {
        lastCount = frame.characters.size();
        VALOADER_LOG_INFO(
            "Skeleton capture: %zu enemy meshes (PlayerArray team filtered)",
            frame.characters.size());
    }
    static auto nextStatsLog = std::chrono::steady_clock::time_point{};
    const auto statsNow = std::chrono::steady_clock::now();
    if (statsNow >= nextStatsLog) {
        nextStatsLog = statsNow + std::chrono::seconds(5);
        VALOADER_LOG_INFO(
            "Skeleton stages: actors=%zu nonNull=%zu mesh=%zu headers=%zu arrays=%zu ctw=%zu captured=%zu offsets[580=%zu,590=%zu,7a8=%zu,820=%zu]",
            captureStats.actors, captureStats.nonNullActors, captureStats.meshPointers,
            captureStats.boneHeaders, captureStats.boneArrays,
            captureStats.componentTransforms, captureStats.captured,
            captureStats.transformOffsetHits[0], captureStats.transformOffsetHits[1],
            captureStats.transformOffsetHits[2],
            captureStats.transformOffsetHits[3]);
        VALOADER_LOG_INFO(
            "Local pawn stages: pawn=%p mesh=%zu headers=%zu arrays=%zu ctw=%zu captured=%d offsets[580=%zu,590=%zu,7a8=%zu,820=%zu]",
            reinterpret_cast<void*>(*localPawn), localCaptureStats.meshPointers,
            localCaptureStats.boneHeaders, localCaptureStats.boneArrays,
            localCaptureStats.componentTransforms, localCharacterProbe ? 1 : 0,
            localCaptureStats.transformOffsetHits[0],
            localCaptureStats.transformOffsetHits[1],
            localCaptureStats.transformOffsetHits[2],
            localCaptureStats.transformOffsetHits[3]);
    }
    return frame;
}

} // namespace valoader::game
