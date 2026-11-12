// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/common_types.h"

/// A game's own menu music, from the player's copy of the game, decoded to stereo 48 kHz 16-bit
/// PCM. Nintendo's BWAV holds one stream per channel: Opus (the Switch's framing), PCM16 or
/// DSP-ADPCM; 5.1 is mixed down to stereo.
///
/// Two choices (Configure > Interface > Sounds):
/// - Title: its title-screen track (the largest .bwav named like "title", else "menu" or
///   "opening"), decoded once and kept as menu_music/<program id>.pcm in the cache folder.
/// - Random: a random song from its soundtrack (the "BGM" .bwav files that are whole songs:
///   long enough, not an intro, outro or one layer of a layered piece). The list of songs is kept
///   as menu_music/<program id>/songs.txt and the last 16 songs played as copies of their .bwav
///   files (compressed, a few MB each), so picks don't read the whole game again.
namespace GameMusic {

enum class Choice { Title, Random };

struct Track {
    u64 program_id{};
    std::string name;         ///< The song's file name
    std::vector<s16> samples; ///< Interleaved stereo, 48 kHz; empty: the game has none
    u32 plays = 0;            ///< Times through before the next song; 0: loops until another pick
};

/// Slow (reads and decodes the game the first time): call it off the UI thread, one call at a
/// time. Random never picks the song that just played, nor one of the last few.
[[nodiscard]] std::shared_ptr<const Track> Load(const std::string& game_path, u64 program_id,
                                                Choice choice);

/// Decodes a BWAV to interleaved stereo 48 kHz (at most `max_seconds`); empty when it can't.
[[nodiscard]] std::vector<s16> DecodeBwav(const std::vector<u8>& bwav, u32 max_seconds);

} // namespace GameMusic
