#include "core/Memory.hpp"

#include <cstdio>
#include <cstring>
#include <link.h>
#include <limits>
#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

namespace valoader::memory {
namespace {

std::vector<Range> readRanges() {
    std::vector<Range> result;
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        return result;
    }

    char line[1024]{};
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        unsigned long long begin{};
        unsigned long long end{};
        char permissions[5]{};
        int pathOffset{};
        if (std::sscanf(line, "%llx-%llx %4s %*s %*s %*s %n",
                        &begin, &end, permissions, &pathOffset) < 3) {
            continue;
        }
        std::string path = pathOffset > 0 ? std::string(line + pathOffset) : std::string{};
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
            path.pop_back();
        }
        result.push_back({
            static_cast<std::uintptr_t>(begin),
            static_cast<std::uintptr_t>(end),
            permissions[0] == 'r',
            permissions[1] == 'w',
            permissions[2] == 'x',
            std::move(path)
        });
    }
    std::fclose(maps);
    return result;
}

} // namespace

std::uintptr_t moduleBase(std::string_view moduleName) noexcept {
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        return 0;
    }

    char line[1024]{};
    std::uintptr_t result = 0;
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        if (std::strstr(line, moduleName.data()) == nullptr) {
            continue;
        }
        unsigned long long start{};
        if (std::sscanf(line, "%llx-", &start) == 1) {
            result = static_cast<std::uintptr_t>(start);
            break;
        }
    }
    std::fclose(maps);
    return result;
}

bool isReadable(std::uintptr_t address, std::size_t size) noexcept {
    if (address < 0x10000 || size == 0 || address > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }

    static std::mutex cacheMutex;
    static std::vector<Range> cache;
    std::lock_guard lock(cacheMutex);
    if (cache.empty()) {
        cache = readRanges();
    }
    const std::uintptr_t requestedEnd = address + size;
    for (const Range& range : cache) {
        if (range.readable && address >= range.begin && requestedEnd <= range.end) {
            return true;
        }
    }
    cache = readRanges();
    for (const Range& range : cache) {
        if (range.readable && address >= range.begin && requestedEnd <= range.end) {
            return true;
        }
    }
    return false;
}

std::vector<Range> ranges() {
    return readRanges();
}

std::vector<Range> moduleRanges(std::string_view moduleName) {
    std::vector<Range> result;
    struct SearchContext {
        std::string_view name;
        std::vector<Range>* ranges;
    } context{moduleName, &result};

    // /proc/self/maps marks the zero-filled tail of PT_LOAD as anonymous .bss.
    // dl_iterate_phdr keeps the ELF ownership and therefore includes globals such
    // as GWorld/FNamePool even when their mapping has no libUE4.so pathname.
    dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* opaque) {
        auto& search = *static_cast<SearchContext*>(opaque);
        const std::string_view path = info->dlpi_name != nullptr
            ? std::string_view(info->dlpi_name)
            : std::string_view{};
        if (path.find(search.name) == std::string_view::npos) {
            return 0;
        }
        for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
            const ElfW(Phdr)& header = info->dlpi_phdr[index];
            if (header.p_type != PT_LOAD || header.p_memsz == 0) {
                continue;
            }
            const auto begin = static_cast<std::uintptr_t>(info->dlpi_addr + header.p_vaddr);
            search.ranges->push_back({
                begin,
                begin + static_cast<std::uintptr_t>(header.p_memsz),
                (header.p_flags & PF_R) != 0,
                (header.p_flags & PF_W) != 0,
                (header.p_flags & PF_X) != 0,
                std::string(path)
            });
        }
        return 1;
    }, &context);

    // Fallback for platforms where the loader does not enumerate the module.
    if (result.empty()) {
        for (Range& range : readRanges()) {
            if (range.path.find(moduleName) != std::string::npos) {
                result.push_back(std::move(range));
            }
        }
    }
    return result;
}

bool readBytesSafe(
    std::uintptr_t address,
    void* destination,
    std::size_t size
) noexcept {
    if (address < 0x10000 || destination == nullptr || size == 0 ||
        address > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    iovec local{destination, size};
    iovec remote{reinterpret_cast<void*>(address), size};
    const long copied = syscall(
        __NR_process_vm_readv,
        getpid(),
        &local,
        1UL,
        &remote,
        1UL,
        0UL
    );
    return copied == static_cast<long>(size);
}

} // namespace valoader::memory
