// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include <boost/algorithm/string/replace.hpp>
#include "common/httplib.h"
#include "common/net/ca_certificates.h"

#include <discord_rpc.h>
#include <fmt/format.h>

#include "common/common_types.h"
#include "common/logging.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/loader/loader.h"

#include "discord_impl.h"

namespace DiscordRPC {

namespace {

/// CheezyEmu's Discord application (discord.com/developers/applications): its name is what
/// Discord shows as "Playing ...". Empty: presence stays off. CHEEZYEMU_DISCORD_APP_ID=<id>
/// sets it for a test.
constexpr char CheezyEmuDiscordAppId[] = "1570203948462247956";

std::string AppId() {
    if (const char* const forced = std::getenv("CHEEZYEMU_DISCORD_APP_ID");
        forced != nullptr && forced[0] != '\0') {
        return forced;
    }
    return CheezyEmuDiscordAppId;
}

constexpr char LogoText[] = "CheezyEmu";
constexpr char LogoUrl[] =
    "https://raw.githubusercontent.com/CheezerWheezer/CheezyEmu/main/dist/branding/"
    "cheezyemu_icon.png";

s64 Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // Anonymous namespace

DiscordImpl::DiscordImpl(Core::System& system_) : system{system_} {
    const std::string app_id = AppId();
    if (app_id.empty()) {
        LOG_INFO(Frontend, "Discord presence: no CheezyEmu application ID in this build; off");
        return;
    }
    DiscordEventHandlers handlers{};
    Discord_Initialize(app_id.c_str(), &handlers, 1, nullptr);
    initialized = true;
}

DiscordImpl::~DiscordImpl() {
    if (lookup.joinable()) {
        lookup.join();
    }
    if (initialized) {
        Discord_ClearPresence();
        Discord_Shutdown();
    }
}

void DiscordImpl::Pause() {
    if (initialized) {
        Discord_ClearPresence();
    }
}

std::string DiscordImpl::GetGameString(const std::string& title) {
    // Convert to lowercase
    std::string icon_name = Common::ToLower(title);

    // Replace spaces with dashes
    std::replace(icon_name.begin(), icon_name.end(), ' ', '-');
    boost::replace_all(icon_name, "é", "e");

    // Remove non-alphanumeric characters but keep dashes
    std::erase_if(icon_name, [](char c) { return !std::isalnum(c) && c != '-'; });

    // Remove dashes from the start and end of the string
    icon_name.erase(icon_name.begin(), std::find_if(icon_name.begin(), icon_name.end(),
                                                    [](int ch) { return ch != '-'; }));
    icon_name.erase(
        std::find_if(icon_name.rbegin(), icon_name.rend(), [](int ch) { return ch != '-'; }).base(),
        icon_name.end());

    // Remove double dashes
    icon_name.erase(std::unique(icon_name.begin(), icon_name.end(),
                                [](char a, char b) { return a == '-' && b == '-'; }),
                    icon_name.end());

    return icon_name;
}

void DiscordImpl::UpdateGameStatus(std::string_view game_url, bool has_boxart) {
    // "Playing CheezyEmu" / "The Legend of Zelda: Ocarina of Time" / "elapsed 12:34".
    const std::string art = std::string{has_boxart ? game_url : LogoUrl};
    const std::string title = game_title;
    DiscordRichPresence presence{};
    presence.largeImageKey = art.c_str();
    presence.largeImageText = title.c_str();
    presence.smallImageKey = LogoUrl;
    presence.smallImageText = LogoText;
    presence.details = title.c_str();
    presence.state = "Playing on CheezyEmu";
    presence.status_display_type = DiscordStatusDisplayType_Details;
    presence.startTimestamp = game_start;
    Discord_UpdatePresence(&presence);
}

void DiscordImpl::Update() {
    if (!initialized) {
        return;
    }
    if (lookup.joinable()) {
        lookup.join();
    }
    if (system.IsPoweredOn()) {
        system.GetAppLoader().ReadTitle(game_title);
        game_start = Now();
        // Shown at once with the logo; the game's box art replaces it when the lookup (a
        // network request, off this thread) finds one.
        UpdateGameStatus({}, false);
        // Art, first found: CheezyEmu's own (dist/discord/<program id>.png in its repository),
        // then the boxart collection by name.
        u64 program_id{};
        system.GetAppLoader().ReadProgramId(program_id);
        std::vector<std::string> candidates{
            fmt::format("https://raw.githubusercontent.com/CheezerWheezer/CheezyEmu/main/dist/"
                        "discord/{:016x}.png",
                        program_id),
            fmt::format("https://raw.githubusercontent.com/eden-emulator/boxart/refs/heads/master/"
                        "img/{}.png",
                        GetGameString(game_title)),
        };
        lookup = std::jthread([this, candidates = std::move(candidates)](std::stop_token stop) {
            for (const std::string& url : candidates) {
                if (stop.stop_requested()) {
                    return;
                }
                httplib::SSLClient client(url);
                client.set_connection_timeout(3);
                client.set_read_timeout(3);
                client.set_follow_location(true);
                Common::Net::ConfigureCaCertificates(client);
                httplib::Request request{
                    .method = "HEAD",
                    .path = url,
                };
                const auto result = client.send(request);
                if (result && result->status == 200) {
                    if (!stop.stop_requested()) {
                        UpdateGameStatus(url, true);
                    }
                    return;
                }
            }
        });
        return;
    }

    DiscordRichPresence presence{};
    presence.largeImageKey = LogoUrl;
    presence.largeImageText = LogoText;
    presence.status_display_type = DiscordStatusDisplayType_Name;
    presence.details = "Picking a game";
    presence.startTimestamp = Now();
    Discord_UpdatePresence(&presence);
}

} // namespace DiscordRPC
