// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "common/logging.h"
#include "video_core/vulkan_common/vulkan_host_allocator.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Vulkan {

#ifdef _WIN32
namespace {

constexpr size_t NumHeaps = 32;

/// In front of every block: where it came from and how large the caller asked it to be.
struct Header {
    HANDLE heap;
    void* raw;
    size_t size;
};

std::array<HANDLE, NumHeaps> heaps{};
std::atomic<u64> allocations{};
std::atomic<u64> bytes_in_use{};

std::atomic<bool> one_heap{};

HANDLE ThreadHeap() {
    if (one_heap.load(std::memory_order_relaxed)) {
        return GetProcessHeap(); // The test: the heap the driver uses without this allocator
    }
    static std::atomic<u32> next{};
    thread_local const u32 index = next.fetch_add(1, std::memory_order_relaxed) % NumHeaps;
    return heaps[index];
}

Header* HeaderOf(void* memory) {
    return reinterpret_cast<Header*>(static_cast<u8*>(memory) - sizeof(Header));
}

void* Allocate(size_t size, size_t alignment) {
    alignment = (std::max)(alignment, size_t{16});
    const HANDLE heap = ThreadHeap();
    void* const raw = HeapAlloc(heap, 0, size + alignment + sizeof(Header));
    if (raw == nullptr) {
        return nullptr;
    }
    const uintptr_t first = reinterpret_cast<uintptr_t>(raw) + sizeof(Header);
    const uintptr_t user = (first + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    void* const memory = reinterpret_cast<void*>(user);
    *HeaderOf(memory) = {heap, raw, size};
    allocations.fetch_add(1, std::memory_order_relaxed);
    bytes_in_use.fetch_add(size, std::memory_order_relaxed);
    return memory;
}

void Free(void* memory) {
    if (memory == nullptr) {
        return;
    }
    const Header header = *HeaderOf(memory);
    bytes_in_use.fetch_sub(header.size, std::memory_order_relaxed);
    HeapFree(header.heap, 0, header.raw);
}

void* VKAPI_CALL OnAllocate(void*, size_t size, size_t alignment, VkSystemAllocationScope) {
    return Allocate(size, alignment);
}

void* VKAPI_CALL OnReallocate(void*, void* original, size_t size, size_t alignment,
                              VkSystemAllocationScope) {
    if (original == nullptr) {
        return Allocate(size, alignment);
    }
    if (size == 0) {
        Free(original);
        return nullptr;
    }
    void* const memory = Allocate(size, alignment);
    if (memory != nullptr) {
        std::memcpy(memory, original, (std::min)(size, HeaderOf(original)->size));
        Free(original);
    }
    return memory;
}

void VKAPI_CALL OnFree(void*, void* memory) {
    Free(memory);
}

bool Enabled() {
    // OFF unless EDEN_NX2_VK_ALLOCATOR=1 (or 2, the comparison). It was on by default for an
    // evening (2026-11-10): shader builds on an RX 6750 XT ran 1.7 times faster with it, but
    // the test PC froze hard twice that evening (no bugcheck, about once per 1.4 hours of
    // running), in a shader build and in plain gameplay, and never in the days of the same
    // tests before it. Not proven to be the cause, but nothing else touches the driver this
    // deeply, so it does not ship on.
    const char* const value = std::getenv("EDEN_NX2_VK_ALLOCATOR");
    return value != nullptr && (value[0] == '1' || value[0] == '2');
}

} // Anonymous namespace

const VkAllocationCallbacks* HostAllocator() {
    static const VkAllocationCallbacks* const callbacks = []() -> const VkAllocationCallbacks* {
        if (!Enabled()) {
            return nullptr;
        }
        for (HANDLE& heap : heaps) {
            heap = HeapCreate(0, 0, 0);
            if (heap == nullptr) {
                LOG_WARNING(Render_Vulkan, "Driver memory: a heap could not be created; the "
                                           "driver allocates by itself");
                return nullptr;
            }
        }
        static const VkAllocationCallbacks table{
            .pUserData = nullptr,
            .pfnAllocation = OnAllocate,
            .pfnReallocation = OnReallocate,
            .pfnFree = OnFree,
            .pfnInternalAllocation = nullptr,
            .pfnInternalFree = nullptr,
        };
        LOG_INFO(Render_Vulkan, "Driver memory: the graphics driver allocates from {} heaps of "
                                "the emulator's (EDEN_NX2_VK_ALLOCATOR, experimental)", NumHeaps);
        return &table;
    }();
    return callbacks;
}

HostAllocatorStats HostAllocatorUse() {
    return {allocations.load(std::memory_order_relaxed),
            bytes_in_use.load(std::memory_order_relaxed)};
}

bool HostAllocatorComparison() {
    const char* const value = std::getenv("EDEN_NX2_VK_ALLOCATOR");
    return value != nullptr && value[0] == '2';
}

void SetHostAllocatorOneHeap(bool one) {
    one_heap.store(one, std::memory_order_relaxed);
}

double ProcessCpuSeconds() {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return 0.0;
    }
    const auto ticks = [](const FILETIME& time) {
        return (static_cast<u64>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;
}

#else

bool HostAllocatorComparison() {
    return false;
}

void SetHostAllocatorOneHeap(bool) {}

double ProcessCpuSeconds() {
    return 0.0;
}

const VkAllocationCallbacks* HostAllocator() {
    return nullptr;
}

HostAllocatorStats HostAllocatorUse() {
    return {};
}

#endif

} // namespace Vulkan
