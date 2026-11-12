// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <span>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/div_ceil.h"
#include "common/logging.h"
#include "video_core/renderer_vulkan/present/ffx_frame_interpolation.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

#include "video_core/host_shaders/ffx_fg_fi_disocclusion_mask_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_game_motion_vector_field_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_game_vector_field_inpainting_pyramid_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_game_vector_field_inpainting_pyramid_nowave_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_inpainting_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_inpainting_pyramid_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_inpainting_pyramid_nowave_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_interpolation_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_optical_flow_vector_field_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_reconstruct_and_dilate_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_reconstruct_previous_depth_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_fi_setup_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_compute_optical_flow_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_compute_scd_divergence_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_filter_optical_flow_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_generate_scd_histogram_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_luminance_pyramid_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_luminance_pyramid_nowave_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_prepare_luma_comp_spv.h"
#include "video_core/host_shaders/ffx_fg_of_scale_optical_flow_comp_spv.h"

namespace Vulkan {

namespace {

// cbOF (ffx_opticalflow_callbacks_glsl.h, std140; the SDK's OpticalflowConstants).
struct OpticalFlowConstants {
    std::array<s32, 2> input_luma_resolution;
    u32 pyramid_level;
    u32 pyramid_level_count;
    s32 frame_index;
    u32 backbuffer_transfer_function;
    std::array<f32, 2> min_max_luminance;
};
static_assert(sizeof(OpticalFlowConstants) == 32);

// cbOF_SPD.
struct OpticalFlowSpdConstants {
    u32 mips;
    u32 num_work_groups;
    std::array<u32, 2> work_group_offset;
    u32 num_work_groups_pyramid;
    std::array<u32, 3> pad;
};
static_assert(sizeof(OpticalFlowSpdConstants) == 32);

// cbFI (ffx_frameinterpolation_callbacks_glsl.h, std140; the SDK's FrameInterpolationConstants).
struct FrameInterpolationConstants {
    std::array<s32, 2> render_size;
    std::array<s32, 2> display_size;
    std::array<f32, 2> display_size_rcp;
    f32 camera_near;
    f32 camera_far;
    std::array<s32, 2> upscaler_target_size;
    s32 mode;
    s32 reset;
    std::array<f32, 4> device_to_view_depth;
    f32 delta_time;
    s32 hudless_attached;
    std::array<s32, 2> distortion_field_size;
    std::array<f32, 2> optical_flow_scale;
    s32 optical_flow_block_size;
    u32 dispatch_flags;
    std::array<s32, 2> max_render_size;
    s32 optical_flow_half_res_mode;
    s32 num_instances;
    std::array<s32, 2> interpolation_rect_base;
    std::array<s32, 2> interpolation_rect_size;
    std::array<f32, 3> debug_bar_color;
    u32 backbuffer_transfer_function;
    std::array<f32, 2> min_max_luminance;
    f32 tan_half_fov;
    s32 pad1;
    std::array<f32, 2> jitter;
    std::array<f32, 2> motion_vector_scale;
};
static_assert(sizeof(FrameInterpolationConstants) == 176);

// cbInpaintingPyramid.
struct PyramidConstants {
    u32 mips;
    u32 num_work_groups;
    std::array<u32, 2> work_group_offset;
};

/// ffxSpdSetup (ffx_spd.h): thread groups, first tile, work groups and mips for a rectangle.
struct Spd {
    u32 groups_x;
    u32 groups_y;
    u32 num_work_groups;
    u32 mips;
};
Spd SpdSetup(u32 width, u32 height, s32 mips = -1) {
    const u32 end_x = (width - 1) / 64;
    const u32 end_y = (height - 1) / 64;
    Spd spd{.groups_x = end_x + 1, .groups_y = end_y + 1, .num_work_groups = 0, .mips = 0};
    spd.num_work_groups = spd.groups_x * spd.groups_y;
    if (mips >= 0) {
        spd.mips = static_cast<u32>(mips);
    } else {
        const f32 resolution = static_cast<f32>((std::max)(width, height));
        spd.mips = static_cast<u32>((std::min)(std::floor(std::log2(resolution)), 12.0f));
    }
    return spd;
}

/// The SDK's setupDeviceDepthToViewSpaceDepthParams, for depth growing with distance (not
/// inverted, not infinite) and a view-space-to-meters factor of 1.
std::array<f32, 4> DeviceToViewDepth(const AmdFrameGen::CameraPlanes& camera, VkExtent2D render) {
    const f32 near_plane = (std::min)(camera.near_plane, camera.far_plane);
    const f32 far_plane = (std::max)(camera.near_plane, camera.far_plane);
    const f32 q = far_plane / (near_plane - far_plane);
    const f32 aspect = static_cast<f32>(render.width) / static_cast<f32>(render.height);
    const f32 cot_half_fov = std::cos(0.5f * camera.fov) / std::sin(0.5f * camera.fov);
    return {-q, q * near_plane, aspect / cot_half_fov, 1.0f / cot_half_fov};
}

constexpr VkMemoryBarrier MemoryBarrier(VkAccessFlags src, VkAccessFlags dst) {
    return VkMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src,
        .dstAccessMask = dst,
    };
}

constexpr VkImageSubresourceRange ALL_LEVELS{
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = VK_REMAINING_MIP_LEVELS,
    .baseArrayLayer = 0,
    .layerCount = 1,
};

/// Descriptor kinds: sampled image, storage image, uniform buffer, storage buffer.
enum class Kind : u8 { SampledImage, StorageImage, UniformBuffer, StorageBuffer };
constexpr Kind KTex = Kind::SampledImage;
constexpr Kind KImg = Kind::StorageImage;
constexpr Kind KUbo = Kind::UniformBuffer;
constexpr Kind KSsbo = Kind::StorageBuffer;
struct Binding {
    u32 index;
    Kind kind;
};

/// One push descriptor (the template reads them in binding order).
union Entry {
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
};

Entry Tex(VkImageView view) {
    return Entry{.image = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL}};
}
Entry Img(VkImageView view) {
    return Entry{.image = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL}};
}
Entry Buf(const VkDescriptorBufferInfo& info) {
    Entry entry;
    entry.buffer = info;
    return entry;
}

