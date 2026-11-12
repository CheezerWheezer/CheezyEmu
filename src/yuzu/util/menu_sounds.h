// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <QObject>
#include <QTimer>

#include "common/common_types.h"
#include "common/polyfill_thread.h"

struct SDL_AudioStream;

/// Menu sounds (menu_sounds.cpp): a soft click when the picked game changes, a chime when a game
/// starts, and a quiet ambient hum on the home screen. All three are made in code (no sound
/// files) and played through SDL, so they sound the same on Windows, Linux and macOS.
///
/// Settings (Configure > Interface > Sounds): menu_sounds, menu_ambient, menu_sound_volume.
/// CHEEZYEMU_SOUND_DUMP=<folder> writes click.wav, chime.wav and hum.wav there instead of
/// playing anything (to listen to them or tune them).
class MenuSounds : public QObject {
    Q_OBJECT

public:
    /// `ambient_wanted` says whether the home screen is showing (no game running); the hum
    /// plays while it is, the window is in front and the setting is on.
    /// `music_wanted`: the picked game's music may play (the home screen, or the game loading).
    explicit MenuSounds(std::function<bool()> ambient_wanted, std::function<bool()> music_wanted,
                        QObject* parent);
    ~MenuSounds() override;

    /// The click for a newly picked game. `volume` (0 to 100) overrides the setting (preview).
    static void Click(int volume = -1);
    /// The chime for starting a game.
    static void Chime();
    /// The game now picked in the game list: its own title music fades in a moment later (read
    /// from its files on another thread the first time), crossfading from the last game's.
    static void SelectGame(const std::string& path, u64 program_id);

private:
    void Tick();
    void Wake();

    void LoadSelectedMusic();

    std::function<bool()> ambient_wanted;
    std::function<bool()> music_wanted;
    QTimer timer;
    QTimer music_timer; ///< A short wait after a pick, so scrolling past games loads nothing
    std::string selected_path;
    u64 selected_program_id = 0;
    u64 playing_program_id = 0;
    bool music_available = false; ///< The playing game has a track
    std::jthread music_loader;
    SDL_AudioStream* stream = nullptr;
    bool paused = false;
    int silent_ticks = 0;
};
