// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "discord.h"

#include <string>
#include <thread>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace DiscordRPC {

class DiscordImpl : public DiscordInterface {
public:
    DiscordImpl(Core::System& system_);
    ~DiscordImpl() override;

    void Pause() override;
    void Update() override;

private:
    std::string GetGameString(const std::string& title);
    void UpdateGameStatus(std::string_view game_url, bool use_default);

    std::string game_title{};
    Core::System& system;
    bool initialized = false; ///< Only with an application ID (discord_impl.cpp)
    s64 game_start = 0;       ///< When the game started (the elapsed time Discord shows)
    std::jthread lookup;      ///< The box art lookup, off the GUI thread
};

} // namespace DiscordRPC
