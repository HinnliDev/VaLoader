#include "game/UnrealReflection.hpp"

#include "core/Log.hpp"
#include "core/Memory.hpp"
#include "core/Math.hpp"
#include "game/VersionProfile.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace valoader::game {
namespace {

constexpr std::size_t kObjectClass = 0x10;
constexpr std::size_t kObjectName = 0x18;
constexpr std::size_t kStructSuper = 0x40;
constexpr std::size_t kStructChildProperties = 0x50;
constexpr std::size_t kFieldClass = 0x08;
constexpr std::size_t kFieldNext = 0x20;
constexpr std::size_t kFieldName = 0x28;
constexpr std::size_t kPropertyOffset = 0x4C;
constexpr std::size_t kBoolPropertyByteMask = 0x7A;
constexpr std::size_t kWorldPersistentLevel = 0x30;

// CodeV 1.24.0 (2911), rediscovery notes:
// - xref of "PersistentLevel" -> 0x67BBFB4;
// - its registration sequence calls hash(name, length) at 0xA0D2170 and then
//   name lookup at 0x67BD320(pool, name, hash);
// - the pool constructor starts at 0x67BABC8 and writes self pointers at the
//   offsets below. These checks intentionally use several independent fields.
constexpr std::uintptr_t kCodeVNameHash = 0xA0D2170;
constexpr std::uintptr_t kCodeVNameLookup = 0x67BD320;

bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() && std::equal(
        left.begin(), left.end(), right.begin(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                std::tolower(static_cast<unsigned char>(b));
        });
}

bool isPlausibleName(std::string_view value) noexcept {
    if (value.empty() || value.size() > 256) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return byte >= 0x20 && byte < 0x7F;
    });
}

bool readableIn(const std::vector<memory::Range>& ranges,
                std::uintptr_t address,
                std::size_t size) noexcept {
    if (address < 0x10000 || address > UINTPTR_MAX - size) {
        return false;
    }
    const std::uintptr_t end = address + size;
    const auto after = std::upper_bound(
        ranges.begin(), ranges.end(), address,
        [](std::uintptr_t value, const memory::Range& range) {
            return value < range.begin;
        });
    if (after == ranges.begin()) {
        return false;
    }
    const memory::Range& range = *std::prev(after);
    return range.readable && address >= range.begin && end <= range.end;
}

} // namespace

bool UnrealReflection::discover(std::uintptr_t moduleBase) {
    if (moduleBase == 0) {
        return false;
    }
    if (moduleBase_ != moduleBase) {
        moduleBase_ = moduleBase;
        nameBlocks_ = 0;
        codeVNamePool_ = 0;
        worldSlot_ = 0;
        engineSlot_ = 0;
        levelActorArrayOffset_ = 0;
        nameLengthShift_ = 0;
        nameIndexCache_.clear();
        propertyCache_.clear();
    }
    if ((worldSlot_ != 0 || engineSlot_ != 0) && world() == 0) {
        const std::uintptr_t staleSlot = engineSlot_ != 0 ? engineSlot_ : worldSlot_;
        VALOADER_LOG_INFO("Discarding stale world source at libUE4+0x%zx",
            static_cast<std::size_t>(staleSlot - moduleBase_));
        worldSlot_ = 0;
        engineSlot_ = 0;
        levelActorArrayOffset_ = 0;
    }
    if (worldSlot_ == 0 && engineSlot_ == 0) {
        (void)discoverWorldStructurally();
    }
    // CodeV uses a protected FName implementation. Name discovery is not part
    // of the launch-time path: a failed stock-pool scan is expensive on a live
    // map and does not help locate UWorld/ULevel.
    return ready();
}

bool UnrealReflection::ready() const noexcept {
    return world() != 0;
}

bool UnrealReflection::namesReady() const noexcept {
    return hasNames();
}