enum PassId : size_t {
    OfPrepareLuma,
    OfLuminancePyramid,
    OfScdHistogram,
    OfScdDivergence,
    OfSearch,
    OfFilter,
    OfScale,
    FiReconstructAndDilate,
    FiSetup,
    FiReconstructPreviousDepth,
    FiGameMotionVectorField,
    FiGameVectorFieldPyramid,
    FiOpticalFlowVectorField,
    FiDisocclusionMask,
    FiInterpolation,
    FiInpaintingPyramid,
    FiInpainting,
    PassCount,
};

constexpr u32 PYRAMID_BINDINGS = 13;

std::mutex active_mutex;
FfxFrameInterpolation* active{};

} // Anonymous namespace

class FfxFrameInterpolation::Pass {
public:
    Pass(const Device& device, std::span<const u32> code, std::vector<Binding> bindings_,
         VkSampler sampler)
        : bindings{std::move(bindings_)} {
        std::vector<VkDescriptorSetLayoutBinding> layout_bindings;
        std::vector<VkDescriptorUpdateTemplateEntry> entries;
        for (size_t i = 0; i < bindings.size(); ++i) {
            VkDescriptorType type{};
            switch (bindings[i].kind) {
            case Kind::SampledImage:
                type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case Kind::StorageImage:
                type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case Kind::UniformBuffer:
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case Kind::StorageBuffer:
                type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            }
            layout_bindings.push_back({
                .binding = bindings[i].index,
                .descriptorType = type,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr,
            });
            entries.push_back({
                .dstBinding = bindings[i].index,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = type,
                .offset = i * sizeof(Entry),
                .stride = sizeof(Entry),
            });
        }
        if (sampler != VK_NULL_HANDLE) {
            // s_LinearClamp of the frame interpolation passes.
            layout_bindings.push_back({
                .binding = 1000,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = &sampler,
            });
        }
        const auto& logical = device.GetLogical();
        set_layout = logical.CreateDescriptorSetLayout({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
            .bindingCount = static_cast<u32>(layout_bindings.size()),
            .pBindings = layout_bindings.data(),
        });
        const VkDescriptorSetLayout set_layout_handle = *set_layout;
        layout = logical.CreatePipelineLayout({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &set_layout_handle,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        });
        update_template = logical.CreateDescriptorUpdateTemplate({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .descriptorUpdateEntryCount = static_cast<u32>(entries.size()),
            .pDescriptorUpdateEntries = entries.data(),
            .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR,
            .descriptorSetLayout = *set_layout,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE,
            .pipelineLayout = *layout,
            .set = 0,
        });
        module = BuildShader(device, code);
        pipeline = logical.CreateComputePipeline({
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = *module,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            .layout = *layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        });
    }

    /// Records one dispatch: `entries` in the order of the pass's bindings.
    void Run(Scheduler& recorder, std::vector<Entry> entries, u32 x, u32 y, u32 z = 1) const {
        ASSERT(entries.size() == bindings.size());
        recorder.Record([this, data = std::move(entries), x, y, z](vk::CommandBuffer cmdbuf) {
            cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);
            cmdbuf.PushDescriptorSetWithTemplateKHR(*update_template, *layout, 0, data.data());
            cmdbuf.Dispatch((std::max)(x, 1U), (std::max)(y, 1U), (std::max)(z, 1U));
            // The next pass reads what this one wrote (everything stays in GENERAL).
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                   MemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT,
                                                 VK_ACCESS_SHADER_READ_BIT |
                                                     VK_ACCESS_SHADER_WRITE_BIT));
        });
    }

private:
    std::vector<Binding> bindings;
    vk::DescriptorSetLayout set_layout;
    vk::PipelineLayout layout;
    vk::DescriptorUpdateTemplate update_template;
    vk::ShaderModule module;
    vk::Pipeline pipeline;
};

bool FfxFrameInterpolation::IsSupported(const Device& device) {
    if (!device.IsKhrPushDescriptorSupported() || device.MaxPushDescriptors() < 20) {
        return false;
    }
    constexpr VkFormatFeatureFlags storage =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (const VkFormat format :
         {VK_FORMAT_R8_UINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R32_UINT, VK_FORMAT_R32_SFLOAT,
          VK_FORMAT_R8G8_UNORM, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT}) {
        if (!device.IsFormatSupported(format, storage, FormatType::Optimal)) {
            return false;
        }
    }
    return true;
}

