// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QObject>
#include <QTimer>

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
    explicit MenuSounds(std::function<bool()> ambient_wanted, QObject* parent);
    ~MenuSounds() override;

    /// The click for a newly picked game. `volume` (0 to 100) overrides the setting (preview).
    static void Click(int volume = -1);
    /// The chime for starting a game.
    static void Chime();

private:
    void Tick();
    void Wake();

    std::function<bool()> ambient_wanted;
    QTimer timer;
    SDL_AudioStream* stream = nullptr;
    bool paused = false;
    int silent_ticks = 0;
};