bool UnrealReflection::discoverWorldStructurally() {
    struct PointerArray {
        std::uintptr_t data{};
        std::int32_t count{};
        std::int32_t capacity{};
    };

    const std::vector<memory::Range> allRanges = memory::ranges();
    const std::vector<memory::Range> ueRanges = memory::moduleRanges("libUE4.so");
    const auto readable = [&allRanges](std::uintptr_t address, std::size_t size) {
        return readableIn(allRanges, address, size);
    };
    const auto validArray = [&readable](const PointerArray& array, int maximum) {
        return array.count > 0 && array.count <= maximum &&
            array.capacity >= array.count && array.capacity <= maximum * 2 &&
            readable(array.data, static_cast<std::size_t>(array.count) * sizeof(std::uintptr_t));
    };
    const auto validCamera = [](std::uintptr_t manager) {
        const std::uintptr_t view = manager + kProfile2911.cameraManagerCameraCache +
            kProfile2911.cameraCachePov;
        const auto location = memory::readSafe<math::Vec3>(
            view + kProfile2911.cameraPovLocation);
        const auto rotation = memory::readSafe<math::Vec3>(
            view + kProfile2911.cameraPovRotation);
        const auto fov = memory::readSafe<float>(view + kProfile2911.cameraPovFov);
        return location && rotation && fov && math::isFinite(*location) &&
            math::isFinite(*rotation) && std::abs(location->x) < 100000000.0F &&
            std::abs(location->y) < 100000000.0F &&
            std::abs(location->z) < 100000000.0F &&
            std::abs(rotation->x) <= 180.0F &&
            std::abs(rotation->y) <= 720.0F &&
            std::abs(rotation->z) <= 180.0F && *fov >= 20.0F && *fov <= 180.0F;
    };
    struct ActorArrayMatch {
        PointerArray array{};
        std::size_t offset{};
    };
    const auto findActorArray = [&](std::uintptr_t level, std::size_t sourceOffset) {
        std::optional<ActorArrayMatch> best;
        int bestScore = -1;
        for (std::size_t offset = 0x28; offset <= 0x300; offset += 8) {
            const auto array = memory::readSafe<PointerArray>(level + offset);
            if (!array || !validArray(*array, 65536)) {
                continue;
            }
            const std::int32_t sampleCount = std::min<std::int32_t>(array->count, 32);
            int readableObjects = 0;
            int levelOwnedObjects = 0;
            for (std::int32_t sample = 0; sample < sampleCount; ++sample) {
                const std::int32_t index = sampleCount == array->count
                    ? sample
                    : static_cast<std::int32_t>(
                        (static_cast<std::int64_t>(sample) * array->count) / sampleCount);
                const auto object = memory::readSafe<std::uintptr_t>(
                    array->data + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t));
                if (!object || !readable(*object, 0x28)) {
                    continue;
                }
                const auto objectClass = memory::readSafe<std::uintptr_t>(*object + kObjectClass);
                if (!objectClass || !readable(*objectClass, 0x28)) {
                    continue;
                }
                ++readableObjects;
                const auto outer = memory::readSafe<std::uintptr_t>(*object + 0x20);
                if (outer && *outer == level) {
                    ++levelOwnedObjects;
                }
            }
            if (readableObjects >= 3) {
                VALOADER_LOG_INFO(
                    "ULevel array candidate: source=+0x%zx level=%p off=0x%zx count=%d readable=%d owned=%d",
                    sourceOffset, reinterpret_cast<void*>(level), offset, array->count,
                    readableObjects, levelOwnedObjects);
            }
            const int score = (offset == kProfile2911.levelActorArray ? 100000000 : 0) +
                levelOwnedObjects * 10000 + readableObjects * 100 +
                std::min(array->count, 99);
            if (readableObjects >= 8 && score > bestScore) {
                bestScore = score;
                best = ActorArrayMatch{*array, offset};
            }
        }
        return best;
    };
    const auto tryEngineSource = [&](std::uintptr_t slot,
                                     std::uintptr_t candidate,
                                     bool verbose) {
        if (!readable(candidate, kProfile2911.engineGameViewport + sizeof(std::uintptr_t))) {
            return false;
        }
        const auto viewport = memory::readSafe<std::uintptr_t>(
            candidate + kProfile2911.engineGameViewport);
        const auto viewportWorld = viewport
            ? memory::readSafe<std::uintptr_t>(*viewport + kProfile2911.viewportWorld)
            : std::nullopt;
        const auto viewportGameInstance = viewport
            ? memory::readSafe<std::uintptr_t>(
                *viewport + kProfile2911.viewportGameInstance)
            : std::nullopt;
        const auto worldGameInstance = viewportWorld
            ? memory::readSafe<std::uintptr_t>(
                *viewportWorld + kProfile2911.worldGameInstance)
            : std::nullopt;
        const auto localPlayers = viewportGameInstance
            ? memory::readSafe<PointerArray>(
                *viewportGameInstance + kProfile2911.gameInstanceLocalPlayers)
            : std::nullopt;
        if (verbose) {
            VALOADER_LOG_INFO(
                "Profile GEngine chain: engine=%p viewport=%p world=%p viewportGI=%p worldGI=%p localCount=%d",
                reinterpret_cast<void*>(candidate), reinterpret_cast<void*>(viewport.value_or(0)),
                reinterpret_cast<void*>(viewportWorld.value_or(0)),
                reinterpret_cast<void*>(viewportGameInstance.value_or(0)),
                reinterpret_cast<void*>(worldGameInstance.value_or(0)),
                localPlayers ? localPlayers->count : -1);
        }
        if (!viewport || !viewportWorld || !viewportGameInstance || !worldGameInstance ||
            !readable(*viewport, 0x100) || !readable(*viewportWorld, 0x300) ||
            !readable(*viewportGameInstance, 0x100) ||
            *worldGameInstance != *viewportGameInstance || !localPlayers ||
            !validArray(*localPlayers, 8)) {
            return false;
        }
        const auto localPlayer = memory::readSafe<std::uintptr_t>(localPlayers->data);
        const auto localViewport = localPlayer
            ? memory::readSafe<std::uintptr_t>(
                *localPlayer + kProfile2911.localPlayerViewportClient)
            : std::nullopt;
        const auto controller = localPlayer
            ? memory::readSafe<std::uintptr_t>(
                *localPlayer + kProfile2911.localPlayerController)
            : std::nullopt;
        const auto cameraManager = controller
            ? memory::readSafe<std::uintptr_t>(
                *controller + kProfile2911.controllerCameraManager)
            : std::nullopt;
        const auto level = memory::readSafe<std::uintptr_t>(
            *viewportWorld + kProfile2911.worldPersistentLevel);
        const bool cameraIsValid = cameraManager && readable(*cameraManager, 0x2800) &&
            validCamera(*cameraManager);
        if (verbose) {
            VALOADER_LOG_INFO(
                "Profile GEngine player: local=%p localViewport=%p controller=%p camera=%p level=%p cameraValid=%d",
                reinterpret_cast<void*>(localPlayer.value_or(0)),
                reinterpret_cast<void*>(localViewport.value_or(0)),
                reinterpret_cast<void*>(controller.value_or(0)),
                reinterpret_cast<void*>(cameraManager.value_or(0)),
                reinterpret_cast<void*>(level.value_or(0)), cameraIsValid ? 1 : 0);
        }
        if (!localPlayer || !localViewport || *localViewport != *viewport || !controller ||
            !cameraManager || !level || !readable(*localPlayer, 0x100) ||
            !readable(*controller, 0x500) || !cameraIsValid || !readable(*level, 0x180)) {
            return false;
        }
        auto actorMatch = findActorArray(
            *level, static_cast<std::size_t>(slot - moduleBase_));
        if (!actorMatch) {
            const auto levels = memory::readSafe<PointerArray>(
                *viewportWorld + kProfile2911.worldLevels);
            if (levels && validArray(*levels, 256)) {
                std::vector<std::uintptr_t> levelSnapshot(
                    static_cast<std::size_t>(levels->count));
                if (memory::readBytesSafe(
                        levels->data, levelSnapshot.data(),
                        levelSnapshot.size() * sizeof(std::uintptr_t))) {
                    for (const std::uintptr_t streamingLevel : levelSnapshot) {
                        if (streamingLevel == 0 || streamingLevel == *level ||
                            !readable(streamingLevel, 0x180)) {
                            continue;
                        }
                        actorMatch = findActorArray(
                            streamingLevel,
                            static_cast<std::size_t>(slot - moduleBase_));
                        if (actorMatch) {
                            VALOADER_LOG_INFO(
                                "Actors resolved from streaming ULevel=%p",
                                reinterpret_cast<void*>(streamingLevel));
                            break;
                        }
                    }
                }
            }
        }
        const auto stableEngine = memory::readSafe<std::uintptr_t>(slot);
        if (!actorMatch || !stableEngine || *stableEngine != candidate) {
            return false;
        }
        engineSlot_ = slot;
        worldSlot_ = 0;
        levelActorArrayOffset_ = actorMatch->offset;
        VALOADER_LOG_INFO(
            "GEngine viewport world discovered at libUE4+0x%zx world=%p actorsOff=0x%zx actors=%d",
            static_cast<std::size_t>(slot - moduleBase_),
            reinterpret_cast<void*>(*viewportWorld), actorMatch->offset,
            actorMatch->array.count);
        return true;
    };

    const std::uintptr_t profiledEngineSlot = moduleBase_ + kProfile2911.gEngine;
    const auto profiledEngine = memory::readSafe<std::uintptr_t>(profiledEngineSlot);
    if (profiledEngine && tryEngineSource(profiledEngineSlot, *profiledEngine, true)) {
        return true;
    }

    std::size_t readableCandidates = 0;
    std::size_t levelCandidates = 0;
    std::size_t outerCandidates = 0;
    std::size_t actorCandidates = 0;
    std::size_t gameInstanceCandidates = 0;
    std::size_t localPlayerCandidates = 0;
    std::size_t controllerCandidates = 0;
    std::size_t cameraManagerCandidates = 0;
    std::size_t playerCandidates = 0;
    std::size_t cameraCandidates = 0;
    for (const memory::Range& range : ueRanges) {
        if (!range.readable || !range.writable) {
            continue;
        }
        std::vector<std::uint8_t> globalSnapshot(range.end - range.begin);
        if (!memory::readBytesSafe(
                range.begin, globalSnapshot.data(), globalSnapshot.size())) {
            continue;
        }
        std::uintptr_t slot = (range.begin + 7U) & ~std::uintptr_t{7U};
        for (; slot + sizeof(std::uintptr_t) <= range.end; slot += sizeof(std::uintptr_t)) {
            std::uintptr_t candidate{};
            std::memcpy(
                &candidate,
                globalSnapshot.data() + (slot - range.begin),
                sizeof(candidate));
            if (!readable(candidate, 0x300)) {
                continue;
            }
            ++readableCandidates;

            // Preferred path: resolve UWorld from the stable GEngine viewport
            // graph. Every link is validated in both directions.
            if (tryEngineSource(slot, candidate, false)) {
                return true;
            }

            const auto levelValue = memory::readSafe<std::uintptr_t>(
                candidate + kProfile2911.worldPersistentLevel);
            if (!levelValue) {
                continue;
            }
            const std::uintptr_t level = *levelValue;
            if (!readable(level, 0x180)) {
                continue;
            }
            ++levelCandidates;
            const auto levelOuter = memory::readSafe<std::uintptr_t>(level + 0x20);
            const bool levelOuterMatches = levelOuter && *levelOuter == candidate;
            if (levelOuterMatches) {
                ++outerCandidates;
            }

            // Validate the unique local-player/camera chain before guessing an
            // unreflected ULevel::Actors offset. Some CodeV level variants do
            // not keep UWorld as PersistentLevel's direct Outer, so Outer is a
            // diagnostic signal rather than a correctness requirement here.
            const auto gameInstance = memory::readSafe<std::uintptr_t>(
                candidate + kProfile2911.worldGameInstance);
            const auto localPlayers = gameInstance
                ? memory::readSafe<PointerArray>(*gameInstance + kProfile2911.gameInstanceLocalPlayers)
                : std::nullopt;
            if (!gameInstance || !readable(*gameInstance, 0x100) || !localPlayers ||
                !validArray(*localPlayers, 8)) {
                continue;
            }
            ++gameInstanceCandidates;
            const auto localPlayer = memory::readSafe<std::uintptr_t>(localPlayers->data);
            if (!localPlayer || !readable(*localPlayer, 0x100)) {
                VALOADER_LOG_INFO("GWorld candidate has invalid LocalPlayer: slot=+0x%zx count=%d ptr=%p",
                    static_cast<std::size_t>(slot - moduleBase_), localPlayers->count,
                    reinterpret_cast<void*>(localPlayer.value_or(0)));
                continue;
            }
            ++localPlayerCandidates;
            const auto controller = localPlayer
                ? memory::readSafe<std::uintptr_t>(*localPlayer + kProfile2911.localPlayerController)
                : std::nullopt;
            if (!controller || !readable(*controller, 0x500)) {
                VALOADER_LOG_INFO("GWorld candidate has invalid PlayerController: lp=%p ptr=%p",
                    reinterpret_cast<void*>(*localPlayer),
                    reinterpret_cast<void*>(controller.value_or(0)));
                continue;
            }
            ++controllerCandidates;
            const auto cameraManager = controller
                ? memory::readSafe<std::uintptr_t>(*controller + kProfile2911.controllerCameraManager)
                : std::nullopt;
            if (!cameraManager || !readable(*cameraManager, 0x2800)) {
                VALOADER_LOG_INFO("GWorld candidate has invalid CameraManager: pc=%p ptr=%p",
                    reinterpret_cast<void*>(*controller),
                    reinterpret_cast<void*>(cameraManager.value_or(0)));
                continue;
            }
            ++cameraManagerCandidates;
            if (!validCamera(*cameraManager)) {
                const std::uintptr_t view = *cameraManager +
                    kProfile2911.cameraManagerCameraCache + kProfile2911.cameraCachePov;
                const auto location = memory::readSafe<math::Vec3>(
                    view + kProfile2911.cameraPovLocation);
                const auto rotation = memory::readSafe<math::Vec3>(
                    view + kProfile2911.cameraPovRotation);
                const auto fov = memory::readSafe<float>(
                    view + kProfile2911.cameraPovFov);
                VALOADER_LOG_INFO(
                    "GWorld camera cache invalid: mgr=%p cacheOff=0x%zx loc=(%.2f,%.2f,%.2f) rot=(%.2f,%.2f,%.2f) fov=%.2f",
                    reinterpret_cast<void*>(*cameraManager),
                    kProfile2911.cameraManagerCameraCache,
                    location ? location->x : 0.0F, location ? location->y : 0.0F,
                    location ? location->z : 0.0F, rotation ? rotation->x : 0.0F,
                    rotation ? rotation->y : 0.0F, rotation ? rotation->z : 0.0F,
                    fov.value_or(0.0F));
                continue;
            }
            ++playerCandidates;
            ++cameraCandidates;

            std::optional<PointerArray> actors;
            std::size_t actorsOffset = 0;
            int bestScore = -1;
            for (std::size_t offset = 0x28; offset <= 0x300; offset += 8) {
                const auto array = memory::readSafe<PointerArray>(level + offset);
                if (!array || !validArray(*array, 65536)) {
                    continue;
                }
                const std::int32_t sampleCount = std::min<std::int32_t>(array->count, 32);
                int readableObjects = 0;
                int levelOwnedObjects = 0;
                for (std::int32_t sample = 0; sample < sampleCount; ++sample) {
                    const std::int32_t index = sampleCount == array->count
                        ? sample
                        : static_cast<std::int32_t>(
                            (static_cast<std::int64_t>(sample) * array->count) / sampleCount);
                    const auto object = memory::readSafe<std::uintptr_t>(
                        array->data + static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t));
                    if (!object || !readable(*object, 0x28)) {
                        continue;
                    }
                    const auto objectClass = memory::readSafe<std::uintptr_t>(*object + kObjectClass);
                    if (!objectClass || !readable(*objectClass, 0x28)) {
                        continue;
                    }
                    ++readableObjects;
                    const auto outer = memory::readSafe<std::uintptr_t>(*object + 0x20);
                    if (outer && *outer == level) {
                        ++levelOwnedObjects;
                    }
                }
                if (readableObjects >= 3) {
                    VALOADER_LOG_INFO(
                        "ULevel array candidate: slot=+0x%zx level=%p off=0x%zx count=%d readable=%d owned=%d",
                        static_cast<std::size_t>(slot - moduleBase_),
                        reinterpret_cast<void*>(level), offset, array->count,
                        readableObjects, levelOwnedObjects);
                }
                const int score = levelOwnedObjects * 10000 + readableObjects * 100 +
                    std::min(array->count, 99);
                if (readableObjects >= 8 && score > bestScore) {
                    bestScore = score;
                    actors = array;
                    actorsOffset = offset;
                }
            }
            if (!actors) {
                VALOADER_LOG_INFO(
                    "UWorld/ULevel candidate without Actors: slot=+0x%zx world=%p level=%p",
                    static_cast<std::size_t>(slot - moduleBase_),
                    reinterpret_cast<void*>(candidate), reinterpret_cast<void*>(level));
                continue;
            }
            const auto stableWorld = memory::readSafe<std::uintptr_t>(slot);
            if (!stableWorld || *stableWorld != candidate) {
                VALOADER_LOG_INFO(
                    "Rejected transient GWorld slot: +0x%zx expected=%p current=%p",
                    static_cast<std::size_t>(slot - moduleBase_),
                    reinterpret_cast<void*>(candidate),
                    reinterpret_cast<void*>(stableWorld.value_or(0)));
                continue;
            }
            ++actorCandidates;
            worldSlot_ = slot;
            levelActorArrayOffset_ = actorsOffset;
            VALOADER_LOG_INFO(
                "Structural GWorld discovered at libUE4+0x%zx actorsOff=0x%zx actors=%d outerMatch=%d",
                static_cast<std::size_t>(slot - moduleBase_), actorsOffset, actors->count,
                levelOuterMatches ? 1 : 0);
            return true;
        }
    }
    VALOADER_LOG_INFO(
        "Structural GWorld stages: ptr=%zu level=%zu outer=%zu actors=%zu gi=%zu lp=%zu pc=%zu mgr=%zu player=%zu camera=%zu",
        readableCandidates, levelCandidates, outerCandidates, actorCandidates,
        gameInstanceCandidates, localPlayerCandidates, controllerCandidates,
        cameraManagerCandidates, playerCandidates, cameraCandidates);
    return false;
}

