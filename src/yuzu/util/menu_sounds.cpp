// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/util/menu_sounds.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <vector>

#include <QDir>
#include <QFile>
#include <QGuiApplication>

#include <SDL3/SDL.h>

#include "common/logging.h"
#include "qt_common/config/uisettings.h"
#include "yuzu/util/game_music.h"

#include <mutex>

namespace {

constexpr int Rate = 48000;
constexpr double TwoPi = 2.0 * std::numbers::pi;

/// The volume setting (0 to 100) as a gain, on a curve that sounds even across the slider.
float Gain(int percent) {
    return static_cast<float>(std::pow(std::clamp(percent, 0, 100) / 100.0, 1.7));
}

/// A tiny white-noise source.
struct Noise {
    uint32_t state = 0x9E3779B9u;
    float Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) / 2147483648.0f - 1.0f;
    }
};

/// The click (mono): a soft wooden tick. A short tone that drops from 2.1 to 1.5 kHz, a little
/// low body under it and a breath of noise at the very start.
const std::vector<float>& ClickWave() {
    static const std::vector<float> wave = [] {
        const int n = Rate * 75 / 1000;
        std::vector<float> w(n);
        Noise noise;
        double phase = 0.0;
        float lowpass = 0.0f;
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / Rate;
            phase += TwoPi * (1500.0 + 600.0 * std::exp(-t / 0.0035)) / Rate;
            const double tone = std::sin(phase) * std::exp(-t / 0.011);
            const double body = std::sin(TwoPi * 430.0 * t) * std::exp(-t / 0.020) * 0.4;
            lowpass += 0.35f * (noise.Next() - lowpass);
            const double breath = lowpass * std::exp(-t / 0.0012) * 0.5;
            const double attack = std::min(1.0, t / 0.0005);
            const double tail = std::min(1.0, double(n - i) / (Rate * 0.005));
            w[i] = static_cast<float>((tone + body + breath) * attack * tail * 0.32);
        }
        return w;
    }();
    return wave;
}