FfxFrameInterpolation::FfxFrameInterpolation(const Device& device_,
                                             MemoryAllocator& memory_allocator_,
                                             Scheduler& scheduler_, VkExtent2D display_size,
                                             VkFormat frame_format)
    : device{device_}, memory_allocator{memory_allocator_}, scheduler{scheduler_},
      display{display_size}, format{frame_format} {
    linear_sampler = device.GetLogical().CreateSampler({
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_NEVER,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    });

    // The pass files' bindings (FFX_*_BIND_* in each), in order.
    const bool waves = device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_BASIC_BIT) &&
                       device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) &&
                       device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_SHUFFLE_BIT);
    const auto pyramid_bindings = [](u32 first) {
        std::vector<Binding> result;
        for (u32 i = 0; i < PYRAMID_BINDINGS; ++i) {
            result.push_back({first + i, KImg});
        }
        return result;
    };
    const auto concat = [](std::vector<Binding> a, const std::vector<Binding>& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };
    const VkSampler none = VK_NULL_HANDLE;
    const VkSampler linear = *linear_sampler;
    passes.resize(PassCount);
    passes[OfPrepareLuma] = std::make_unique<Pass>(
        device, FFX_FG_OF_PREPARE_LUMA_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KImg}, {2, KUbo}}, none);
    passes[OfLuminancePyramid] = std::make_unique<Pass>(
        device,
        waves ? std::span<const u32>(FFX_FG_OF_LUMINANCE_PYRAMID_COMP_SPV)
              : std::span<const u32>(FFX_FG_OF_LUMINANCE_PYRAMID_NOWAVE_COMP_SPV),
        std::vector<Binding>{{0, KImg}, {1, KImg}, {2, KImg}, {3, KImg}, {4, KImg},
                             {5, KImg}, {6, KImg}, {7, KUbo}, {8, KUbo}},
        none);
    passes[OfScdHistogram] = std::make_unique<Pass>(
        device, FFX_FG_OF_GENERATE_SCD_HISTOGRAM_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KImg}, {2, KUbo}}, none);
    passes[OfScdDivergence] = std::make_unique<Pass>(
        device, FFX_FG_OF_COMPUTE_SCD_DIVERGENCE_COMP_SPV,
        std::vector<Binding>{{0, KImg}, {1, KImg}, {2, KImg}, {3, KImg}, {4, KUbo}}, none);
    passes[OfSearch] = std::make_unique<Pass>(
        device, FFX_FG_OF_COMPUTE_OPTICAL_FLOW_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KImg}, {3, KImg}, {4, KUbo}},
        none);
    passes[OfFilter] = std::make_unique<Pass>(
        device, FFX_FG_OF_FILTER_OPTICAL_FLOW_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KImg}, {2, KUbo}}, none);
    passes[OfScale] = std::make_unique<Pass>(
        device, FFX_FG_OF_SCALE_OPTICAL_FLOW_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KTex}, {3, KImg}, {4, KImg},
                             {5, KUbo}},
        none);
    passes[FiReconstructAndDilate] = std::make_unique<Pass>(
        device, FFX_FG_FI_RECONSTRUCT_AND_DILATE_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KImg}, {3, KImg}, {4, KImg},
                             {5, KUbo}},
        linear);
    passes[FiSetup] = std::make_unique<Pass>(
        device, FFX_FG_FI_SETUP_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KImg}, {2, KImg}, {3, KImg}, {4, KImg},
                             {5, KImg}, {6, KSsbo}, {7, KUbo}},
        linear);
    passes[FiReconstructPreviousDepth] = std::make_unique<Pass>(
        device, FFX_FG_FI_RECONSTRUCT_PREVIOUS_DEPTH_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KTex}, {3, KTex}, {4, KImg},
                             {5, KUbo}},
        linear);
    passes[FiGameMotionVectorField] = std::make_unique<Pass>(
        device, FFX_FG_FI_GAME_MOTION_VECTOR_FIELD_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KTex}, {3, KTex}, {4, KTex},
                             {5, KImg}, {6, KImg}, {7, KUbo}},
        linear);
    passes[FiGameVectorFieldPyramid] = std::make_unique<Pass>(
        device,
        waves ? std::span<const u32>(FFX_FG_FI_GAME_VECTOR_FIELD_INPAINTING_PYRAMID_COMP_SPV)
              : std::span<const u32>(FFX_FG_FI_GAME_VECTOR_FIELD_INPAINTING_PYRAMID_NOWAVE_COMP_SPV),
        concat(concat({{0, KTex}, {1, KTex}, {2, KSsbo}}, pyramid_bindings(3)),
               {{16, KUbo}, {17, KUbo}}),
        linear);
    passes[FiOpticalFlowVectorField] = std::make_unique<Pass>(
        device, FFX_FG_FI_OPTICAL_FLOW_VECTOR_FIELD_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KTex}, {3, KTex}, {4, KTex},
                             {6, KImg}, {7, KImg}, {8, KUbo}},
        linear);
    passes[FiDisocclusionMask] = std::make_unique<Pass>(
        device, FFX_FG_FI_DISOCCLUSION_MASK_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KTex}, {3, KTex}, {4, KTex},
                             {5, KTex}, {6, KTex}, {7, KImg}, {8, KUbo}},
        linear);
    passes[FiInterpolation] = std::make_unique<Pass>(
        device, FFX_FG_FI_INTERPOLATION_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KTex}, {3, KTex}, {4, KTex},
                             {5, KTex}, {6, KTex}, {7, KTex}, {8, KSsbo}, {9, KImg},
                             {10, KUbo}},
        linear);
    passes[FiInpaintingPyramid] = std::make_unique<Pass>(
        device,
        waves ? std::span<const u32>(FFX_FG_FI_INPAINTING_PYRAMID_COMP_SPV)
              : std::span<const u32>(FFX_FG_FI_INPAINTING_PYRAMID_NOWAVE_COMP_SPV),
        concat(concat({{0, KTex}, {1, KSsbo}}, pyramid_bindings(2)),
               {{15, KUbo}, {16, KUbo}}),
        linear);
    passes[FiInpainting] = std::make_unique<Pass>(
        device, FFX_FG_FI_INPAINTING_COMP_SPV,
        std::vector<Binding>{{0, KTex}, {1, KTex}, {2, KTex}, {3, KTex}, {7, KImg},
                             {8, KUbo}},
        linear);

    // Optical flow images (ffx_opticalflow.cpp opticalflowCreate).
    std::vector<VkImage> new_images;
    const auto make = [&](Image& image, VkFormat image_format, u32 width, u32 height,
                          u32 levels = 1, bool storage = true) {
        image = CreateImage(image_format, width, height, levels, storage);
        new_images.push_back(*image.image);
    };
    for (size_t side = 0; side < 2; ++side) {
        for (u32 level = 0; level < 7; ++level) {
            make(luma[side][level], VK_FORMAT_R8_UINT, (std::max)(display.width >> level, 1U),
                 (std::max)(display.height >> level, 1U));
        }
    }
    flow_sizes[0] = {Common::DivCeil(display.width, 8U), Common::DivCeil(display.height, 8U)};
    for (size_t level = 1; level < 7; ++level) {
        flow_sizes[level] = {(flow_sizes[level - 1].width + 1) / 2,
                             (flow_sizes[level - 1].height + 1) / 2};
    }
    for (size_t side = 0; side < 2; ++side) {
        for (size_t level = 0; level < 7; ++level) {
            make(flow[side][level], VK_FORMAT_R16G16_SINT, flow_sizes[level].width,
                 flow_sizes[level].height);
        }
    }
    constexpr u32 SCD_HISTOGRAM_WIDTH = 256 * 3 * 3;
    make(scd_histogram, VK_FORMAT_R32_UINT, SCD_HISTOGRAM_WIDTH, 1);
    make(scd_previous_histogram, VK_FORMAT_R32_SFLOAT, SCD_HISTOGRAM_WIDTH, 1);
    make(scd_temp, VK_FORMAT_R32_UINT, 3, 1);
    make(flow_vectors, VK_FORMAT_R16G16_SINT, flow_sizes[0].width, flow_sizes[0].height);
    make(scd_output, VK_FORMAT_R32_UINT, 3, 1);

    // Frame interpolation images at the display size (frameinterpolationCreate).
    for (Image& source : sources) {
        make(source, format, display.width, display.height, 1, false);
    }
    make(with_hud, format, display.width, display.height, 1, false);
    const u32 pyramid_width = (std::max)(display.width / 2, 1U);
    const u32 pyramid_height = (std::max)(display.height / 2, 1U);
    const u32 pyramid_levels = static_cast<u32>(std::floor(std::log2(
                                   static_cast<f32>((std::max)(pyramid_width, pyramid_height))))) +
                               1;
    make(inpainting_pyramid, VK_FORMAT_R16G16B16A16_SFLOAT, pyramid_width, pyramid_height,
         pyramid_levels);
    make(distortion_field, VK_FORMAT_R8G8_UNORM, 1, 1);
    make(zero_confidence, VK_FORMAT_R32_UINT, 1, 1);
    counters = memory_allocator.CreateBuffer(
        VkBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = 16,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        },
        MemoryUsage::DeviceLocal);
    uniform_buffer = memory_allocator.CreateBuffer(
        VkBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = 8 * 32768,
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        },
        MemoryUsage::Upload);
    uniform_mapped = uniform_buffer.Mapped();
    InitializeImages(std::move(new_images));

    {
        std::scoped_lock lock{active_mutex};
        active = this;
    }
    LOG_INFO(Render_Vulkan,
             "FSR 3.1.4 frame generation (built from the FidelityFX SDK): {}x{}, {} subgroup "
             "operations",
             display.width, display.height, waves ? "with" : "without");
}