bool UnrealReflection::hasNames() const noexcept {
    return nameBlocks_ != 0 || codeVNamePool_ != 0;
}

std::uintptr_t UnrealReflection::world() const noexcept {
    if (engineSlot_ != 0) {
        const auto engine = memory::readSafe<std::uintptr_t>(engineSlot_);
        const auto viewport = engine
            ? memory::readSafe<std::uintptr_t>(*engine + kProfile2911.engineGameViewport)
            : std::nullopt;
        const auto value = viewport
            ? memory::readSafe<std::uintptr_t>(*viewport + kProfile2911.viewportWorld)
            : std::nullopt;
        return value.value_or(0);
    }
    const auto value = memory::readSafe<std::uintptr_t>(worldSlot_);
    return value.value_or(0);
}

std::size_t UnrealReflection::levelActorArrayOffset() const noexcept {
    return levelActorArrayOffset_ != 0
        ? levelActorArrayOffset_
        : kProfile2911.levelActorArray;
}

bool UnrealReflection::discoverNamePool() {
    constexpr std::array<std::uint8_t, 4> kNone{{'N', 'o', 'n', 'e'}};
    const std::vector<memory::Range> allRanges = memory::ranges();
    const std::vector<memory::Range> ueRanges = memory::moduleRanges("libUE4.so");
    for (const memory::Range& range : ueRanges) {
        if (range.writable) {
            VALOADER_LOG_INFO("Scanning libUE4 writable PT_LOAD: +0x%zx..+0x%zx",
                static_cast<std::size_t>(range.begin - moduleBase_),
                static_cast<std::size_t>(range.end - moduleBase_));
        }
    }

    // The constructor receives its object in X0 and CodeV allocates it outside
    // the ELF image. Do not probe arbitrary globals here: tagged/PAC pointers in
    // this protected build require a separate, explicitly validated resolver.

    // Stock UE4.23+ fallback.
    for (const memory::Range& range : ueRanges) {
        if (!range.readable || !range.writable || range.end <= range.begin) {
            continue;
        }
        std::uintptr_t address = (range.begin + 7U) & ~std::uintptr_t{7U};
        for (; address + sizeof(std::uintptr_t) <= range.end; address += sizeof(std::uintptr_t)) {
            const std::uintptr_t block = *reinterpret_cast<const std::uintptr_t*>(address);
            if (!readableIn(allRanges, block, 6)) {
                continue;
            }
            const std::uint16_t header = *reinterpret_cast<const std::uint16_t*>(block);
            if ((header & 1U) != 0) {
                continue;
            }
            std::uint8_t lengthShift = 0;
            if ((header >> 1U) == kNone.size()) {
                lengthShift = 1;
            } else if ((header >> 6U) == kNone.size()) {
                lengthShift = 6;
            } else {
                continue;
            }
            if (std::memcmp(reinterpret_cast<const void*>(block + 2), kNone.data(), kNone.size()) == 0) {
                nameBlocks_ = address;
                nameLengthShift_ = lengthShift;
                VALOADER_LOG_INFO("FNamePool blocks discovered at libUE4+0x%zx",
                                  static_cast<std::size_t>(nameBlocks_ - moduleBase_));
                return true;
            }
        }
    }
    return false;
}

