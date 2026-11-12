// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/util/game_music.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <unordered_map>

#include <opus.h>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/loader/loader.h"
#include "qt_common/qt_common.h"

namespace GameMusic {

namespace {

constexpr u32 RATE = 48000;
constexpr u32 MAX_SECONDS = 180;
constexpr u32 CACHE_MAGIC = 0x554D5A43; // "CZMU"
constexpr u32 CACHE_VERSION = 2; // 2: the quiet opening trimmed
constexpr u32 SONGS_KEPT = 16;   // .bwav copies kept per game (Random)
constexpr u64 SONG_MIN_BYTES = 2'500'000;
constexpr u32 SONG_MIN_SECONDS = 30;
constexpr std::string_view SONG_LIST_HEADER = "CheezyEmu songs 3"; // 2: sections left out

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

template <typename T>
bool Read(const std::vector<u8>& data, size_t offset, T& value) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return true;
}

u32 ReadBigU32(const std::vector<u8>& data, size_t offset) {
    return (u32{data[offset]} << 24) | (u32{data[offset + 1]} << 16) |
           (u32{data[offset + 2]} << 8) | u32{data[offset + 3]};
}

/// One BWAV channel (bwav channel info, 0x4C bytes).
struct Channel {
    u16 codec;
    u16 pan;
    u32 rate;
    u32 samples;
    std::array<s16, 16> coefficients;
    u32 offset;
    s16 history1;
    s16 history2;
};

/// Switch Opus ("0x80000001" header, "0x80000004" data of big-endian size/range-framed
/// packets), one channel.
std::vector<float> DecodeOpus(const std::vector<u8>& data, const Channel& channel,
                              size_t max_samples) {
    std::vector<float> out;
    u32 header_id = 0, header_size = 0, data_offset = 0, pre_skip = 0;
    if (!Read(data, channel.offset, header_id) || header_id != 0x80000001 ||
        !Read(data, channel.offset + 4, header_size) ||
        !Read(data, channel.offset + 0x10, data_offset) ||
        !Read(data, channel.offset + 0x1C, pre_skip)) {
        return out;
    }
    const size_t chunk = channel.offset + data_offset;
    u32 data_id = 0, data_size = 0;
    if (!Read(data, chunk, data_id) || data_id != 0x80000004 || !Read(data, chunk + 4, data_size)) {
        return out;
    }
    int error = 0;
    OpusDecoder* const decoder = opus_decoder_create(RATE, 1, &error);
    if (decoder == nullptr || error != OPUS_OK) {
        return out;
    }
    out.reserve(std::min<size_t>(channel.samples, max_samples));
    std::array<float, 5760> pcm{};
    size_t position = chunk + 8;
    const size_t end = std::min<size_t>(data.size(), chunk + 8 + data_size);
    size_t skip = pre_skip;
    while (position + 8 <= end && out.size() < max_samples) {
        const u32 size = ReadBigU32(data, position);
        position += 8;
        if (size == 0 || position + size > end) {
            break;
        }
        const int decoded = opus_decode_float(decoder, data.data() + position,
                                              static_cast<opus_int32>(size), pcm.data(),
                                              static_cast<int>(pcm.size()), 0);
        position += size;
        if (decoded <= 0) {
            break;
        }
        const size_t first = std::min<size_t>(skip, static_cast<size_t>(decoded));
        skip -= first;
        out.insert(out.end(), pcm.begin() + static_cast<std::ptrdiff_t>(first),
                   pcm.begin() + decoded);
    }
    opus_decoder_destroy(decoder);
    out.resize(std::min(out.size(), max_samples));
    return out;
}

/// Nintendo DSP-ADPCM: 8-byte frames of 14 four-bit samples.
std::vector<float> DecodeAdpcm(const std::vector<u8>& data, const Channel& channel,
                               size_t max_samples) {
    std::vector<float> out;
    const size_t samples = std::min<size_t>(channel.samples, max_samples);
    out.reserve(samples);
    s32 history1 = channel.history1;
    s32 history2 = channel.history2;
    size_t position = channel.offset;
    while (out.size() < samples && position + 8 <= data.size()) {
        const u8 header = data[position];
        const s32 scale = 1 << (header & 0xF);
        const size_t index = (header >> 4) & 7;
        const s32 c1 = channel.coefficients[index * 2];
        const s32 c2 = channel.coefficients[index * 2 + 1];
        for (size_t n = 0; n < 14 && out.size() < samples; ++n) {
            const u8 byte = data[position + 1 + n / 2];
            s32 nibble = (n % 2 == 0) ? (byte >> 4) : (byte & 0xF);
            if (nibble >= 8) {
                nibble -= 16;
            }
            s32 sample = ((nibble * scale) << 11) + 1024 + c1 * history1 + c2 * history2;
            sample = std::clamp(sample >> 11, -32768, 32767);
            history2 = history1;
            history1 = sample;
            out.push_back(static_cast<float>(sample) / 32768.0f);
        }
        position += 8;
    }
    return out;
}

std::vector<float> DecodePcm16(const std::vector<u8>& data, const Channel& channel,
                               size_t max_samples) {
    std::vector<float> out;
    const size_t samples = std::min<size_t>(channel.samples, max_samples);
    out.reserve(samples);
    for (size_t i = 0; i < samples; ++i) {
        s16 sample = 0;
        if (!Read(data, channel.offset + i * 2, sample)) {
            break;
        }
        out.push_back(static_cast<float>(sample) / 32768.0f);
    }
    return out;
}

std::filesystem::path CachePath(u64 program_id) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "menu_music" /
           fmt::format("{:016X}.pcm", program_id);
}

