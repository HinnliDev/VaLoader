#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace valoader::game {

struct VersionProfile {
    std::uint64_t versionCode;
    std::uintptr_t gWorld;
    std::uintptr_t gEngine;
    std::size_t worldPersistentLevel;
    std::size_t worldGameInstance;
    std::size_t worldLevels;
    std::size_t worldGameState;
    std::size_t levelActorArray;
    std::size_t engineGameViewport;
    std::size_t viewportWorld;
    std::size_t viewportGameInstance;
    std::size_t gameInstanceLocalPlayers;
    std::size_t localPlayerController;
    std::size_t localPlayerViewportClient;
    std::size_t controllerAcknowledgedPawn;
    std::size_t controllerCameraManager;
    std::size_t pawnPlayerState;
    std::size_t gameStatePlayerArray;
    std::size_t playerStateCampGroup;
    std::size_t playerStateControlledViewTarget;
    std::size_t playerStateLifeStatus;
    std::size_t cameraManagerCameraCache;
    std::size_t cameraCachePov;
    std::size_t cameraPovLocation;
    std::size_t cameraPovRotation;
    std::size_t cameraPovFov;
    std::size_t actorRootComponent;
    std::size_t characterMesh;
    std::size_t sceneComponentLocation;
    std::size_t meshComponentToWorld;
    std::size_t meshComponentSpaceTransforms;
    std::size_t trainingNpcHolder;
    std::size_t trainingNpcVisibleMesh;
};

// Valorant Mobile 1.24.0, versionCode 2911, UE4 4.26.1, arm64-v8a.
// Rediscovery in IDA: search the strings "PersistentLevel", "AcknowledgedPawn",
// "CachedBoneSpaceTransforms" and "ProjectWorldLocationToScreen". Follow their
// xrefs to the generated reflection registration functions. GWorld must then be
// confirmed by the chain UWorld -> PersistentLevel -> Actors while the match is
// running. Never reuse this profile after a game update without that validation.
inline constexpr VersionProfile kProfile2911{
    .versionCode = 2911,
    .gWorld = 0,
    // Runtime-validated in CodeV 1.24.0: the slot resolves to UGameEngine and
    // closes the GameViewport/World/GameInstance/LocalPlayer graph below.
    .gEngine = 0xB422BE0,
    .worldPersistentLevel = 0x30,
    // Confirmed from Z_Construct_UClass_UWorld_Statics: the property at 0x220
    // references Z_Construct_UClass_UGameInstance_NoRegister (0x8F68F40).
    .worldGameInstance = 0x220,
    // UWorld::Levels generated array property (streaming sublevels included).
    .worldLevels = 0x1D8,
    .worldGameState = 0x1C0,
    .levelActorArray = 0x98,
    // UGameEngine::GameViewport has an encrypted property name in CodeV, but
    // its generated property metadata references UGameViewportClient and
    // records offset 0x7D8. UGameViewportClient metadata exposes World=0x70
    // and GameInstance=0x78.
    .engineGameViewport = 0x7D8,
    .viewportWorld = 0x70,
    .viewportGameInstance = 0x78,
    .gameInstanceLocalPlayers = 0x38,
    .localPlayerController = 0x30,
    // ULocalPlayer::ViewportClient, confirmed by generated metadata.
    .localPlayerViewportClient = 0x70,
    .controllerAcknowledgedPawn = 0x348,
    .controllerCameraManager = 0x360,
    // Live pointer match against GameState::PlayerArray. The same pointer is
    // mirrored by CodeV at +0x768/+0x8E8, but +0x2E8 is APawn::PlayerState.
    .pawnPlayerState = 0x2E8,
    .gameStatePlayerArray = 0x2E0,
    // CodeVPlayerState::CampGroup. IDA metadata at libUE4+0xA7706E8 encodes
    // +0x4C0. Live 5v5 validation splits the ten states into two exact groups
    // (local player and allies=1, enemies=2). TeamID at +0x4B4 is zero here.
    .playerStateCampGroup = 0x4C0,
    // CodeVPlayerState::PossessedCharacter at +0x4D8 is a TArray header,
    // not an ACharacter pointer. The following reflected object property is
    // ControlledViewTarget at +0x4E8. Live validation: for the local state it
    // exactly equals APlayerController::AcknowledgedPawn; remote states use it
    // to expose their currently controlled character.
    // IDA rediscovery: ControlledViewTarget metadata at libUE4+0xA770818.
    .playerStateControlledViewTarget = 0x4E8,
    // CodeVPlayerState::CharacterLifeTime at +0x520 embeds
    // SGCharacterLifeTime::LifeStatus at offset zero. Live pawns report zero;
    // +0x524 belongs to the following CharacterLifeTimeForReplay property.
    .playerStateLifeStatus = 0x520,
    // PlayerCameraManager metadata: CameraCachePrivate=0x1BD0 and
    // LastFrameCameraCachePrivate=0x21E0 (FCameraCacheEntry size 0x610).
    .cameraManagerCameraCache = 0x1BD0,
    // CodeV extends UE4's FMinimalViewInfo with LocationExcludeOffset at
    // +0x0C. Rotation and FOV therefore do not use the stock UE4 offsets.
    // Rediscovery: generated MinimalViewInfo property metadata near the
    // "MinimalViewInfo" registration in libUE4.so.
    .cameraCachePov = 0x10,
    .cameraPovLocation = 0x00,
    .cameraPovRotation = 0x18,
    .cameraPovFov = 0x24,
    .actorRootComponent = 0x198,
    // ACharacter::Mesh. Confirmed by its 88-element component transform
    // buffers and the linked 88-entry RefSkeleton.
    .characterMesh = 0x328,
    .sceneComponentLocation = 0x1A0,
    .meshComponentToWorld = 0x1E0,
    // USkinnedMeshComponent double-buffers component-space poses at
    // +0x580/+0x590. Both contain the same 88-bone RefSkeleton layout.
    // CodeV's reflected CachedComponentSpaceTransforms descriptor additionally
    // encodes +0x820 (IDA: strings xref table at libUE4+0xAD08780).
    .meshComponentSpaceTransforms = 0x580,
    // Runtime pointer-graph validation on the 1.24.0 shooting-range targets:
    // Actor+0x780 -> CodeV holder; holder+0x760 -> visible 88-bone
    // USkeletalMeshComponent. The direct actor pose object is not renderable.
    .trainingNpcHolder = 0x780,
    .trainingNpcVisibleMesh = 0x760,
};

// Read from USkeletalMesh::RefSkeleton (asset +0x1C0, FMeshBoneInfo::ParentIndex
// at +0x08) for the active 88-bone player rig. Finger/face/IK branches are
// intentionally omitted from the ESP silhouette.
inline constexpr std::array<std::pair<int, int>, 22> kDefaultSkeletonConnections{{
    {8, 7}, {7, 6}, {6, 5}, {5, 4}, {4, 3}, {3, 2},
    {6, 20}, {20, 21}, {21, 22}, {22, 23},
    {6, 43}, {43, 44}, {44, 45}, {45, 46},
    {2, 68}, {68, 69}, {69, 70}, {70, 71},
    {2, 72}, {72, 73}, {73, 74}, {74, 75},
}};

} // namespace valoader::game