FfxFrameInterpolation::~FfxFrameInterpolation() {
    std::scoped_lock lock{active_mutex};
    if (active == this) {
        active = nullptr;
    }
}

FfxFrameInterpolation::Image FfxFrameInterpolation::CreateImage(VkFormat image_format, u32 width,
                                                                u32 height, u32 levels,
                                                                bool storage) {
    Image result;
    result.format = image_format;
    result.extent = {(std::max)(width, 1U), (std::max)(height, 1U)};
    result.image = memory_allocator.CreateImage(VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = image_format,
        .extent = {result.extent.width, result.extent.height, 1},
        .mipLevels = levels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = static_cast<VkImageUsageFlags>(
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0)),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    });
    const auto view = [&](u32 base_level, u32 level_count) {
        return device.GetLogical().CreateImageView(VkImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = *result.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image_format,
            .components{},
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = base_level,
                .levelCount = level_count,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        });
    };
    result.view = view(0, levels);
    if (levels > 1) {
        for (u32 level = 0; level < levels; ++level) {
            result.levels.push_back(view(level, 1));
        }
    }
    return result;
}

void FfxFrameInterpolation::InitializeImages(std::vector<VkImage> images) {
    // New images: GENERAL, cleared to zero (what the SDK's "uninitialized" reads as).
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([fresh = std::move(images)](vk::CommandBuffer cmdbuf) {
        for (const VkImage image : fresh) {
            const VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = ALL_LEVELS,
            };
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, barrier);
            cmdbuf.ClearColorImage(image, VK_IMAGE_LAYOUT_GENERAL, VkClearColorValue{},
                                   ALL_LEVELS);
        }
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               0,
                               MemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                             VK_ACCESS_SHADER_READ_BIT |
                                                 VK_ACCESS_SHADER_WRITE_BIT |
                                                 VK_ACCESS_TRANSFER_READ_BIT |
                                                 VK_ACCESS_TRANSFER_WRITE_BIT));
    });
}