std::shared_ptr<const Track> ReadCache(u64 program_id) {
    std::ifstream file{CachePath(program_id), std::ios::binary};
    u32 magic = 0, version = 0, frames = 0;
    if (!file.read(reinterpret_cast<char*>(&magic), 4) ||
        !file.read(reinterpret_cast<char*>(&version), 4) ||
        !file.read(reinterpret_cast<char*>(&frames), 4) || magic != CACHE_MAGIC ||
        version != CACHE_VERSION || frames > RATE * MAX_SECONDS) {
        return nullptr;
    }
    auto track = std::make_shared<Track>();
    track->program_id = program_id;
    track->samples.resize(size_t{frames} * 2);
    if (frames != 0 &&
        !file.read(reinterpret_cast<char*>(track->samples.data()),
                   static_cast<std::streamsize>(track->samples.size() * sizeof(s16)))) {
        return nullptr;
    }
    return track;
}

void WriteCache(const Track& track) {
    const auto path = CachePath(track.program_id);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    const u32 header[3]{CACHE_MAGIC, CACHE_VERSION, static_cast<u32>(track.samples.size() / 2)};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(reinterpret_cast<const char*>(track.samples.data()),
               static_cast<std::streamsize>(track.samples.size() * sizeof(s16)));
}

/// The title-screen track: the largest .bwav named like "title", else "menu", else "opening".
FileSys::VirtualFile FindTrack(const FileSys::VirtualDir& root) {
    std::array<FileSys::VirtualFile, 3> best{};
    std::function<void(const FileSys::VirtualDir&)> walk = [&](const FileSys::VirtualDir& dir) {
        for (const auto& file : dir->GetFiles()) {
            const std::string name = Lower(file->GetName());
            if (!name.ends_with(".bwav")) {
                continue;
            }
            for (size_t tier = 0; tier < best.size(); ++tier) {
                static constexpr std::array<const char*, 3> words{"title", "menu", "opening"};
                if (name.find(words[tier]) != std::string::npos) {
                    if (!best[tier] || file->GetSize() > best[tier]->GetSize()) {
                        best[tier] = file;
                    }
                    break;
                }
            }
        }
        for (const auto& sub : dir->GetSubdirectories()) {
            if (Lower(sub->GetName()) != "voice") {
                walk(sub);
            }
        }
    };
    walk(root);
    for (const auto& file : best) {
        if (file) {
            return file;
        }
    }
    return nullptr;
}

/// The player's copy, opened: its loader (kept, as the files may read through it) and RomFS.
struct OpenGame {
    std::unique_ptr<Loader::AppLoader> loader;
    FileSys::VirtualDir root;
};

OpenGame Open(const std::string& game_path) {
    OpenGame game;
    game.loader = Loader::GetLoader(
        *QtCommon::system, QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read));
    FileSys::VirtualFile image;
    if (game.loader) {
        game.loader->ReadRomFS(image);
    }
    game.root = image ? FileSys::ExtractRomFS(image) : nullptr;
    return game;
}

