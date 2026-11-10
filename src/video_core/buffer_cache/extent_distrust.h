// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>

#include "common/common_types.h"
#include "common/fs/path_util.h"
#include "common/logging.h"

// Compute programs known to write past the extent their store analysis proves (BufferCache's
// ExtentTrust). Their storage bindings are marked GPU-written in full from their first dispatch:
// a program that writes far only now and then passes a single check, and by the time a later
// check catches it the game has already read stale bytes back (Ocarina of Time's huge glowing
// triangles and white flashes, which then stay until the game is restarted).
//
// The list is the programs built in below plus those this PC's checks caught in earlier sessions,
// kept in <shader folder>/extent_distrust.txt (one hash per line; delete the file to forget them).
namespace VideoCommon::ExtentDistrust {

/// Caught on an RX 6750 XT in Kakariko Village, 2026-11-10: 16380 and 65528 bytes proven, bytes
/// written about 2 MiB and 1 MiB past the pointer; the third (16376 proven, written from +8 on)
/// passed its first checks and was found by an audit five minutes in.
inline constexpr std::array<u64, 3> BuiltIn{
    0x2de1ad413354e217ULL,
    0x20bf08b31399735cULL,
    0x4190a05452684c96ULL,
};

struct State {
    std::mutex mutex;
    std::unordered_set<u64> known;
    bool loaded{};
};

[[nodiscard]] inline State& GetState() {
    static State state;
    return state;
}

[[nodiscard]] inline std::filesystem::path FilePath() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) / "extent_distrust.txt";
}

/// `state.mutex` held.
inline void Load(State& state) {
    if (state.loaded) {
        return;
    }
    state.loaded = true;
    // EDEN_NX2_EXTENT_KNOWN=0: start with no known programs (tests of the checks themselves).
    if (const char* value = std::getenv("EDEN_NX2_EXTENT_KNOWN"); value != nullptr && value[0] == '0') {
        LOG_INFO(HW_GPU, "Compute write extents: EDEN_NX2_EXTENT_KNOWN=0, no program is known");
        return;
    }
    state.known.insert(BuiltIn.begin(), BuiltIn.end());
    size_t saved = 0;
    std::ifstream file{FilePath()};
    for (std::string line; std::getline(file, line);) {
        u64 hash{};
        const auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), hash, 16);
        if (error == std::errc{} && end != line.data() && hash != 0) {
            saved += state.known.insert(hash).second ? 1 : 0;
        }
    }
    LOG_INFO(HW_GPU,
             "Compute write extents: {} programs marked in full from the start ({} built in, {} "
             "caught in earlier sessions)",
             state.known.size(), BuiltIn.size(), saved);
}

/// Whether the program is marked in full from its first dispatch.
[[nodiscard]] inline bool IsKnown(u64 hash) {
    State& state = GetState();
    std::scoped_lock lock{state.mutex};
    Load(state);
    return state.known.contains(hash);
}

/// A check caught the program: later sessions mark it in full from the start.
inline void Remember(u64 hash) {
    State& state = GetState();
    std::scoped_lock lock{state.mutex};
    Load(state);
    if (!state.known.insert(hash).second) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(FilePath().parent_path(), error);
    std::ofstream file{FilePath(), std::ios::app};
    if (file) {
        file << fmt::format("{:016x}\n", hash);
    }
}

} // namespace VideoCommon::ExtentDistrust