void FfxFrameInterpolation::CreateRenderSizeImages(VkExtent2D render_size) {
    if (dilated_depth.image && render.width == render_size.width &&
        render.height == render_size.height) {
        return;
    }
    if (dilated_depth.image) {
        scheduler.Finish(); // the old images may still be in use
    }
    render = render_size;
    std::vector<VkImage> new_images;
    const auto make = [&](Image& image, VkFormat image_format) {
        image = CreateImage(image_format, render.width, render.height, 1, true);
        new_images.push_back(*image.image);
    };
    make(dilated_depth, VK_FORMAT_R32_SFLOAT);
    make(dilated_motion, VK_FORMAT_R16G16_SFLOAT);
    make(reconstructed_previous_depth, VK_FORMAT_R32_UINT);
    make(reconstructed_interpolated_depth, VK_FORMAT_R32_UINT);
    make(game_vectors_x, VK_FORMAT_R32_UINT);
    make(game_vectors_y, VK_FORMAT_R32_UINT);
    make(flow_vectors_x, VK_FORMAT_R32_UINT);
    make(flow_vectors_y, VK_FORMAT_R32_UINT);
    make(disocclusion, VK_FORMAT_R8G8_UNORM);
    InitializeImages(std::move(new_images));
    game_reset = true;
}

void FfxFrameInterpolation::BeginUniforms() {
    region = (region + 1) % region_ticks.size();
    if (!scheduler.IsFree(region_ticks[region])) {
        scheduler.Wait(region_ticks[region]);
    }
    region_offset = 0;
    region_ticks[region] = scheduler.CurrentTick();
}

VkDescriptorBufferInfo FfxFrameInterpolation::Uniform(const void* data, size_t size) {
    constexpr size_t REGION_SIZE = 32768;
    const size_t alignment = (std::max<size_t>)(device.GetUniformBufferAlignment(), 16);
    const size_t slot = Common::AlignUp(size, alignment);
    ASSERT(region_offset + slot <= REGION_SIZE);
    const size_t offset = region * REGION_SIZE + region_offset;
    std::memcpy(uniform_mapped.data() + offset, data, size);
    region_offset += slot;
    return VkDescriptorBufferInfo{*uniform_buffer, offset, slot};
}

void FfxFrameInterpolation::PrepareActive(const AmdFrameGen::PrepareInputs& inputs) {
    std::scoped_lock lock{active_mutex};
    if (active != nullptr) {
        active->Prepare(inputs);
    }
}

void FfxFrameInterpolation::Prepare(const AmdFrameGen::PrepareInputs& inputs) {
    if (inputs.render_size.width == 0 || inputs.render_size.height == 0 ||
        inputs.render_size.width > display.width * 2 ||
        inputs.render_size.height > display.height * 2) {
        return;
    }
    CreateRenderSizeImages(inputs.render_size);
    const auto now = std::chrono::steady_clock::now();
    if (last_prepare != std::chrono::steady_clock::time_point{}) {
        frame_time_ms = std::clamp(
            std::chrono::duration<f32, std::milli>(now - last_prepare).count(), 1.0f, 100.0f);
    }
    last_prepare = now;
    jitter = inputs.jitter;
    camera = AmdFrameGen::ExtractCameraPlanes(inputs.view_projection);
    game_reset = game_reset || inputs.reset;

    // Views of the game's depth and motion, kept until the GPU has used them.
    while (!input_views.empty() && scheduler.IsFree(input_views.front().first)) {
        input_views.pop_front();
    }
    const auto input_view = [&](const AmdFrameGen::PrepareImage& input) {
        vk::ImageView view = device.GetLogical().CreateImageView(VkImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = input.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = input.format,
            .components{},
            .subresourceRange{
                .aspectMask = input.aspect,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        });
        const VkImageView handle = *view;
        input_views.emplace_back(scheduler.CurrentTick(), std::move(view));
        return handle;
    };
    const VkImageView depth_view = input_view(inputs.depth);
    const VkImageView motion_view = input_view(inputs.motion_vectors);

    FrameInterpolationConstants constants{};
    constants.render_size = {static_cast<s32>(render.width), static_cast<s32>(render.height)};
    constants.display_size = {static_cast<s32>(display.width), static_cast<s32>(display.height)};
    constants.display_size_rcp = {1.0f / display.width, 1.0f / display.height};
    constants.max_render_size = constants.render_size;
    constants.camera_near = camera.near_plane;
    constants.camera_far = camera.far_plane;
    constants.device_to_view_depth = DeviceToViewDepth(camera, render);
    constants.jitter = jitter;
    // The motion is in render pixels (motion vector scale 1): the SDK divides by the render size.
    constants.motion_vector_scale = {1.0f / render.width, 1.0f / render.height};
    constants.interpolation_rect_size = constants.display_size;
    constants.upscaler_target_size = constants.display_size;
    constants.distortion_field_size = {1, 1};

    scheduler.RequestOutsideRenderPassOperationContext();
    BeginUniforms();
    const VkDescriptorBufferInfo cb = Uniform(&constants, sizeof(constants));
    // The reconstructed previous depth starts at the far plane (1, depth growing with distance).
    scheduler.Record([image = *reconstructed_previous_depth.image](vk::CommandBuffer cmdbuf) {
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,
                               MemoryBarrier(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                             VK_ACCESS_TRANSFER_WRITE_BIT));
        VkClearColorValue far_depth{};
        far_depth.float32[0] = 1.0f;
        cmdbuf.ClearColorImage(image, VK_IMAGE_LAYOUT_GENERAL, far_depth, ALL_LEVELS);
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                               MemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT |
                                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                                 VK_ACCESS_SHADER_WRITE_BIT,
                                             VK_ACCESS_SHADER_READ_BIT |
                                                 VK_ACCESS_SHADER_WRITE_BIT));
    });
    passes[FiReconstructAndDilate]->Run(
        scheduler,
        {Tex(motion_view), Tex(depth_view), Img(*reconstructed_previous_depth.view),
         Img(*dilated_motion.view), Img(*dilated_depth.view), Buf(cb)},
        Common::DivCeil(render.width, 8U), Common::DivCeil(render.height, 8U));
    scheduler.InvalidateState();
    game_frame_prepared = true;
    ++prepared_frames;
}