/// Whether a file is one of a game's whole songs: a .bwav with "bgm" in its name, at least
/// 2.5 MB, and none of its name's parts ("BGM_Kokiri_Night_Intro": bgm, kokiri, night, intro)
/// mark an intro, outro, cutscene or one layer of a layered piece.
bool IsSong(const std::string& file_name, u64 size) {
    const std::string name = Lower(file_name);
    if (size < SONG_MIN_BYTES || !name.ends_with(".bwav") || name.find("bgm") == std::string::npos) {
        return false;
    }
    static const std::set<std::string, std::less<>> not_songs{
        "dm",    "demo", "intro",      "outro", "noise",   "pad",   "base",  "close",
        "far",   "jingle", "oneshot", "transition", "gong", "roar", "voice", "stinger",
        "other", "fanfare", "se",     "sfx",   "ambient", "amb",   "env",   "nointro",
    };
    const std::string_view stem = std::string_view{name}.substr(0, name.size() - 5);
    size_t start = 0;
    while (start <= stem.size()) {
        size_t end = stem.find_first_of("_-. ", start);
        if (end == std::string_view::npos) {
            end = stem.size();
        }
        std::string_view part = stem.substr(start, end - start);
        while (!part.empty() && std::isdigit(static_cast<unsigned char>(part.back()))) {
            part.remove_suffix(1);
        }
        if (not_songs.contains(part)) {
            return false;
        }
        start = end + 1;
    }
    return true;
}

/// "bgm_market_daytime_02.bwav" -> "bgm_market_daytime": the name of a numbered section of a
/// piece (empty when the name doesn't end in "_<number>").
std::string SectionOf(const std::string& file_name) {
    const std::string name = Lower(file_name);
    const size_t dot = name.rfind('.');
    const size_t bar = name.rfind('_', dot);
    if (dot == std::string::npos || bar == std::string::npos || bar + 1 == dot ||
        !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(bar + 1),
                     name.begin() + static_cast<std::ptrdiff_t>(dot),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return {};
    }
    return name.substr(0, bar);
}

struct Song {
    std::string path; ///< In the RomFS, '/' between folders
    u64 size{};
    [[nodiscard]] std::string FileName() const {
        return path.substr(path.find_last_of('/') + 1);
    }
};

/// The game's songs: every IsSong file that isn't one numbered section of a piece (Morning_01,
/// Morning_02 ...), one per size (the layers of a layered piece, and the same song kept under
/// several names, are the same size), in name order.
std::vector<Song> FindSongs(const FileSys::VirtualDir& root,
                            std::unordered_map<std::string, FileSys::VirtualFile>& files) {
    std::vector<Song> found;
    std::function<void(const FileSys::VirtualDir&, const std::string&)> walk =
        [&](const FileSys::VirtualDir& dir, const std::string& prefix) {
            const auto dir_files = dir->GetFiles();
            std::unordered_map<std::string, int> sections;
            for (const auto& file : dir_files) {
                if (std::string section = SectionOf(file->GetName()); !section.empty()) {
                    ++sections[section];
                }
            }
            for (const auto& file : dir_files) {
                const auto section = sections.find(SectionOf(file->GetName()));
                if (section != sections.end() && section->second > 1) {
                    continue;
                }
                if (IsSong(file->GetName(), file->GetSize())) {
                    Song song{prefix + file->GetName(), file->GetSize()};
                    files[song.path] = file;
                    found.push_back(std::move(song));
                }
            }
            for (const auto& sub : dir->GetSubdirectories()) {
                if (Lower(sub->GetName()) != "voice") {
                    walk(sub, prefix + sub->GetName() + "/");
                }
            }
        };
    walk(root, "");
    std::sort(found.begin(), found.end(),
              [](const Song& a, const Song& b) { return a.path < b.path; });
    std::set<u64> sizes;
    std::erase_if(found, [&sizes](const Song& song) { return !sizes.insert(song.size).second; });
    return found;
}

std::filesystem::path SongFolder(u64 program_id) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "menu_music" /
           fmt::format("{:016X}", program_id);
}