bool UnrealReflection::discoverWorld() {
    const std::vector<memory::Range> allRanges = memory::ranges();
    const std::vector<memory::Range> ueRanges = memory::moduleRanges("libUE4.so");
    for (const memory::Range& range : ueRanges) {
        if (!range.readable || !range.writable || range.end <= range.begin) {
            continue;
        }
        std::uintptr_t slot = (range.begin + 7U) & ~std::uintptr_t{7U};
        for (; slot + sizeof(std::uintptr_t) <= range.end; slot += sizeof(std::uintptr_t)) {
            const std::uintptr_t candidate = *reinterpret_cast<const std::uintptr_t*>(slot);
            if (!readableIn(allRanges, candidate, kWorldPersistentLevel + sizeof(std::uintptr_t)) ||
                !isA(candidate, "World")) {
                continue;
            }
            const auto level = memory::read<std::uintptr_t>(candidate + kWorldPersistentLevel);
            if (!level || *level == 0 || !isA(*level, "Level")) {
                continue;
            }
            worldSlot_ = slot;
            VALOADER_LOG_INFO("GWorld slot discovered at libUE4+0x%zx",
                              static_cast<std::size_t>(worldSlot_ - moduleBase_));
            return true;
        }
    }
    return false;
}

std::optional<std::uint32_t> UnrealReflection::nameIndex(std::string_view name) const {
    if (name.empty()) {
        return std::nullopt;
    }
    const std::string key(name);
    if (const auto found = nameIndexCache_.find(key); found != nameIndexCache_.end()) {
        return found->second;
    }
    if (codeVNamePool_ != 0) {
        using HashFunction = std::uint64_t (*)(const char*, std::uint32_t);
        using LookupFunction = std::uint32_t (*)(std::uintptr_t, const char*, std::uint32_t);
        const auto hashFunction = reinterpret_cast<HashFunction>(moduleBase_ + kCodeVNameHash);
        const auto lookupFunction = reinterpret_cast<LookupFunction>(moduleBase_ + kCodeVNameLookup);
        const std::uint32_t hash = static_cast<std::uint32_t>(
            hashFunction(key.c_str(), static_cast<std::uint32_t>(key.size())));
        const std::uint32_t index = lookupFunction(codeVNamePool_, key.c_str(), hash);
        nameIndexCache_.emplace(key, index);
        return index;
    }
    return std::nullopt;
}

