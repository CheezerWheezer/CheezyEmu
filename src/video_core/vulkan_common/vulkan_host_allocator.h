// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan.h"

namespace Vulkan {

/// Host memory for the graphics driver's own bookkeeping (VkAllocationCallbacks), handed to it
/// with the instance and the device.
///
/// Why: AMD's Windows driver compiles shaders with memory from the process heap, and threads that
/// build pipelines at the same time wait for that heap's lock most of their time (measured on an
/// RX 6750 XT: 16 build threads did the work of 3 to 4, 75 to 85% of their time waiting in
/// RtlAllocateHeap). That is every pipeline built while a game runs, and the whole shader build
/// on the loading screen. Here each thread allocates from one of several heaps, so they stop
/// waiting for each other.
///
/// Null when it is off (EDEN_NX2_VK_ALLOCATOR=0) or the platform has no need for it: the driver
/// then allocates as it always did. The same pointer goes to vkCreateInstance, vkCreateDevice and
/// their destroy calls.
[[nodiscard]] const VkAllocationCallbacks* HostAllocator();

/// How much the driver asked for so far: allocations made, and bytes in use.
struct HostAllocatorStats {
    u64 allocations{};
    u64 bytes_in_use{};
};
[[nodiscard]] HostAllocatorStats HostAllocatorUse();

/// EDEN_NX2_VK_ALLOCATOR=2 (test): the shader precache compares one heap against all of them.
[[nodiscard]] bool HostAllocatorComparison();
void SetHostAllocatorOneHeap(bool one);

/// CPU time this process used so far (user and kernel), 0 where it is not known.
[[nodiscard]] double ProcessCpuSeconds();

} // namespace Vulkan