/// The chime (stereo, about 1.2 s): a deep, warm rising chord. Four notes an octave lower than a
/// bright bell (D, A, D, F#), struck quickly one after another and spreading left to right,
/// each a warm mallet tone with a slightly detuned twin for body; a soft sub-bass D under them
/// gives it depth; all in a small soft room.
const std::vector<float>& ChimeWave() {
    static const std::vector<float> wave = [] {
        const int n = Rate * 12 / 10;
        std::vector<float> left(n), right(n);
        struct Note {
            double freq, start, pan, gain;
        };
        // A quick D major strum, low to high.
        constexpr std::array<Note, 4> notes{{
            {293.66, 0.000, -0.30, 1.00},
            {369.99, 0.035, -0.10, 0.80},
            {440.00, 0.070, 0.10, 0.70},
            {587.33, 0.105, 0.30, 0.62},
        }};
        // A warm mallet: the note, a detuned twin (slow beating = body), its octave and a
        // little of the brighter partials, which fade first.
        constexpr std::array<double, 5> ratio{1.0, 1.004, 2.0, 3.0, 4.2};
        constexpr std::array<double, 5> level{1.0, 0.38, 0.45, 0.14, 0.06};
        constexpr std::array<double, 5> decay{0.42, 0.42, 0.26, 0.14, 0.07};
        for (const Note& note : notes) {
            const double angle = (note.pan + 1.0) * std::numbers::pi / 4.0;
            const double gl = std::cos(angle) * note.gain;
            const double gr = std::sin(angle) * note.gain;
            for (int i = int(note.start * Rate); i < n; ++i) {
                const double t = double(i) / Rate - note.start;
                double s = 0.0;
                for (size_t k = 0; k < ratio.size(); ++k) {
                    s += std::sin(TwoPi * note.freq * ratio[k] * t) * level[k] *
                         std::exp(-t / decay[k]);
                }
                s *= std::min(1.0, t / 0.003);
                left[i] += static_cast<float>(s * gl);
                right[i] += static_cast<float>(s * gr);
            }
        }
        // The sparkle: three little bells running up over the strum (A, D, F#, two octaves
        // up), panned across, with the inharmonic partials that make a bell ring.
        constexpr std::array<Note, 3> bells{{
            {880.00, 0.150, -0.45, 0.30},
            {1174.66, 0.185, 0.05, 0.26},
            {1479.98, 0.220, 0.45, 0.22},
        }};
        constexpr std::array<double, 4> bell_ratio{1.0, 2.76, 5.40, 8.93};
        constexpr std::array<double, 4> bell_level{1.0, 0.42, 0.18, 0.07};
        constexpr std::array<double, 4> bell_decay{0.34, 0.16, 0.08, 0.04};
        for (const Note& bell : bells) {
            const double angle = (bell.pan + 1.0) * std::numbers::pi / 4.0;
            const double gl = std::cos(angle) * bell.gain;
            const double gr = std::sin(angle) * bell.gain;
            for (int i = int(bell.start * Rate); i < n; ++i) {
                const double t = double(i) / Rate - bell.start;
                double s = 0.0;
                for (size_t k = 0; k < bell_ratio.size(); ++k) {
                    s += std::sin(TwoPi * bell.freq * bell_ratio[k] * t) * bell_level[k] *
                         std::exp(-t / bell_decay[k]);
                }
                s *= std::min(1.0, t / 0.002);
                left[i] += static_cast<float>(s * gl);
                right[i] += static_cast<float>(s * gr);
            }
        }
        // The sub-bass: a low D that swells in over 12 ms and fades with the chord.
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / Rate;
            const double sub = std::sin(TwoPi * 146.83 * t) * 0.55 * std::min(1.0, t / 0.012) *
                               std::exp(-t / 0.38);
            left[i] += static_cast<float>(sub);
            right[i] += static_cast<float>(sub);
        }
        // The room: damped feedback delays and two all-passes per side (a small Schroeder
        // reverb), slightly different left and right so it feels wide.
        const auto room = [&](const std::vector<float>& in, double offset_ms) {
            std::vector<float> out(n, 0.0f);
            for (const double ms : {29.7, 37.1, 41.1, 43.7}) {
                const int d = int((ms + offset_ms) * Rate / 1000.0);
                std::vector<float> line(d, 0.0f);
                float damp = 0.0f;
                for (int i = 0; i < n; ++i) {
                    const float y = line[i % d];
                    damp += 0.25f * (y - damp);
                    line[i % d] = in[i] + damp * 0.72f;
                    out[i] += y * 0.25f;
                }
            }
            for (const double ms : {5.0, 1.7}) {
                const int d = int((ms + offset_ms * 0.1) * Rate / 1000.0);
                std::vector<float> line(d, 0.0f);
                for (int i = 0; i < n; ++i) {
                    const float delayed = line[i % d];
                    const float x = out[i];
                    const float y = delayed - 0.5f * x;
                    line[i % d] = x + 0.5f * y;
                    out[i] = y;
                }
            }
            return out;
        };
        const std::vector<float> wet_l = room(left, 0.0);
        const std::vector<float> wet_r = room(right, 0.6);
        std::vector<float> w(size_t(n) * 2);
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float tail = std::min(1.0f, float(n - i) / (Rate * 0.05f));
            w[2 * i] = (left[i] + wet_l[i] * 0.24f) * tail;
            w[2 * i + 1] = (right[i] + wet_r[i] * 0.24f) * tail;
            peak = std::max({peak, std::abs(w[2 * i]), std::abs(w[2 * i + 1])});
        }
        for (float& s : w) {
            s *= 0.45f / peak;
        }
        return w;
    }();
    return wave;
}

/// Set on the audio thread when the playing song is near its end: the next one is wanted.
std::atomic<bool> want_next_music{false};

