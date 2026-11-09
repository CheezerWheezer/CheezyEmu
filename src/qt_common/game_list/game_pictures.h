// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Game list pictures CheezyEmu found online or the player picked, one PNG per title ID in
// <cache>/game_pictures. They win over the icon inside the game file. A title the search found
// nothing for gets a .none marker, so it isn't searched for again for a day.

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include "common/common_types.h"
#include "common/fs/path_util.h"

namespace QtCommon::GamePictures {

inline std::filesystem::path Folder() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "game_pictures";
}

inline std::filesystem::path PicturePath(u64 program_id) {
    return Folder() / fmt::format("{:016X}.png", program_id);
}

inline std::filesystem::path MissPath(u64 program_id) {
    return Folder() / fmt::format("{:016X}.none", program_id);
}

/// The saved picture for a title, or nothing.
inline std::vector<u8> Load(u64 program_id) {
    if (program_id == 0) {
        return {};
    }
    std::ifstream file(PicturePath(program_id), std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<u8>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

inline bool Save(u64 program_id, std::span<const u8> png) {
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    const auto path = PicturePath(program_id);
    const auto temp = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(png.data()),
                   static_cast<std::streamsize>(png.size()));
        if (!file) {
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    std::filesystem::remove(MissPath(program_id), ec);
    return !ec;
}

inline void Remove(u64 program_id) {
    std::error_code ec;
    std::filesystem::remove(PicturePath(program_id), ec);
    std::filesystem::remove(MissPath(program_id), ec);
}

/// Remembers that no picture was found, so the automatic search skips the title for a while.
inline void MarkMissing(u64 program_id) {
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    std::ofstream file(MissPath(program_id), std::ios::trunc);
}

/// Whether the automatic search should skip this title: found nothing within the last day.
inline bool RecentlyMissing(u64 program_id) {
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(MissPath(program_id), ec);
    if (ec) {
        return false;
    }
    return std::filesystem::file_time_type::clock::now() - written < std::chrono::hours{24};
}

} // namespace QtCommon::GamePictures
