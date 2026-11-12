// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/util/game_music.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>

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
            std::string name = file->GetName();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
            std::string name = sub->GetName();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name != "voice") {
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

std::shared_ptr<const Track> Load(const std::string& game_path, u64 program_id) {
    if (auto cached = ReadCache(program_id)) {
        return cached;
    }
    auto track = std::make_shared<Track>();
    track->program_id = program_id;
    try {
        const auto loader = Loader::GetLoader(
            *QtCommon::system, QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read));
        FileSys::VirtualFile image;
        if (loader) {
            loader->ReadRomFS(image);
        }
        const auto root = image ? FileSys::ExtractRomFS(image) : nullptr;
        if (const auto file = root ? FindTrack(root) : nullptr) {
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

} // namespace GameMusic