/// The mixer: clicks, the chime and the hum, made a block at a time on SDL's audio thread.
struct Synth {
    // Set before each block.
    float volume = 0.0f;     ///< The volume setting as a gain
    float hum_target = 0.0f; ///< 1 to fade the hum in, 0 to fade it out

    /// The picked game's music: the current track and the one fading out.
    struct MusicSlot {
        std::shared_ptr<const GameMusic::Track> track;
        size_t position = 0;
        float gain = 0.0f;
        float target = 0.0f;
        u32 played = 0;          ///< Times through so far
        bool asked_next = false; ///< want_next_music was set for it
    };
    std::array<MusicSlot, 2> music;
    float music_on = 0.0f; ///< 1 while music may play (fades out at 0)
    /// The song after the playing one: crossfades in over its last second.
    std::shared_ptr<const GameMusic::Track> queued;

    void StartMusic(std::shared_ptr<const GameMusic::Track> track) {
        music[1] = std::move(music[0]);
        music[1].target = 0.0f;
        music[0] = MusicSlot{std::move(track), 0, 0.0f, 1.0f, 0, false};
    }

    std::array<int, 3> click_pos{-1, -1, -1}; ///< Three clicks can ring at once
    std::array<float, 3> click_gain{};
    size_t next_click = 0;
    int chime_pos = -1;
    float chime_gain = 0.0f;

    // The hum: a warm A-major pad. Each note is a pair of slightly detuned tones, one per side,
    // that slowly beat against each other; each note swells on its own slow cycle, with soft
    // air under it.
    struct Voice {
        double freq;
        float amp;
        double cycle; ///< Seconds per swell
        double offset;
    };
    static constexpr std::array<Voice, 6> pad{{
        {110.00, 0.55f, 13.0, 0.0},
        {164.81, 0.42f, 17.0, 1.3},
        {220.00, 0.30f, 19.0, 2.1},
        {277.18, 0.17f, 23.0, 0.7},
        {329.63, 0.12f, 29.0, 2.9},
        {493.88, 0.05f, 31.0, 4.2},
    }};
    std::array<double, pad.size() * 2> phase{};
    std::array<float, pad.size()> swell{};
    double hum_time = 0.0;
    int64_t hum_samples = 0;
    float hum_level = 0.0f;
    float air_l = 0.0f;
    float air_r = 0.0f;
    float air_gain = 0.0f;
    Noise noise;

    void StartClick(float gain) {
        click_pos[next_click] = 0;
        click_gain[next_click] = gain;
        next_click = (next_click + 1) % click_pos.size();
    }

    void StartChime(float gain) {
        chime_pos = 0;
        chime_gain = gain;
    }

    [[nodiscard]] bool Silent() const {
        const bool music_quiet = std::all_of(music.begin(), music.end(), [](const MusicSlot& m) {
            return !m.track || m.gain <= 0.0f;
        });
        return music_quiet && chime_pos < 0 && hum_level <= 0.0f && hum_target <= 0.0f &&
               std::all_of(click_pos.begin(), click_pos.end(), [](int p) { return p < 0; });
    }