/// songs.txt: the header, then "<size> <path>" per song. Nothing when it isn't there yet.
std::optional<std::vector<Song>> ReadSongList(u64 program_id) {
    std::ifstream file{SongFolder(program_id) / "songs.txt"};
    std::string line;
    if (!std::getline(file, line) || line != SONG_LIST_HEADER) {
        return std::nullopt;
    }
    std::vector<Song> listed;
    while (std::getline(file, line)) {
        const size_t space = line.find(' ');
        if (space == std::string::npos || space + 1 >= line.size()) {
            continue;
        }
        listed.push_back({line.substr(space + 1), std::strtoull(line.c_str(), nullptr, 10)});
    }
    return listed;
}

void WriteSongList(u64 program_id, const std::vector<Song>& listed) {
    std::error_code ec;
    std::filesystem::create_directories(SongFolder(program_id), ec);
    std::ofstream file{SongFolder(program_id) / "songs.txt", std::ios::trunc};
    file << SONG_LIST_HEADER << '\n';
    for (const Song& song : listed) {
        file << song.size << ' ' << song.path << '\n';
    }
}

std::filesystem::path KeptSong(u64 program_id, const Song& song) {
    return SongFolder(program_id) / song.FileName();
}

/// Keeps a song's .bwav, and drops the least recently played beyond SONGS_KEPT.
void KeepSong(u64 program_id, const Song& song, const std::vector<u8>& bytes) {
    {
        std::ofstream file{KeptSong(program_id, song), std::ios::binary | std::ios::trunc};
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> kept;
    for (const auto& entry : std::filesystem::directory_iterator(SongFolder(program_id), ec)) {
        if (entry.path().extension() == ".bwav") {
            kept.emplace_back(entry.last_write_time(ec), entry.path());
        }
    }
    if (kept.size() <= SONGS_KEPT) {
        return;
    }
    std::sort(kept.begin(), kept.end());
    for (size_t i = 0; i + SONGS_KEPT < kept.size(); ++i) {
        std::filesystem::remove(kept[i].second, ec);
    }
}

std::optional<std::vector<u8>> ReadKeptSong(u64 program_id, const Song& song) {
    const auto path = KeptSong(program_id, song);
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<u8> bytes(song.size);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
        file.peek() != std::ifstream::traits_type::eof()) {
        return std::nullopt; // not the same file (an update changed it, or cut short)
    }
    std::error_code ec;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
    return bytes;
}

/// The picked game's songs, kept between picks while it stays picked.
struct SongPool {
    u64 program_id{};
    std::vector<Song> songs;
    bool listed = false;
    OpenGame game; ///< Opened this session (else picks come from the kept songs first)
    std::unordered_map<std::string, FileSys::VirtualFile> files;
    std::deque<std::string> recent; ///< The last songs played, newest last
    std::mt19937 rng{std::random_device{}()};
};

std::mutex song_pool_mutex;
SongPool song_pool;

std::shared_ptr<const Track> LoadTitle(const std::string& game_path, u64 program_id) {
    if (auto cached = ReadCache(program_id)) {
        return cached;
    }
    auto track = std::make_shared<Track>();
    track->program_id = program_id;
    try {
        const OpenGame game = Open(game_path);
        if (const auto file = game.root ? FindTrack(game.root) : nullptr) {
            track->name = file->GetName();
            track->samples = DecodeBwav(file->ReadAllBytes(), MAX_SECONDS);
            LOG_INFO(Frontend, "Menu music for {:016X}: {} ({:.0f} s)", program_id,
                     file->GetName(), track->samples.size() / 2.0 / RATE);
        } else {
            LOG_INFO(Frontend, "Menu music for {:016X}: no title track found", program_id);
        }
    } catch (const std::exception& error) {
        LOG_WARNING(Frontend, "Menu music for {:016X}: {}", program_id, error.what());
    }
    WriteCache(*track); // also "none", so the game isn't read again
    return track;
}

