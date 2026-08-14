#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace valoader::game {

struct ReflectedProperty {
    std::size_t offset{};
    std::string name;
    std::string type;
    std::uint8_t boolMask{};
};

class UnrealReflection final {
public:
    [[nodiscard]] bool discover(std::uintptr_t moduleBase);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool namesReady() const noexcept;
    [[nodiscard]] std::uintptr_t world() const noexcept;
    [[nodiscard]] std::size_t levelActorArrayOffset() const noexcept;
    [[nodiscard]] std::string objectName(std::uintptr_t object) const;
    [[nodiscard]] std::string className(std::uintptr_t object) const;
    [[nodiscard]] bool isA(std::uintptr_t object, std::string_view className) const;
    [[nodiscard]] std::optional<ReflectedProperty> findProperty(
        std::uintptr_t object,
        std::initializer_list<std::string_view> names
    ) const;

private:
    [[nodiscard]] bool discoverNamePool();
    [[nodiscard]] bool discoverWorldStructurally();
    [[nodiscard]] bool discoverWorld();
    [[nodiscard]] bool hasNames() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> nameIndex(std::string_view name) const;
    [[nodiscard]] std::string resolveName(std::uint32_t comparisonIndex) const;
    [[nodiscard]] std::uintptr_t objectClass(std::uintptr_t object) const;

    std::uintptr_t moduleBase_{};
    std::uintptr_t nameBlocks_{};
    std::uintptr_t codeVNamePool_{};
    std::uintptr_t worldSlot_{};
    std::uintptr_t engineSlot_{};
    std::size_t levelActorArrayOffset_{};
    std::uint8_t nameLengthShift_{};
    mutable std::unordered_map<std::string, std::uint32_t> nameIndexCache_;
    mutable std::unordered_map<std::string, std::optional<ReflectedProperty>> propertyCache_;
};

} // namespace valoader::game