    /// `frames` stereo frames into `out`.
    void Mix(float* out, int frames) {
        const std::vector<float>& click = ClickWave();
        const std::vector<float>& chime = ChimeWave();
        for (int f = 0; f < frames; ++f) {
            float l = 0.0f;
            float r = 0.0f;
            for (size_t k = 0; k < click_pos.size(); ++k) {
                if (click_pos[k] >= 0) {
                    const float s = click[click_pos[k]] * click_gain[k];
                    l += s;
                    r += s;
                    if (++click_pos[k] >= int(click.size())) {
                        click_pos[k] = -1;
                    }
                }
            }
            if (chime_pos >= 0) {
                l += chime[2 * chime_pos] * chime_gain;
                r += chime[2 * chime_pos + 1] * chime_gain;
                if (++chime_pos >= int(chime.size() / 2)) {
                    chime_pos = -1;
                }
            }
            bool next_song = false;
            for (size_t m = 0; m < music.size(); ++m) {
                MusicSlot& slot = music[m];
                if (!slot.track || slot.track->samples.empty()) {
                    continue;
                }
                // Fades in over 1.5 s and out over 1 s.
                const float want = slot.target * music_on;
                if (slot.gain < want) {
                    slot.gain = std::min(want, slot.gain + 1.0f / (Rate * 1.5f));
                } else if (slot.gain > want) {
                    slot.gain = std::max(want, slot.gain - 1.0f / (Rate * 1.0f));
                }
                if (slot.gain <= 0.0f) {
                    continue;
                }
                const auto& samples = slot.track->samples;
                const float eased = slot.gain * slot.gain * (3.0f - 2.0f * slot.gain);
                const float gain = eased * std::min(1.0f, volume * 1.6f) / 32768.0f;
                l += samples[slot.position] * gain;
                r += samples[slot.position + 1] * gain;
                slot.position += 2;
                // A song with a set number of plays: 25 s before its last play ends the next one
                // is asked for, and it crossfades in over the last second. Until it's here the
                // song plays on (loops); the title theme loops until another pick.
                const u32 plays = slot.track->plays;
                if (m == 0 && plays != 0 && slot.played + 1 >= plays) {
                    const size_t left = (samples.size() - slot.position) / 2;
                    if (!slot.asked_next && left < size_t{Rate} * 25) {
                        slot.asked_next = true;
                        want_next_music.store(true, std::memory_order_relaxed);
                    }
                    next_song = queued != nullptr && left <= size_t{Rate};
                }
                if (slot.position + 1 >= samples.size()) {
                    slot.position = 0;
                    ++slot.played;
                }
            }
            if (next_song) {
                StartMusic(std::move(queued));
                queued = nullptr;
            }
            if (hum_level > 0.0f || hum_target > 0.0f) {
                const auto [hl, hr] = Hum();
                l += hl;
                r += hr;
            }
            out[2 * f] = l;
            out[2 * f + 1] = r;
        }
    }

    std::pair<float, float> Hum() {
        // Fades in over 2.5 s and out over 0.9 s, eased.
        if (hum_level < hum_target) {
            hum_level = std::min(hum_target, hum_level + 1.0f / (Rate * 2.5f));
        } else if (hum_level > hum_target) {
            hum_level = std::max(hum_target, hum_level - 1.0f / (Rate * 0.9f));
        }
        // The slow swells, every 64 samples.
        if ((hum_samples++ & 63) == 0) {
            for (size_t v = 0; v < pad.size(); ++v) {
                swell[v] = pad[v].amp * static_cast<float>(
                    0.65 + 0.35 * std::sin(TwoPi * hum_time / pad[v].cycle + pad[v].offset));
            }
            air_gain = static_cast<float>(0.10 * (0.6 + 0.4 * std::sin(TwoPi * hum_time / 37.0)));
        }
        hum_time += 1.0 / Rate;
        float l = 0.0f;
        float r = 0.0f;
        for (size_t v = 0; v < pad.size(); ++v) {
            for (int side = 0; side < 2; ++side) {
                double& ph = phase[v * 2 + side];
                ph += TwoPi * pad[v].freq * (side == 0 ? 0.9991 : 1.0009) / Rate;
                if (ph >= TwoPi) {
                    ph -= TwoPi;
                }
                const float s =
                    swell[v] * static_cast<float>(std::sin(ph) + 0.12 * std::sin(2.0 * ph));
                (side == 0 ? l : r) += s;
            }
        }
        air_l += 0.06f * (noise.Next() - air_l);
        air_r += 0.06f * (noise.Next() - air_r);
        l += air_l * air_gain;
        r += air_r * air_gain;
        const float overall = static_cast<float>(0.85 + 0.15 * std::sin(TwoPi * hum_time / 41.0));
        const float eased = hum_level * hum_level * (3.0f - 2.0f * hum_level);
        const float gain = 0.09f * overall * eased * volume;
        return {l * gain, r * gain};
    }
};

