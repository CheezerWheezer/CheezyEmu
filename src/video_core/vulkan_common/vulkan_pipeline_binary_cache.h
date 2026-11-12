// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

/// Saves the driver's compiled pipelines with VK_KHR_pipeline_binary and gives them back to the
/// driver on later runs, so a pipeline is compiled once per driver instead of once per session.
///
/// Drivers that keep their own disk cache don't need this, but AMD's Windows driver doesn't keep
/// enough: on an RX 6750 XT a cached Ocarina of Time pipeline still took 2.5-5 ms (often seconds
/// at boot) to build in every session, and 98% of the boot build was spent in amdvlk64.dll. Built
/// from saved binaries it takes ~0.2 ms (boot to title 513 s -> 14 s with 2.9k pipelines).
///
/// One file per title, beside the title's other caches:
///   <shader dir>/<title id>/vulkan_binaries.fork-v<cache version>.bin
/// The file records the driver's global pipeline key; a driver update replaces it. Removing the
/// title's shader caches from the game list removes it too.
namespace PipelineBinaryCache {

/// Starts saving and reusing binaries for the pipelines created on this device. No-op when the
/// device doesn't have VK_KHR_pipeline_binary enabled.
void Activate(VkDevice device, const vk::DeviceDispatch& dld, const std::filesystem::path& file);

/// Stops it and closes the file. Pipelines created afterwards are built normally.
void Deactivate();

/// Called by vk::Device. Returns false when the pipeline should be created the usual way.
bool CreateGraphicsPipeline(VkDevice device, const vk::DeviceDispatch& dld,
                            const VkGraphicsPipelineCreateInfo& ci, VkPipelineCache cache,
                            VkPipeline* out, VkResult* result);
bool CreateComputePipeline(VkDevice device, const vk::DeviceDispatch& dld,
                           const VkComputePipelineCreateInfo& ci, VkPipelineCache cache,
                           VkPipeline* out, VkResult* result);

/// Pipelines the driver is compiling right now for the game: not ones loaded from saved binaries
/// (about half a millisecond each) and not the shader precache's. The status bar shows this as
/// "Building"; counting the loads too read as "the shader prebuild did not work".
[[nodiscard]] int PipelinesCompiling();

/// Marks the calling thread as a background builder (the shader precache): its compiles are
/// not counted in PipelinesCompiling.
void SetBackgroundBuilderThread();

/// Counts one driver compile while it lives (vk::Device and the binary cache).
class CompileScope {
public:
    CompileScope();
    ~CompileScope();
    CompileScope(const CompileScope&) = delete;
    CompileScope& operator=(const CompileScope&) = delete;

private:
    bool counted;
};

} // namespace PipelineBinaryCache

} // namespace Vulkan