void FfxFrameInterpolation::RunOpticalFlow(VkImageView current, bool reset) {
    if (reset) {
        of_frame_index = 0;
        std::vector<VkImage> cleared{*scd_temp.image, *scd_output.image, *scd_histogram.image,
                                     *scd_previous_histogram.image};
        for (auto& side : luma) {
            for (Image& level : side) {
                cleared.push_back(*level.image);
            }
        }
        scheduler.Record([to_clear = std::move(cleared)](vk::CommandBuffer cmdbuf) {
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                   MemoryBarrier(VK_ACCESS_SHADER_READ_BIT |
                                                     VK_ACCESS_SHADER_WRITE_BIT,
                                                 VK_ACCESS_TRANSFER_WRITE_BIT));
            for (const VkImage image : to_clear) {
                cmdbuf.ClearColorImage(image, VK_IMAGE_LAYOUT_GENERAL, VkClearColorValue{},
                                       ALL_LEVELS);
            }
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                   MemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                                 VK_ACCESS_SHADER_READ_BIT |
                                                     VK_ACCESS_SHADER_WRITE_BIT));
        });
    } else {
        ++of_frame_index;
    }

    constexpr u32 LEVELS = 7;
    OpticalFlowConstants constants{
        .input_luma_resolution = {static_cast<s32>(display.width),
                                  static_cast<s32>(display.height)},
        .pyramid_level = 0,
        .pyramid_level_count = LEVELS,
        .frame_index = static_cast<s32>(of_frame_index),
        .backbuffer_transfer_function = 0, // LDR (sRGB) presented frames
        .min_max_luminance = {0.0f, 1.0f},
    };
    const Spd spd = SpdSetup(display.width, display.height, 4);
    const OpticalFlowSpdConstants spd_constants{
        .mips = spd.mips,
        .num_work_groups = spd.num_work_groups,
        .work_group_offset = {0, 0},
        .num_work_groups_pyramid = spd.num_work_groups,
        .pad = {},
    };
    const VkDescriptorBufferInfo cb = Uniform(&constants, sizeof(constants));
    const VkDescriptorBufferInfo cb_spd = Uniform(&spd_constants, sizeof(spd_constants));

    const bool odd_frame = (of_parity & 1) != 0;
    const size_t cur = odd_frame ? 1 : 0;
    const size_t prev = cur ^ 1;
    const u32 w = display.width;
    const u32 h = display.height;

    passes[OfPrepareLuma]->Run(scheduler, {Tex(current), Img(*luma[cur][0].view), Buf(cb)},
                               ((w + 1) / 2 + 15) / 16, ((h + 1) / 2 + 15) / 16);
    passes[OfLuminancePyramid]->Run(
        scheduler,
        {Img(*luma[cur][0].view), Img(*luma[cur][1].view), Img(*luma[cur][2].view),
         Img(*luma[cur][3].view), Img(*luma[cur][4].view), Img(*luma[cur][5].view),
         Img(*luma[cur][6].view), Buf(cb), Buf(cb_spd)},
        spd.groups_x, spd.groups_y);
    passes[OfScdHistogram]->Run(scheduler,
                                {Tex(*luma[cur][0].view), Img(*scd_histogram.view), Buf(cb)},
                                ((w / 4) / 3 + 31) / 32, 16, 9);
    passes[OfScdDivergence]->Run(scheduler,
                                 {Img(*scd_histogram.view), Img(*scd_previous_histogram.view),
                                  Img(*scd_temp.view), Img(*scd_output.view), Buf(cb)},
                                 9, 3);

    for (s32 level = LEVELS - 1; level >= 0; --level) {
        const bool odd_level = (level & 1) != 0;
        const size_t a = odd_frame != odd_level ? 1 : 0;
        const size_t b = a ^ 1;
        constants.pyramid_level = static_cast<u32>(level);
        const VkDescriptorBufferInfo cb_level = Uniform(&constants, sizeof(constants));
        const u32 luma_w = (std::max)(w >> level, 1U);
        const u32 luma_h = (std::max)(h >> level, 1U);
        passes[OfSearch]->Run(scheduler,
                              {Tex(*luma[cur][level].view), Tex(*luma[prev][level].view),
                               Img(*flow[a][level].view), Img(*scd_output.view), Buf(cb_level)},
                              ((luma_w + 3) / 4 * 16 + 63) / 64, (luma_h + 15) / 16);
        const VkImageView filtered = level == 0 ? *flow_vectors.view : *flow[b][level].view;
        passes[OfFilter]->Run(scheduler, {Tex(*flow[a][level].view), Img(filtered), Buf(cb_level)},
                              (flow_sizes[level].width + 15) / 16,
                              (flow_sizes[level].height + 3) / 4);
        if (level > 0) {
            passes[OfScale]->Run(scheduler,
                                 {Tex(*luma[cur][level].view), Tex(*luma[prev][level].view),
                                  Tex(*flow[b][level].view), Img(*flow[b][level - 1].view),
                                  Img(*scd_output.view), Buf(cb_level)},
                                 (flow_sizes[level - 1].width + 3) / 4,
                                 (flow_sizes[level - 1].height + 3) / 4);
        }
    }
    ++of_parity;
}