// Shared between the UI thread (which sets them) and SDL's audio thread (which reads them).
std::atomic<uint32_t> click_requests{0};
std::atomic<float> click_request_gain{0.0f};
std::atomic<uint32_t> chime_requests{0};
std::atomic<float> volume_now{0.0f};
std::atomic<float> hum_wanted{0.0f};
std::atomic<bool> silent_now{true};
std::atomic<float> music_wanted_now{0.0f};
std::mutex next_music_mutex;
std::shared_ptr<const GameMusic::Track> next_music;
bool next_music_ready = false;
bool next_music_queued = false; ///< next_music follows the playing song (else it starts now)

MenuSounds* instance = nullptr;

struct Player {
    Synth synth;
    uint32_t clicks_seen = 0;
    uint32_t chimes_seen = 0;
};
Player player;

void SDLCALL Feed(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* const p = static_cast<Player*>(user);
    Synth& synth = p->synth;
    synth.volume = volume_now.load(std::memory_order_relaxed);
    synth.hum_target = hum_wanted.load(std::memory_order_relaxed);
    synth.music_on = music_wanted_now.load(std::memory_order_relaxed);
    if (std::unique_lock lock{next_music_mutex, std::try_to_lock};
        lock.owns_lock() && next_music_ready) {
        next_music_ready = false;
        if (next_music_queued) {
            synth.queued = std::move(next_music);
        } else {
            synth.queued = nullptr;
            synth.StartMusic(std::move(next_music));
        }
        next_music = nullptr;
    }
    if (const uint32_t c = click_requests.load(); c != p->clicks_seen) {
        p->clicks_seen = c;
        synth.StartClick(click_request_gain.load());
    }
    if (const uint32_t c = chime_requests.load(); c != p->chimes_seen) {
        p->chimes_seen = c;
        synth.StartChime(synth.volume);
    }
    std::array<float, 2 * 512> block;
    int frames = additional / int(2 * sizeof(float));
    while (frames > 0) {
        const int count = std::min(frames, 512);
        synth.Mix(block.data(), count);
        SDL_PutAudioStreamData(stream, block.data(), count * int(2 * sizeof(float)));
        frames -= count;
    }
    silent_now.store(synth.Silent(), std::memory_order_relaxed);
}

/// Writes 16-bit stereo 48 kHz samples as a WAV file.
void WriteWav(const QString& path, const std::vector<float>& stereo) {
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    const auto put32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(stereo.size() * 2);
    file.write("RIFF", 4);
    put32(36 + bytes);
    file.write("WAVEfmt ", 8);
    put32(16);
    put16(1);
    put16(2);
    put32(Rate);
    put32(Rate * 4);
    put16(4);
    put16(16);
    file.write("data", 4);
    put32(bytes);
    for (const float s : stereo) {
        put16(static_cast<uint16_t>(
            static_cast<int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f))));
    }
}

