// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "common/logging.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"

namespace Vulkan {

/// Debugging: EDEN_NX2_SPIRV_OVERRIDE=<folder> uses <folder>/<shader hash>.spv in place of the
/// translated shader (an edited, recompiled copy of a dump with the same interface). Nothing
/// happens while the variable is unset.
inline void OverrideSpirv(u64 shader_hash, std::vector<u32>& code) {
    static const char* const folder = std::getenv("EDEN_NX2_SPIRV_OVERRIDE");
    if (folder == nullptr || folder[0] == '\0') {
        return;
    }
    const std::filesystem::path path =
        std::filesystem::path{folder} / fmt::format("{:016x}.spv", shader_hash);
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return;
    }
    const auto size = static_cast<size_t>(file.tellg());
    if (size == 0 || size % sizeof(u32) != 0) {
        LOG_ERROR(Render_Vulkan, "SPIR-V override {} has a bad size", path.string());
        return;
    }
    std::vector<u32> replacement(size / sizeof(u32));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(replacement.data()), static_cast<std::streamsize>(size));
    if (!file) {
        return;
    }
    LOG_WARNING(Render_Vulkan, "SPIR-V override: shader {:016x} from {}", shader_hash,
                path.string());
    code = std::move(replacement);
}

inline std::vector<u32> EmitPipelineShader(const Shader::Profile& profile,
                                           const Shader::RuntimeInfo& runtime_info,
                                           Shader::IR::Program& program,
                                           Shader::Backend::Bindings& binding, u64 shader_hash,
                                           std::span<const u32> reference = {}) {
    std::vector<u32> code{
        Shader::Backend::SPIRV::EmitSPIRV(profile, runtime_info, program, binding)};
    if (!reference.empty() && !std::ranges::equal(code, reference)) {
        LOG_ERROR(Render_Vulkan,
                  "Nx2Perf async_pipelines: shader {:016x} worker SPIR-V differs; "
                  "using GPU-thread SPIR-V",
                  shader_hash);
        code.assign(reference.begin(), reference.end());
    }
    OverrideSpirv(shader_hash, code);
    return code;
}

} // namespace Vulkan
