// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/common_types.h"

/// A game's own menu music, from the player's copy of the game: its title-screen track (the
/// largest .bwav in its RomFS named like "title", else "menu" or "opening"), decoded to stereo
/// 48 kHz 16-bit PCM. Nintendo's BWAV holds one stream per channel: Opus (the Switch's framing),
/// PCM16 or DSP-ADPCM; 5.1 is mixed down to stereo. Decoded once and kept in the cache folder
/// (menu_music/<program id>.pcm), so later picks don't read the game again.
namespace GameMusic {

struct Track {
    u64 program_id{};
    std::vector<s16> samples; ///< Interleaved stereo, 48 kHz; empty: the game has none
};

/// Slow (reads and decodes the game the first time): call it off the UI thread.
[[nodiscard]] std::shared_ptr<const Track> Load(const std::string& game_path, u64 program_id);

/// Decodes a BWAV to interleaved stereo 48 kHz (at most `max_seconds`); empty when it can't.
[[nodiscard]] std::vector<s16> DecodeBwav(const std::vector<u8>& bwav, u32 max_seconds);

} // namespace GameMusic
