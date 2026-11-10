// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "common/settings.h"

namespace Settings {

/// The PC the recommended settings are chosen for. The defaults stand for a capable desktop.
struct HostProfile {
    u64 ram_bytes{u64{32} << 30};  ///< System RAM
    u64 vram_bytes{u64{12} << 30}; ///< Video memory of the chosen GPU (its device-local heaps)
    bool amd{true};                ///< AMD GPU
    bool integrated{false};        ///< Integrated GPU (shares the system's RAM)
    bool windows{true};            ///< AMD's FSR and frame generation runtimes are Windows only
};

/// CheezyEmu's recommended settings for a PC. This table is the only place that lists them:
/// change a value here to change what "Find Best Settings" writes. The first part is upstream
/// Lichii's developer table with CheezyEmu's own defaults where they differ; the last part is
/// what depends on the PC. Personal choices (language, region, docked or handheld, backend,
/// device) stay out of it. Every entry is visited for every PC, so a backup made on one holds
/// what another may change.
template <typename Func>
void ForEachRecommendedSetting(Values& values, const HostProfile& host, Func&& func) {
    const auto entry = [&func]<typename Type, bool ranged>(SwitchableSetting<Type, ranged>& setting,
                                                           std::type_identity_t<Type> value) {
        func(setting, value);
    };
    constexpr u64 GiB = u64{1} << 30;
    // A "12 GB" card reports a little under 12 GiB, an "8 GB" one a little under 8.
    const bool large_card = !host.integrated && host.vram_bytes >= 11 * GiB;
    const bool medium_card = !host.integrated && host.vram_bytes >= 7 * GiB;

    // Graphics > Advanced
    entry(values.gpu_accuracy, GpuAccuracy::Low);
    entry(values.dma_accuracy, DmaAccuracy::Default);
    entry(values.gpu_fence_behavior, GpuFenceBehavior::Default);
    entry(values.native_compute_write_mark, NativeComputeWriteMark::Auto);
    // Aggressive keeps more textures in video memory, which Windows also charges to RAM plus
    // page file: only where both are plentiful.
    entry(values.vram_usage_mode, large_card && host.ram_bytes >= 30 * GiB
                                      ? VramUsageMode::Aggressive
                                      : VramUsageMode::Conservative);
    entry(values.nvdec_emulation, NvdecEmulation::Gpu);
    entry(values.max_anisotropy, AnisotropyMode::Automatic);
    entry(values.accelerate_astc, AstcDecodeMode::Gpu);
    entry(values.frame_pacing_mode, FramePacingMode::Target_Auto);
    // BC3 takes a quarter of the video memory of uncompressed textures.
    entry(values.astc_recompression,
          large_card ? AstcRecompression::Uncompressed : AstcRecompression::Bc3);
    entry(values.sync_memory_operations, false);
    entry(values.renderer_force_max_clock, false);
    entry(values.use_disk_shader_cache, true);
    entry(values.use_vulkan_driver_pipeline_cache, true);
    // CheezyEmu's default since build 9 ("Skip only redrawn targets").
    entry(values.native_async_draws, NativeAsyncDraws::Transient);
    entry(values.native_pipeline_wait_ms, 8);
    entry(values.precache_shaders, PrecacheShaders::Off);
    entry(values.use_video_framerate, false);
    entry(values.mkw_30fps, false);
    entry(values.mkw_dynamic_fps, false);
    entry(values.use_reactive_flushing, true);
    entry(values.barrier_feedback_loops, true);
    entry(values.enable_buffer_history, false);

    // Graphics > Extras
    entry(values.skip_cpu_inner_invalidation, false);
    entry(values.fix_bloom_effects, false);
    entry(values.use_asynchronous_shaders, true);
    entry(values.gpu_unswizzle_texture_size, GpuUnswizzleSize::Large);
    entry(values.gpu_unswizzle_stream_size, GpuUnswizzle::Medium);
    entry(values.gpu_unswizzle_chunk_size, GpuUnswizzleChunk::Medium);
    entry(values.gpu_unswizzle_enabled, false);
#if defined(_WIN32) || defined(__linux__)
    // Other hosts skip the pinning.
    entry(values.pin_gpu_threads, true);
#endif
#ifndef __APPLE__
    // Extended dynamic state stays disabled on macOS: MoltenVK renders black screens with it.
    entry(values.dyna_state, ExtendedDynamicState::EDS2);
#endif
    entry(values.sample_shading, 0);
    entry(values.vertex_input_dynamic_state, true);

    // System
    entry(values.cpu_clock, CpuClock::Normal);
    entry(values.gpu_clock, GpuClock::Boost);
    entry(values.use_multi_core, true);
    entry(values.use_speed_limit, true);
    entry(values.speed_limit, 100);
    entry(values.slow_speed_limit, 50);
    entry(values.turbo_speed_limit, 200);
    entry(values.sync_core_speed, false);

    // CPU
    entry(values.cpu_accuracy, CpuAccuracy::Auto);
    entry(values.use_custom_cpu_ticks, false);

    // What depends on the PC: AMD FSR, frame generation and Ocarina of Time's render size.
    // FSR runs on any GPU on Windows (FSR 4 where AMD offers it, FSR 3.1 otherwise).
    entry(values.amd_fsr4, host.windows);
    // AMD frame generation keeps about 1.5 GB of RAM for itself: a PC with 16 GB runs out with
    // it (build 11 reports of error 1455 and whole-PC freezes), and it needs a card that holds
    // the game's frame rate.
    const bool frame_generation =
        host.windows && host.amd && medium_card && host.ram_bytes >= 24 * GiB;
    entry(values.frame_gen, frame_generation);
    entry(values.frame_gen_type,
          host.windows && host.amd ? FrameGenType::AmdFsr : values.frame_gen_type.GetValue(true));
    // The scene's render size: full on a 12 GB card, lower where video memory or the GPU is
    // short. (On a PC limited by its CPU a lower preset doesn't add frames.)
    entry(values.oot_fsr_preset, large_card    ? OotFsrPreset::NativeAa
                                 : medium_card ? OotFsrPreset::Quality
                                               : OotFsrPreset::Performance);
    // Docked output: 4K needs the video memory for 4K images.
    entry(values.oot_docked_output,
          large_card ? OotDockedOutput::Output4k : OotDockedOutput::Output1440p);
}

/// The table for the default PC (a capable desktop with an AMD card).
template <typename Func>
void ForEachRecommendedSetting(Values& values, Func&& func) {
    ForEachRecommendedSetting(values, HostProfile{}, std::forward<Func>(func));
}

/// A global setting that changed, with its values as BasicSetting::ToString gives them.
struct RecommendedSettingChange {
    const BasicSetting* setting;
    std::string previous;
    std::string current;
};

/// Writes the recommended values to the global settings and returns the ones that changed.
/// Per-game values, and whether a setting uses its per-game value, stay as they are. When
/// something changed, recommended_settings_backup takes the previous value of every recommended
/// setting it does not hold yet. A value it holds stays until it is restored, so applying twice
/// does not lose the values from before the first time.
std::vector<RecommendedSettingChange> ApplyRecommendedSettings(Values& values,
                                                               const HostProfile& host = {});

/// Puts back the global values recommended_settings_backup holds, clears it and returns the
/// settings that changed. Only settings in the recommended table are restored.
std::vector<RecommendedSettingChange> RestorePreviousSettings(Values& values);

} // namespace Settings