void FfxFrameInterpolation::Process(VkImage frame, VkImage hudless) {
    game_frame_for_present = game_frame_prepared;
    game_frame_prepared = false;
    current_source ^= 1;
    // A switch between frames with and without a HUD-less copy starts over (the previous source
    // would be the other kind).
    const bool apart = hudless != VK_NULL_HANDLE;
    const bool reset = !have_previous || apart != hud_apart;
    hud_apart = apart;
    scheduler.RequestOutsideRenderPassOperationContext();
    BeginUniforms();
    scheduler.Record([frame, hudless, destination = *sources[current_source].image,
                      hud_copy = *with_hud.image, extent = display](vk::CommandBuffer cmdbuf) {
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,
                               MemoryBarrier(VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                             VK_ACCESS_TRANSFER_READ_BIT |
                                                 VK_ACCESS_TRANSFER_WRITE_BIT));
        const VkImageCopy whole{
            .srcSubresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .srcOffset{0, 0, 0},
            .dstSubresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstOffset{0, 0, 0},
            .extent{extent.width, extent.height, 1},
        };
        if (hudless != VK_NULL_HANDLE) {
            cmdbuf.CopyImage(hudless, VK_IMAGE_LAYOUT_GENERAL, destination,
                             VK_IMAGE_LAYOUT_GENERAL, whole);
            cmdbuf.CopyImage(frame, VK_IMAGE_LAYOUT_GENERAL, hud_copy, VK_IMAGE_LAYOUT_GENERAL,
                             whole);
        } else {
            cmdbuf.CopyImage(frame, VK_IMAGE_LAYOUT_GENERAL, destination,
                             VK_IMAGE_LAYOUT_GENERAL, whole);
        }
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                               MemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                             VK_ACCESS_SHADER_READ_BIT));
    });
    RunOpticalFlow(*sources[current_source].view, reset);
    have_previous = true;
}

