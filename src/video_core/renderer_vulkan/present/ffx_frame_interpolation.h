// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <vector>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/vk_amd_frame_gen.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class MemoryAllocator;
class Scheduler;

/// AMD FSR 3.1.4 frame generation built from source, for systems AMD ships no frame generation
/// runtime for (Linux, macOS): the FidelityFX SDK v1.1.4 optical flow and frame interpolation
/// passes (externals/FidelityFX-SDK, compiled in host_shaders). The host side follows the SDK's
/// ffx_opticalflow.cpp and ffx_frameinterpolation.cpp: the same passes, internal images,
/// constants and dispatch sizes, recorded through the scheduler with push descriptors.
///
/// The game's depth and motion (Ocarina of Time's, through AmdFrameGen::Prepare) are prepared
/// per game frame; at present time optical flow runs on the presented frame and one frame is
/// interpolated halfway between the previous and the current presented frame (2x, as AMD's).
class FfxFrameInterpolation {
public:
    explicit FfxFrameInterpolation(const Device& device, MemoryAllocator& memory_allocator,
                                   Scheduler& scheduler, VkExtent2D display_size,
                                   VkFormat frame_format);
    ~FfxFrameInterpolation();

    FfxFrameInterpolation(const FfxFrameInterpolation&) = delete;
    FfxFrameInterpolation& operator=(const FfxFrameInterpolation&) = delete;

    /// Whether the device has what the passes need (push descriptors, storage formats).
    [[nodiscard]] static bool IsSupported(const Device& device);

    /// Whether the game's depth and motion were prepared for the frame Process took last.
    [[nodiscard]] bool HasGameFrame() const noexcept {
        return game_frame_for_present;
    }

    /// The presented frame (GENERAL layout, the size and format given at construction) becomes
    /// the current interpolation source; optical flow runs against the previous one. With
    /// `hudless` (the same frame drawn without the game's HUD, or null) the HUD-less frames are
    /// interpolated and the HUD is taken from `frame` as it is, so it doesn't move or smear.
    void Process(VkImage frame, VkImage hudless = VK_NULL_HANDLE);

    /// Writes the frame halfway between the previous and the current presented frame into
    /// `destination` (GENERAL layout): written through `storage_view`, read back through
    /// `sampled_view` for the inpainting pyramid.
    void Generate(VkImage destination, VkImageView storage_view, VkImageView sampled_view);

    /// Records the prepare pass of one game frame (dilated depth and motion) on the GPU thread.
    void Prepare(const AmdFrameGen::PrepareInputs& inputs);

    /// AmdFrameGen::Prepare on systems without AMD's runtime: hands the game frame to the live
    /// instance, if there is one.
    static void PrepareActive(const AmdFrameGen::PrepareInputs& inputs);

private:
    struct Image {
        vk::Image image;
        vk::ImageView view;
        std::vector<vk::ImageView> levels; ///< One view per level when it has more than one
        VkFormat format{};
        VkExtent2D extent{};
    };
    class Pass;

    Image CreateImage(VkFormat format, u32 width, u32 height, u32 levels, bool storage);
    void CreateRenderSizeImages(VkExtent2D render_size);
    void InitializeImages(std::vector<VkImage> images);
    /// Uniform data for this recording: a slice of a ring the GPU is done with.
    VkDescriptorBufferInfo Uniform(const void* data, size_t size);
    void BeginUniforms();
    void RunOpticalFlow(VkImageView current, bool reset);

    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;
    VkExtent2D display;
    VkFormat format;

    vk::Sampler linear_sampler;
    std::vector<std::unique_ptr<Pass>> passes;

    // Optical flow (display size, 8x8 blocks).
    std::array<std::array<Image, 7>, 2> luma;
    std::array<std::array<Image, 7>, 2> flow;
    std::array<VkExtent2D, 7> flow_sizes{};
    Image scd_histogram;
    Image scd_previous_histogram;
    Image scd_temp;
    Image flow_vectors; ///< The flow interpolation reads (level 0, filtered)
    Image scd_output;
    u32 of_frame_index = 0;
    u32 of_parity = 0;

    // Frame interpolation.
    std::array<Image, 2> sources; ///< Previous and current presented frame, by `current_source`
    u32 current_source = 0;
    Image with_hud;          ///< The current frame with its HUD, when `sources` are HUD-less
    bool hud_apart = false;  ///< The last Process had a HUD-less frame
    bool have_previous = false;
    Image inpainting_pyramid;
    Image distortion_field;
    Image zero_confidence;
    vk::Buffer counters;
    VkExtent2D render{};
    Image dilated_depth;
    Image dilated_motion;
    Image reconstructed_previous_depth;
    Image reconstructed_interpolated_depth;
    Image game_vectors_x;
    Image game_vectors_y;
    Image flow_vectors_x;
    Image flow_vectors_y;
    Image disocclusion;

    // The prepared game frame.
    bool game_frame_prepared = false;
    bool game_frame_for_present = false;
    bool game_reset = true;
    std::array<f32, 2> jitter{};
    AmdFrameGen::CameraPlanes camera{};
    f32 frame_time_ms = 16.6f;
    std::chrono::steady_clock::time_point last_prepare{};
    u64 prepared_frames = 0;
    /// Views of the game's depth and motion images, kept until the GPU is done with them.
    std::deque<std::pair<u64, vk::ImageView>> input_views;

    // Uniform ring: regions reused once the GPU passed their tick.
    vk::Buffer uniform_buffer;
    std::span<u8> uniform_mapped;
    std::array<u64, 8> region_ticks{};
    size_t region = 0;
    size_t region_offset = 0;
};

} // namespace Vulkan