/// CHEEZYEMU_SOUND_DUMP: the three sounds as WAV files, at the default volume.
void Dump(const QString& folder) {
    QDir{}.mkpath(folder);
    const float gain = Gain(static_cast<int>(UISettings::values.menu_sound_volume.GetDefault()));
    std::vector<float> click;
    for (const float s : ClickWave()) {
        click.push_back(s * gain);
        click.push_back(s * gain);
    }
    WriteWav(folder + QStringLiteral("/click.wav"), click);
    std::vector<float> chime = ChimeWave();
    for (float& s : chime) {
        s *= gain;
    }
    WriteWav(folder + QStringLiteral("/chime.wav"), chime);
    // 24 seconds of hum: it fades in, plays, and fades out over the last second.
    Synth synth;
    synth.volume = gain;
    synth.hum_target = 1.0f;
    std::vector<float> hum(size_t(Rate) * 24 * 2);
    for (size_t at = 0; at < hum.size() / 2; at += 512) {
        if (at >= size_t(Rate) * 23) {
            synth.hum_target = 0.0f;
        }
        const int count = int(std::min<size_t>(512, hum.size() / 2 - at));
        synth.Mix(hum.data() + at * 2, count);
    }
    WriteWav(folder + QStringLiteral("/hum.wav"), hum);
    // CHEEZYEMU_MUSIC_TEST=<game file>|<program id, hex>: that game's menu music as music.wav
    // (the whole path: found in its files, decoded, cached).
    if (const char* const test = std::getenv("CHEEZYEMU_MUSIC_TEST"); test != nullptr) {
        const QString spec = QString::fromLocal8Bit(test);
        const qsizetype bar = spec.lastIndexOf(QLatin1Char('|'));
        // CHEEZYEMU_MUSIC_TEST_PICKS=<n>: n random songs instead, as music_1.wav and on.
        const char* const picks_text = std::getenv("CHEEZYEMU_MUSIC_TEST_PICKS");
        const int picks = picks_text != nullptr ? std::atoi(picks_text) : 0;
        if (bar > 0) {
            for (int pick = 0; pick < std::max(picks, 1); ++pick) {
                const auto started = std::chrono::steady_clock::now();
                const auto track = GameMusic::Load(
                    spec.left(bar).toStdString(), spec.mid(bar + 1).toULongLong(nullptr, 16),
                    picks > 0 ? GameMusic::Choice::Random : GameMusic::Choice::Title);
                const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started);
                if (!track) {
                    continue;
                }
                LOG_INFO(Frontend, "Music test: {} in {} ms, {} plays", track->name, took.count(),
                         track->plays);
                std::vector<float> stereo(track->samples.size());
                for (size_t i = 0; i < stereo.size(); ++i) {
                    stereo[i] = track->samples[i] / 32768.0f;
                }
                WriteWav(folder + (picks > 0 ? QStringLiteral("/music_%1.wav").arg(pick + 1)
                                             : QStringLiteral("/music.wav")),
                         stereo);
            }
        }
    }
    LOG_INFO(Frontend, "Menu sounds written to {}", folder.toStdString());
}

} // Anonymous namespace