std::shared_ptr<const Track> LoadRandom(const std::string& game_path, u64 program_id) {
    std::scoped_lock lock{song_pool_mutex};
    SongPool& pool_now = song_pool;
    if (pool_now.program_id != program_id) {
        pool_now = SongPool{};
        pool_now.program_id = program_id;
        if (auto listed = ReadSongList(program_id)) {
            pool_now.songs = std::move(*listed);
            pool_now.listed = true;
        }
    }
    const auto open_game = [&] {
        if (!pool_now.game.root) {
            pool_now.game = Open(game_path);
            pool_now.files.clear();
            if (pool_now.game.root) {
                std::vector<Song> found = FindSongs(pool_now.game.root, pool_now.files);
                if (!pool_now.listed || found.size() != pool_now.songs.size()) {
                    pool_now.songs = std::move(found);
                    WriteSongList(program_id, pool_now.songs);
                    std::string names;
                    for (const Song& song : pool_now.songs) {
                        names += (names.empty() ? "" : ", ") + song.FileName();
                    }
                    LOG_INFO(Frontend, "Menu music for {:016X}: {} songs: {}", program_id,
                             pool_now.songs.size(), names);
                }
                pool_now.listed = true;
            }
        }
        return pool_now.game.root != nullptr;
    };
    try {
        if (!pool_now.listed && !open_game()) {
            return LoadTitle(game_path, program_id);
        }
        for (int attempt = 0; attempt < 6 && !pool_now.songs.empty(); ++attempt) {
            // Not one of the last few (a third of the songs, at most 8).
            const size_t avoid = std::min<size_t>(8, pool_now.songs.size() / 3);
            while (pool_now.recent.size() > avoid) {
                pool_now.recent.pop_front();
            }
            std::vector<const Song*> fresh;
            std::vector<const Song*> fresh_kept;
            for (const Song& song : pool_now.songs) {
                if (std::find(pool_now.recent.begin(), pool_now.recent.end(), song.path) !=
                    pool_now.recent.end()) {
                    continue;
                }
                fresh.push_back(&song);
                std::error_code ec;
                if (std::filesystem::exists(KeptSong(program_id, song), ec)) {
                    fresh_kept.push_back(&song);
                }
            }
            if (fresh.empty()) {
                pool_now.recent.clear();
                continue;
            }
            // Until the game is opened this session, a kept song starts right away.
            const auto& from = (!pool_now.game.root && !fresh_kept.empty()) ? fresh_kept : fresh;
            const Song song = *from[std::uniform_int_distribution<size_t>(0, from.size() - 1)(
                pool_now.rng)];
            std::optional<std::vector<u8>> bytes = ReadKeptSong(program_id, song);
            if (!bytes) {
                if (!open_game()) {
                    break;
                }
                const auto file = pool_now.files.find(song.path);
                if (file == pool_now.files.end()) {
                    std::erase_if(pool_now.songs,
                                  [&song](const Song& s) { return s.path == song.path; });
                    continue;
                }
                bytes = file->second->ReadAllBytes();
                KeepSong(program_id, song, *bytes);
            }
            auto track = std::make_shared<Track>();
            track->program_id = program_id;
            track->name = song.FileName();
            track->samples = DecodeBwav(*bytes, MAX_SECONDS);
            const double seconds = track->samples.size() / 2.0 / RATE;
            if (seconds < SONG_MIN_SECONDS) {
                // Too short to be a song (or it didn't decode): not picked again.
                std::erase_if(pool_now.songs,
                              [&song](const Song& s) { return s.path == song.path; });
                continue;
            }
            // A short loop plays twice before the next song.
            track->plays = seconds < 75.0 ? 2 : 1;
            pool_now.recent.push_back(song.path);
            LOG_INFO(Frontend, "Menu music for {:016X}: {} ({:.0f} s, of {} songs)", program_id,
                     track->name, seconds, pool_now.songs.size());
            return track;
        }
    } catch (const std::exception& error) {
        LOG_WARNING(Frontend, "Menu music for {:016X}: {}", program_id, error.what());
    }
    return LoadTitle(game_path, program_id); // no songs found: the title theme
}

} // Anonymous namespace

