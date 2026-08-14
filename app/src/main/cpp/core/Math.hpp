#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace valoader::math {

struct Vec2 {
    float x{};
    float y{};
};

struct Vec3 {
    float x{};
    float y{};
    float z{};

    [[nodiscard]] Vec3 operator+(const Vec3& other) const noexcept {
        return {x + other.x, y + other.y, z + other.z};
    }

    [[nodiscard]] Vec3 operator-(const Vec3& other) const noexcept {
        return {x - other.x, y - other.y, z - other.z};
    }

    [[nodiscard]] Vec3 operator*(const Vec3& other) const noexcept {
        return {x * other.x, y * other.y, z * other.z};
    }
};

struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{1.0F};

    [[nodiscard]] Vec3 rotate(const Vec3& point) const noexcept {
        const Vec3 q{x, y, z};
        const Vec3 twiceCross{
            2.0F * (q.y * point.z - q.z * point.y),
            2.0F * (q.z * point.x - q.x * point.z),
            2.0F * (q.x * point.y - q.y * point.x)
        };
        return {
            point.x + w * twiceCross.x + (q.y * twiceCross.z - q.z * twiceCross.y),
            point.y + w * twiceCross.y + (q.z * twiceCross.x - q.x * twiceCross.z),
            point.z + w * twiceCross.z + (q.x * twiceCross.y - q.y * twiceCross.x)
        };
    }
};

struct Transform {
    Quaternion rotation{};
    Vec3 translation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};
    float padding[2]{};

    [[nodiscard]] Vec3 transformPosition(const Vec3& point) const noexcept {
        return rotation.rotate(point * scale) + translation;
    }
};

static_assert(sizeof(Transform) == 0x30);
static_assert(offsetof(Transform, translation) == 0x10);
static_assert(offsetof(Transform, scale) == 0x1C);

struct Camera {
    Vec3 location{};
    Vec3 rotation{};
    float fieldOfView{90.0F};
};

[[nodiscard]] inline bool isFinite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline float dot(const Vec3& left, const Vec3& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] inline bool worldToScreen(
    const Vec3& world,
    const Camera& camera,
    float width,
    float height,
    Vec2& screen
) noexcept {
    constexpr float degreesToRadians = 0.01745329251994329577F;
    const float pitch = camera.rotation.x * degreesToRadians;
    const float yaw = camera.rotation.y * degreesToRadians;
    const float roll = camera.rotation.z * degreesToRadians;

    const float sp = std::sin(pitch);
    const float cp = std::cos(pitch);
    const float sy = std::sin(yaw);
    const float cy = std::cos(yaw);
    const float sr = std::sin(roll);
    const float cr = std::cos(roll);

    const Vec3 forward{cp * cy, cp * sy, sp};
    const Vec3 right{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    const Vec3 up{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
    const Vec3 delta = world - camera.location;

    const float depth = dot(delta, forward);
    if (depth <= 1.0F || camera.fieldOfView < 1.0F || width <= 1.0F || height <= 1.0F) {
        return false;
    }

    const float focalLength = (width * 0.5F) /
        std::tan(camera.fieldOfView * degreesToRadians * 0.5F);
    screen.x = width * 0.5F + dot(delta, right) * focalLength / depth;
    screen.y = height * 0.5F - dot(delta, up) * focalLength / depth;
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

} // namespace valoader::math