std::string UnrealReflection::resolveName(std::uint32_t comparisonIndex) const {
    if (nameBlocks_ == 0) {
        return {};
    }
    const std::uint32_t blockIndex = comparisonIndex >> 16U;
    const std::uint32_t entryOffset = comparisonIndex & 0xFFFFU;
    if (blockIndex > 8192U) {
        return {};
    }
    const auto block = memory::read<std::uintptr_t>(
        nameBlocks_ + static_cast<std::uintptr_t>(blockIndex) * sizeof(std::uintptr_t));
    if (!block || *block == 0) {
        return {};
    }
    const std::uintptr_t entry = *block + static_cast<std::uintptr_t>(entryOffset) * 2U;
    const auto header = memory::read<std::uint16_t>(entry);
    if (!header) {
        return {};
    }
    const bool wide = (*header & 1U) != 0;
    const std::size_t length = nameLengthShift_ != 0 ? (*header >> nameLengthShift_) : 0;
    if (wide || length == 0 || length > 256 || !memory::isReadable(entry + 2, length)) {
        return {};
    }
    std::string result(reinterpret_cast<const char*>(entry + 2), length);
    return isPlausibleName(result) ? result : std::string{};
}

std::uintptr_t UnrealReflection::objectClass(std::uintptr_t object) const {
    return memory::read<std::uintptr_t>(object + kObjectClass).value_or(0);
}

