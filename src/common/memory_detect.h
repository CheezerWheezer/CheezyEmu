// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "common/common_types.h"

namespace Common {

struct MemoryInfo {
    u64 TotalPhysicalMemory{};
    u64 TotalSwapMemory{};
};

/**
 * Gets the memory info of the host system
 * @return Reference to a MemoryInfo struct with the physical and swap memory sizes in bytes
 */
[[nodiscard]] const MemoryInfo& GetMemInfo();

/// This process's memory in bytes. Zero where the host does not report a value.
struct ProcessMemoryUsage {
    u64 commit{};              ///< Private commit (Windows PrivateUsage, Linux VmData).
    u64 working_set{};         ///< Resident memory.
    u64 private_working_set{}; ///< Resident memory not shared with other mappings.
    u64 system_commit{};       ///< Commit charge of the whole system (Windows).
    u64 system_commit_limit{}; ///< Commit limit of the whole system (Windows).
};

[[nodiscard]] ProcessMemoryUsage GetProcessMemoryUsage();

/// Memory the system can still hand out, in bytes. Windows: its commit limit (RAM plus the page
/// files) minus what is committed. macOS: free, inactive and purgeable memory. Linux:
/// MemAvailable. No value where it can't be read.
[[nodiscard]] std::optional<u64> GetSystemCommitRoom();

} // namespace Common