MenuSounds::MenuSounds(std::function<bool()> ambient_wanted_,
                       std::function<bool()> music_wanted_, QObject* parent)
    : QObject{parent}, ambient_wanted{std::move(ambient_wanted_)},
      music_wanted{std::move(music_wanted_)} {
    music_timer.setSingleShot(true);
    connect(&music_timer, &QTimer::timeout, this, &MenuSounds::LoadSelectedMusic);
    music_worker = std::jthread([this](std::stop_token stop) { MusicWorker(stop); });
    if (const char* const dump = std::getenv("CHEEZYEMU_SOUND_DUMP");
        dump != nullptr && dump[0] != '\0') {
        Dump(QString::fromLocal8Bit(dump));
        return;
    }
    // Made before the sound starts, so the audio thread never waits for them.
    ClickWave();
    ChimeWave();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOG_WARNING(Frontend, "Menu sounds: no audio ({}); off", SDL_GetError());
        return;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, Rate};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, &player);
    if (stream == nullptr) {
        LOG_WARNING(Frontend, "Menu sounds: could not open the sound output ({}); off",
                    SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    instance = this;
    paused = true;
    connect(&timer, &QTimer::timeout, this, &MenuSounds::Tick);
    timer.start(200);
    Tick();
}

MenuSounds::~MenuSounds() {
    if (instance == this) {
        instance = nullptr;
    }
    if (stream != nullptr) {
        SDL_DestroyAudioStream(stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void MenuSounds::Click(int volume) {
    if (instance == nullptr) {
        return;
    }
    if (volume < 0) {
        if (!UISettings::values.menu_sounds.GetValue()) {
            return;
        }
        click_request_gain.store(volume_now.load());
    } else {
        click_request_gain.store(Gain(volume));
    }
    click_requests.fetch_add(1);
    instance->Wake();
}

void MenuSounds::Chime() {
    if (instance == nullptr || !UISettings::values.menu_sounds.GetValue()) {
        return;
    }
    chime_requests.fetch_add(1);
    instance->Wake();
}

void MenuSounds::Tick() {
    volume_now.store(Gain(static_cast<int>(UISettings::values.menu_sound_volume.GetValue())));
    const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;
    const bool home = ambient_wanted && ambient_wanted();
    // The game's music on the home screen while CheezyEmu is in front, and while the game
    // loads (its shaders building) even when it isn't.
    const bool music = UISettings::values.menu_game_music.GetValue() && music_available &&
                       music_wanted && music_wanted() && (active || !home);
    music_wanted_now.store(music ? 1.0f : 0.0f);
    // The playing song is near its end: load the next one.
    // (Not while another game's music is on its way: it replaces this song anyway.)
    if (want_next_music.exchange(false) && playing_program_id != 0 &&
        sounding_program_id == playing_program_id) {
        RequestMusic(true);
    }
    if (music) {
        Wake();
    }
    const bool hum = UISettings::values.menu_ambient.GetValue() && active && home && !music;
    hum_wanted.store(hum ? 1.0f : 0.0f);
    if (hum) {
        Wake();
        return;
    }
    // Nothing playing for three seconds: let the sound device rest.
    if (paused) {
        return;
    }
    if (!silent_now.load()) {
        silent_ticks = 0;
    } else if (++silent_ticks >= 15) {
        SDL_PauseAudioStreamDevice(stream);
        paused = true;
    }
}

void MenuSounds::SelectGame(const std::string& path, u64 program_id) {
    if (instance == nullptr || program_id == 0) {
        return;
    }
    instance->selected_path = path;
    instance->selected_program_id = program_id;
    instance->music_timer.start(450);
}

void MenuSounds::StartGame(const std::string& path, u64 program_id) {
    if (instance == nullptr || program_id == 0) {
        return;
    }
    instance->music_timer.stop();
    instance->selected_path = path;
    instance->selected_program_id = program_id;
    instance->LoadSelectedMusic();
}

void MenuSounds::LoadSelectedMusic() {
    if (selected_program_id == playing_program_id || selected_path.empty()) {
        return;
    }
    playing_program_id = selected_program_id;
    playing_path = selected_path;
    RequestMusic(false);
}

void MenuSounds::RequestMusic(bool queued) {
    {
        std::scoped_lock lock{request_mutex};
        request = MusicRequest{playing_path, playing_program_id,
                               UISettings::values.menu_music_random.GetValue(), queued,
                               ++request_serial};
    }
    request_ready.notify_one();
}

void MenuSounds::MusicWorker(std::stop_token stop) {
    // Read and decoded here (the first time a game is picked: its files are read; after that
    // its kept songs are). Only the newest request is loaded, and only its result is used.
    while (true) {
        MusicRequest wanted;
        {
            std::unique_lock lock{request_mutex};
            if (!request_ready.wait(lock, stop, [this] { return request.has_value(); })) {
                return;
            }
            wanted = std::move(*request);
            request.reset();
        }
        auto track = GameMusic::Load(wanted.path, wanted.program_id,
                                     wanted.random ? GameMusic::Choice::Random
                                                   : GameMusic::Choice::Title);
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, serial = wanted.serial, follows = wanted.queued,
                   loaded = std::move(track)]() mutable {
                if (!loaded || serial != request_serial ||
                    loaded->program_id != playing_program_id) {
                    return; // a newer pick
                }
                if (follows && loaded->samples.empty()) {
                    return;
                }
                music_available = !loaded->samples.empty();
                sounding_program_id = loaded->program_id;
                std::scoped_lock lock{next_music_mutex};
                next_music = std::move(loaded);
                next_music_ready = true;
                next_music_queued = follows;
                Wake();
            });
    }
}

void MenuSounds::Wake() {
    silent_ticks = 0;
    if (paused && stream != nullptr) {
        silent_now.store(false);
        SDL_ResumeAudioStreamDevice(stream);
        paused = false;
    }
}