std::string UnrealReflection::objectName(std::uintptr_t object) const {
    const auto index = memory::read<std::uint32_t>(object + kObjectName);
    if (!index) {
        return {};
    }
    if (nameBlocks_ != 0) {
        return resolveName(*index);
    }
    for (const auto& [name, knownIndex] : nameIndexCache_) {
        if (knownIndex == *index) {
            return name;
        }
    }
    return {};
}

std::string UnrealReflection::className(std::uintptr_t object) const {
    const std::uintptr_t type = objectClass(object);
    return type != 0 ? objectName(type) : std::string{};
}

bool UnrealReflection::isA(std::uintptr_t object, std::string_view expected) const {
    const auto expectedIndex = nameIndex(expected);
    std::uintptr_t type = objectClass(object);
    for (int depth = 0; type != 0 && depth < 64; ++depth) {
        const auto actualIndex = memory::read<std::uint32_t>(type + kObjectName);
        if ((expectedIndex && actualIndex && *actualIndex == *expectedIndex) ||
            (!expectedIndex && equalsIgnoreCase(objectName(type), expected))) {
            return true;
        }
        type = memory::read<std::uintptr_t>(type + kStructSuper).value_or(0);
    }
    return false;
}

std::optional<ReflectedProperty> UnrealReflection::findProperty(
    std::uintptr_t object,
    std::initializer_list<std::string_view> names
) const {
    std::uintptr_t type = objectClass(object);
    std::string cacheKey = std::to_string(type);
    for (std::string_view name : names) {
        cacheKey.push_back('|');
        cacheKey.append(name);
    }
    if (const auto cached = propertyCache_.find(cacheKey); cached != propertyCache_.end()) {
        return cached->second;
    }
    for (int classDepth = 0; type != 0 && classDepth < 64; ++classDepth) {
        std::uintptr_t field = memory::read<std::uintptr_t>(type + kStructChildProperties).value_or(0);
        for (int fieldIndex = 0; field != 0 && fieldIndex < 4096; ++fieldIndex) {
            const std::uint32_t fieldNameIndex =
                memory::read<std::uint32_t>(field + kFieldName).value_or(0);
            const std::string fieldName = nameBlocks_ != 0
                ? resolveName(fieldNameIndex)
                : std::string{};
            for (std::string_view requested : names) {
                const auto requestedIndex = nameIndex(requested);
                if (!((requestedIndex && fieldNameIndex == *requestedIndex) ||
                      (!requestedIndex && equalsIgnoreCase(fieldName, requested)))) {
                    continue;
                }
                const auto offset = memory::read<std::int32_t>(field + kPropertyOffset);
                if (!offset || *offset < 0 || *offset > 0x10000) {
                    propertyCache_[cacheKey] = std::nullopt;
                    return std::nullopt;
                }
                const std::uintptr_t fieldClass =
                    memory::read<std::uintptr_t>(field + kFieldClass).value_or(0);
                const std::uint32_t fieldTypeIndex = fieldClass != 0
                    ? memory::read<std::uint32_t>(fieldClass).value_or(0)
                    : 0;
                std::string fieldType = nameBlocks_ != 0
                    ? resolveName(fieldTypeIndex)
                    : std::string{};
                if (fieldType.empty()) {
                    for (std::string_view candidate : {"BoolProperty", "ByteProperty",
                             "Int16Property", "IntProperty", "FloatProperty", "ObjectProperty"}) {
                        if (const auto candidateIndex = nameIndex(candidate);
                            candidateIndex && *candidateIndex == fieldTypeIndex) {
                            fieldType.assign(candidate);
                            break;
                        }
                    }
                }
                const std::uint8_t mask = memory::read<std::uint8_t>(
                    field + kBoolPropertyByteMask).value_or(0);
                ReflectedProperty result{
                    static_cast<std::size_t>(*offset), std::string(requested), fieldType, mask};
                propertyCache_[cacheKey] = result;
                return result;
            }
            field = memory::read<std::uintptr_t>(field + kFieldNext).value_or(0);
        }
        type = memory::read<std::uintptr_t>(type + kStructSuper).value_or(0);
    }
    propertyCache_[cacheKey] = std::nullopt;
    return std::nullopt;
}

} // namespace valoader::game
