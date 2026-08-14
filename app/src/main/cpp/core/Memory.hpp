#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace valoader::memory {

struct Range {
    std::uintptr_t begin{};
    std::uintptr_t end{};
    bool readable{};
    bool writable{};
    bool executable{};
    std::string path;
};

[[nodiscard]] std::uintptr_t moduleBase(std::string_view moduleName) noexcept;
[[nodiscard]] bool isReadable(std::uintptr_t address, std::size_t size) noexcept;
[[nodiscard]] std::vector<Range> ranges();
[[nodiscard]] std::vector<Range> moduleRanges(std::string_view moduleName);
[[nodiscard]] bool readBytesSafe(
    std::uintptr_t address,
    void* destination,
    std::size_t size
) noexcept;

template <typename T>
[[nodiscard]] std::optional<T> read(std::uintptr_t address) noexcept {
    if (!isReadable(address, sizeof(T))) {
        return std::nullopt;
    }
    return *reinterpret_cast<const T*>(address);
}

// Use this for discovery and pointers owned by the game. Unlike a checked
// direct dereference, process_vm_readv cannot crash when UE unmaps a page
// between the /proc/maps check and the actual read.
template <typename T>
[[nodiscard]] std::optional<T> readSafe(std::uintptr_t address) noexcept {
    T value{};
    return readBytesSafe(address, &value, sizeof(value))
        ? std::optional<T>(value)
        : std::nullopt;
}

} // namespace valoader::memory