void FfxFrameInterpolation::Generate(VkImage destination, VkImageView storage_view,
                                     VkImageView sampled_view) {
    if (!dilated_depth.image) {
        return;
    }
    const bool reset = game_reset;
    game_reset = false;
    const Image& previous = sources[current_source ^ 1];
    const Image& current = sources[current_source];

    FrameInterpolationConstants constants{};
    constants.render_size = {static_cast<s32>(render.width), static_cast<s32>(render.height)};
    constants.display_size = {static_cast<s32>(display.width), static_cast<s32>(display.height)};
    constants.display_size_rcp = {1.0f / display.width, 1.0f / display.height};
    constants.max_render_size = constants.render_size;
    constants.camera_near = camera.near_plane;
    constants.camera_far = camera.far_plane;
    constants.device_to_view_depth = DeviceToViewDepth(camera, render);
    constants.upscaler_target_size = constants.display_size;
    constants.mode = 0;
    constants.reset = reset ? 1 : 0;
    constants.delta_time = frame_time_ms;
    constants.hudless_attached = hud_apart ? 1 : 0;
    constants.distortion_field_size = {1, 1};
    constants.optical_flow_scale = {1.0f / display.width, 1.0f / display.height};
    constants.optical_flow_block_size = 8;
    constants.interpolation_rect_base = {0, 0};
    constants.interpolation_rect_size = constants.display_size;
    constants.backbuffer_transfer_function = 0;
    constants.min_max_luminance = {0.0f, 1.0f};
    const f32 aspect = static_cast<f32>(render.width) / static_cast<f32>(render.height);
    const f32 horizontal_fov = std::atan(std::tan(camera.fov / 2) * aspect) * 2;
    constants.tan_half_fov = std::tan(horizontal_fov * 0.5f);
    constants.jitter = jitter;
    constants.motion_vector_scale = {1.0f / render.width, 1.0f / render.height};

    scheduler.RequestOutsideRenderPassOperationContext();
    BeginUniforms();
    const VkDescriptorBufferInfo cb = Uniform(&constants, sizeof(constants));
    const auto pyramid_cb = [&](u32 width, u32 height) {
        const Spd spd = SpdSetup(width, height);
        const PyramidConstants pyramid{
            .mips = spd.mips,
            .num_work_groups = spd.num_work_groups,
            .work_group_offset = {0, 0},
        };
        return std::make_pair(spd, Uniform(&pyramid, sizeof(pyramid)));
    };
    const auto pyramid_mips = [&] {
        std::vector<Entry> mips;
        for (u32 i = 0; i < PYRAMID_BINDINGS; ++i) {
            const size_t level = (std::min<size_t>)(i, inpainting_pyramid.levels.size() - 1);
            mips.push_back(Img(*inpainting_pyramid.levels[level]));
        }
        return mips;
    };
    const VkDescriptorBufferInfo counters_info{*counters, 0, 16};
    const u32 render_x = Common::DivCeil(render.width, 8U);
    const u32 render_y = Common::DivCeil(render.height, 8U);
    const u32 display_x = Common::DivCeil(display.width, 8U);
    const u32 display_y = Common::DivCeil(display.height, 8U);

    scheduler.Record([counters_handle = *counters,
                      interpolated = *reconstructed_interpolated_depth.image,
                      clear_depth = !reset](vk::CommandBuffer cmdbuf) {
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,
                               MemoryBarrier(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                             VK_ACCESS_TRANSFER_WRITE_BIT));
        cmdbuf.FillBuffer(counters_handle, 0, 16, 0);
        if (clear_depth) {
            VkClearColorValue far_depth{};
            far_depth.float32[0] = 1.0f;
            cmdbuf.ClearColorImage(interpolated, VK_IMAGE_LAYOUT_GENERAL, far_depth, ALL_LEVELS);
        }
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                               MemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                             VK_ACCESS_SHADER_READ_BIT |
                                                 VK_ACCESS_SHADER_WRITE_BIT));
    });

    passes[FiSetup]->Run(scheduler,
                         {Tex(*scd_output.view), Img(*game_vectors_x.view),
                          Img(*game_vectors_y.view), Img(*flow_vectors_x.view),
                          Img(*flow_vectors_y.view), Img(*disocclusion.view), Buf(counters_info),
                          Buf(cb)},
                         render_x, render_y);
    if (!reset) {
        passes[FiReconstructPreviousDepth]->Run(
            scheduler,
            {Tex(*dilated_motion.view), Tex(*dilated_depth.view), Tex(*current.view),
             Tex(*distortion_field.view), Img(*reconstructed_interpolated_depth.view), Buf(cb)},
            render_x, render_y);
        passes[FiGameMotionVectorField]->Run(
            scheduler,
            {Tex(*dilated_motion.view), Tex(*dilated_depth.view), Tex(*previous.view),
             Tex(*current.view), Tex(*distortion_field.view), Img(*game_vectors_x.view),
             Img(*game_vectors_y.view), Buf(cb)},
            render_x, render_y);
        {
            const auto [spd, cb_pyramid] = pyramid_cb(render.width, render.height);
            std::vector<Entry> entries{Tex(*game_vectors_x.view), Tex(*game_vectors_y.view),
                                       Buf(counters_info)};
            const auto mips = pyramid_mips();
            entries.insert(entries.end(), mips.begin(), mips.end());
            entries.push_back(Buf(cb));
            entries.push_back(Buf(cb_pyramid));
            passes[FiGameVectorFieldPyramid]->Run(scheduler, std::move(entries), spd.groups_x,
                                                  spd.groups_y);
        }
        passes[FiOpticalFlowVectorField]->Run(
            scheduler,
            {Tex(*flow_vectors.view), Tex(*zero_confidence.view), Tex(*dilated_depth.view),
             Tex(*previous.view), Tex(*current.view), Img(*flow_vectors_x.view),
             Img(*flow_vectors_y.view), Buf(cb)},
            (display.width / 8 + 7) / 8, (display.height / 8 + 7) / 8);
        passes[FiDisocclusionMask]->Run(
            scheduler,
            {Tex(*game_vectors_x.view), Tex(*game_vectors_y.view),
             Tex(*reconstructed_previous_depth.view), Tex(*dilated_depth.view),
             Tex(*reconstructed_interpolated_depth.view), Tex(*inpainting_pyramid.view),
             Tex(*distortion_field.view), Img(*disocclusion.view), Buf(cb)},
            render_x, render_y);
    }
    passes[FiInterpolation]->Run(
        scheduler,
        {Tex(*game_vectors_x.view), Tex(*game_vectors_y.view), Tex(*flow_vectors_x.view),
         Tex(*flow_vectors_y.view), Tex(*previous.view), Tex(*current.view),
         Tex(*disocclusion.view), Tex(*inpainting_pyramid.view), Buf(counters_info),
         Img(storage_view), Buf(cb)},
        display_x, display_y);
    {
        const auto [spd, cb_pyramid] = pyramid_cb(display.width, display.height);
        std::vector<Entry> entries{Tex(sampled_view), Buf(counters_info)};
        const auto mips = pyramid_mips();
        entries.insert(entries.end(), mips.begin(), mips.end());
        entries.push_back(Buf(cb));
        entries.push_back(Buf(cb_pyramid));
        passes[FiInpaintingPyramid]->Run(scheduler, std::move(entries), spd.groups_x,
                                         spd.groups_y);
    }
    // The interpolation source, then the frame as presented: where they differ (the HUD) the
    // presented frame's pixels are kept.
    passes[FiInpainting]->Run(scheduler,
                              {Tex(*scd_output.view), Tex(*inpainting_pyramid.view),
                               Tex(*current.view),
                               Tex(hud_apart ? *with_hud.view : *current.view),
                               Img(storage_view), Buf(cb)},
                              display_x, display_y);
    (void)destination;
}

} // namespace Vulkan
