// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// AMD FSR frame generation (FidelityFX SDK 2.4, Vulkan).
//
// AMD's frame generation swapchain replaces the emulator's swapchain: it presents each real frame
// plus one generated in between, paced on its own threads. The generated frame comes from the
// game's depth and motion vectors, which only Ocarina of Time's live upscale pipeline provides, so
// other games present through it with generation off. The DLL picks the version for the GPU:
// FSR Frame Generation 4 on Radeon RX 9000, FSR 3.1 everywhere else (other AMD, NVIDIA, Intel).

#pragma once

#include <array>
#include <mutex>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

namespace AmdFrameGen {

/// The user picked AMD FSR frame generation (Windows; the device and swapchain set up at startup).
[[nodiscard]] bool Requested();

/// Frames AMD's swapchain was asked to generate so far (one per game frame presented while
/// generating), for the frame rate shown with the game's.
[[nodiscard]] u64 GeneratedFrames();

/// Whether AMD's frame generation swapchain is in use (it is set up when CheezyEmu starts).
[[nodiscard]] bool SwapchainActive();

/// Creates AMD's frame generation swapchain in place of vkCreateSwapchainKHR. Returns
/// VK_NULL_HANDLE when it can't (missing DLL, no spare present queue, creation failed); the caller
/// then creates a normal swapchain.
[[nodiscard]] VkSwapchainKHR CreateSwapchain(const Device& device, std::mutex& submit_mutex,
                                             const VkSwapchainCreateInfoKHR& create_info);

/// Waits for AMD's presents, then destroys the swapchain and its context.
void DestroySwapchain(const Device& device, VkSwapchainKHR swapchain);

/// Lets AMD's in-flight presents finish (before the device idles or the swapchain is replaced).
void WaitForPresents();

/// Destroys every frame generation context once the device is idle (emulator shutdown).
void Shutdown();

[[nodiscard]] std::vector<VkImage> GetImages(const Device& device, VkSwapchainKHR swapchain);

[[nodiscard]] VkResult AcquireNextImage(const Device& device, VkSwapchainKHR swapchain,
                                        VkSemaphore semaphore, u32* image_index);

struct PresentTarget {
    u64 frame_id;  ///< The prepared game frame this present shows; 0 when none (no generation).
    VkRect2D rect; ///< Where the game picture sits in the swapchain image.
    /// The presented image without the game's HUD (swapchain size and format, shader readable),
    /// or null.
    VkImage hudless{};
    VkFormat hudless_format{};
    VkExtent2D hudless_extent{};
};

/// Presents through AMD's swapchain. Must not be called with the scheduler's submit mutex held:
/// AMD submits to the graphics queue from inside, under that mutex.
[[nodiscard]] VkResult Present(VkQueue queue, const VkPresentInfoKHR& info,
                               const PresentTarget& target);

/// The near and far plane and vertical field of view (radians) of a view-projection matrix.
struct CameraPlanes {
    f32 near_plane = 0.1f;
    f32 far_plane = 10000.0f;
    f32 fov = 1.0f;
};
/// Row-major, [-1,1] clip depth, as the game's camera buffer holds it. Defaults when the matrix
/// can't be taken apart (and on platforms without AMD's runtime).
[[nodiscard]] CameraPlanes ExtractCameraPlanes(const std::array<f32, 16>& view_projection);

struct PrepareImage {
    VkImage image;
    VkFormat format;
    VkImageAspectFlags aspect;
};

struct PrepareInputs {
    PrepareImage depth;          ///< Device depth, [0,1] growing with distance, GENERAL layout.
    PrepareImage motion_vectors; ///< Pixel motion to the previous frame, GENERAL layout.
    VkExtent2D render_size;
    std::array<f32, 16> view_projection; ///< Row-major, [-1,1] clip depth.
    bool reset;
    std::array<f32, 2> jitter{}; ///< Offset the scene (and so the depth) was drawn with, pixels.
};

/// Records AMD's prepare pass for one game frame on the scheduler.
void Prepare(Scheduler& scheduler, const PrepareInputs& inputs);

/// On the scheduler worker, in order with the recorded commands: the frame prepared since the
/// last call, or 0.
[[nodiscard]] u64 TakePreparedFrame();

} // namespace AmdFrameGen
} // namespace Vulkan