std::vector<s16> DecodeBwav(const std::vector<u8>& bwav, u32 max_seconds) {
    std::vector<s16> result;
    u16 bom = 0, channel_count = 0;
    if (bwav.size() < 16 || std::memcmp(bwav.data(), "BWAV", 4) != 0 || !Read(bwav, 4, bom) ||
        bom != 0xFEFF || !Read(bwav, 14, channel_count) || channel_count == 0 ||
        channel_count > 16) {
        return result;
    }
    const size_t max_samples = size_t{RATE} * max_seconds;
    std::vector<std::vector<float>> decoded;
    std::vector<u16> pans;
    for (size_t i = 0; i < channel_count; ++i) {
        const size_t info = 16 + i * 0x4C;
        Channel channel{};
        if (!Read(bwav, info, channel.codec) || !Read(bwav, info + 2, channel.pan) ||
            !Read(bwav, info + 4, channel.rate) || !Read(bwav, info + 12, channel.samples) ||
            !Read(bwav, info + 16, channel.coefficients) || !Read(bwav, info + 52, channel.offset) ||
            !Read(bwav, info + 0x46, channel.history1) ||
            !Read(bwav, info + 0x48, channel.history2)) {
            return result;
        }
        if (channel.rate != RATE) {
            return result; // other rates would need resampling: no music rather than wrong pitch
        }
        switch (channel.codec) {
        case 0:
            decoded.push_back(DecodePcm16(bwav, channel, max_samples));
            break;
        case 1:
            decoded.push_back(DecodeAdpcm(bwav, channel, max_samples));
            break;
        case 2:
            decoded.push_back(DecodeOpus(bwav, channel, max_samples));
            break;
        default:
            return result;
        }
        pans.push_back(channel.pan);
    }
    size_t length = SIZE_MAX;
    for (const auto& samples : decoded) {
        length = std::min(length, samples.size());
    }
    if (length == 0 || length == SIZE_MAX) {
        return result;
    }
    // Down to stereo: front left/right as they are, centre and the surrounds at -3 dB, LFE at
    // a third (pans 0 FL, 1 FR, 2 C, 3 LFE, 4 SL, 5 SR; a pair is left and right).
    std::vector<float> mix(length * 2, 0.0f);
    for (size_t c = 0; c < decoded.size(); ++c) {
        float left = 0.5f, right = 0.5f;
        if (decoded.size() == 1) {
            left = right = 1.0f;
        } else if (decoded.size() == 2) {
            left = c == 0 ? 1.0f : 0.0f;
            right = 1.0f - left;
        } else {
            static constexpr std::array<std::array<float, 2>, 6> weights{{
                {1.0f, 0.0f},     // front left
                {0.0f, 1.0f},     // front right
                {0.707f, 0.707f}, // centre
                {0.33f, 0.33f},   // LFE
                {0.707f, 0.0f},   // surround left
                {0.0f, 0.707f},   // surround right
            }};
            if (pans[c] < weights.size()) {
                left = weights[pans[c]][0];
                right = weights[pans[c]][1];
            }
        }
        for (size_t i = 0; i < length; ++i) {
            mix[i * 2] += decoded[c][i] * left;
            mix[i * 2 + 1] += decoded[c][i] * right;
        }
    }
    float peak = 0.0f;
    for (const float sample : mix) {
        peak = std::max(peak, std::abs(sample));
    }
    const float gain = peak > 0.95f ? 0.95f / peak : 1.0f;
    // Title tracks often open with a long quiet build-up: start (and loop back to) where the
    // music is audible, a moment before the first tenth of a second above -40 dBFS.
    size_t start = 0;
    constexpr size_t WINDOW = RATE / 10;
    for (size_t frame = 0; frame + WINDOW < length; frame += WINDOW) {
        double energy = 0.0;
        for (size_t i = frame; i < frame + WINDOW; ++i) {
            energy += double(mix[i * 2]) * mix[i * 2] + double(mix[i * 2 + 1]) * mix[i * 2 + 1];
        }
        if (std::sqrt(energy / (WINDOW * 2)) * gain > 0.01) {
            start = frame > RATE * 3 / 10 ? frame - RATE * 3 / 10 : 0;
            break;
        }
    }
    mix.erase(mix.begin(), mix.begin() + static_cast<std::ptrdiff_t>(start * 2));
    result.resize(mix.size());
    for (size_t i = 0; i < mix.size(); ++i) {
        result[i] = static_cast<s16>(std::lround(std::clamp(mix[i] * gain, -1.0f, 1.0f) * 32767.0f));
    }
    return result;
}

std::shared_ptr<const Track> Load(const std::string& game_path, u64 program_id, Choice choice) {
    return choice == Choice::Random ? LoadRandom(game_path, program_id)
                                    : LoadTitle(game_path, program_id);
}

} // namespace GameMusic
