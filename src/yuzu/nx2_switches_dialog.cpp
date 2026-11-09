// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "yuzu/nx2_switches_dialog.h"

namespace {

QString Tr(const char* text) {
    return QCoreApplication::translate("Nx2SwitchesDialog", text);
}

struct Switch {
    const char* name;        ///< The environment variable
    const char* label;       ///< The checkbox
    const char* description; ///< Shown under it
    bool default_on;         ///< What the program does with no value set
    const char* on_value;    ///< Written when turned on and that isn't the default
    const char* off_value;   ///< Written when turned off and that isn't the default
};

// Player-facing switches only; nx2_switches.txt describes the rest.
constexpr std::array switches{
    Switch{"EDEN_NX2_SEM_MIRROR", QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Semaphore mirror"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Copies GPU queue progress into the page the GPU waits on (new in "
                             "build 11). Fixes freezes and black screens after cutscenes. Turn it "
                             "off to test whether it causes a graphics problem."),
           true, "1", "0"},
    Switch{"EDEN_NX2_SPLIT", QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "GPU thread split"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Runs GPU commands on a second thread. Up to 50% more FPS in Switch 2 "
                             "games. Turn off only to test a problem."),
           true, "2", "0"},
    Switch{"EDEN_NX2_FENCE_BATCH", QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Batch GPU fences"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Signals finished GPU work in batches: less driver overhead, smoother "
                             "frame times."),
           true, "1", "0"},
    Switch{"EDEN_NX2_AMD_FG_HUD_MASK",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Steady HUD with AMD frame generation"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Keeps Ocarina of Time's HUD still in generated frames instead of "
                             "smearing it with the scene."),
           true, "1", "0"},
    Switch{"EDEN_NX2_OOT_JITTER",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "FSR camera jitter (sharper FSR, test)"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Ocarina of Time: nudges the camera by less than a pixel every frame, "
                             "which FSR needs to rebuild fine detail (less blocky and shimmery, "
                             "most on Performance and below). New: turn it off again if the "
                             "picture shakes or the HUD jitters, and tell us."),
           false, "1", "0"},
    Switch{"EDEN_NX2_FSR_VERSION",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Use FSR 3.1 instead of FSR 4 (faster)"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "FSR 4 looks best but is heavy on anything except the RX 9000 series: "
                             "on RX 6000 and 7000 it can cost more than it saves. FSR 3.1 is much "
                             "lighter, a little softer and ghosts more."),
           false, "3", "4"},
    Switch{"EDEN_NX2_FSR4_CHECK",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Fall back to FSR 3.1 when FSR 4 draws black"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Checks a few pixels of FSR 4's picture and switches to FSR 3.1 when "
                             "it is black (AMD driver 2.0.406 on RX 9000 does that; update the "
                             "driver). Turn off to keep FSR 4 if it switched with a picture that "
                             "looked fine."),
           true, "1", "0"},
    Switch{"EDEN_NX2_RESETFIX",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Donkey Kong Bananza terrain fix"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Fixes terrain and indirect drawing in Donkey "
                                                  "Kong Bananza."),
           true, "1", "0"},
    Switch{"EDEN_NX2_KEEP_ASTC",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Keep ASTC textures uncompressed"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Keeps ASTC textures as RGBA8 instead of re-compressing them to BC3 "
                             "on cards with less than 20 GB. Removes grid lines on distant terrain "
                             "on AMD, but uses much more video memory."),
           false, "1", "0"},
    Switch{"EDEN_NX2_PRELOAD_ALL_PIPELINES",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "Build every saved shader at boot"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Builds the game's whole shader cache before it starts, so new areas "
                             "don't stutter. Run once with it on to compile and save them all, "
                             "then turn it off (it uses a lot of RAM)."),
           false, "1", "0"},
    Switch{"EDEN_NX2_NO_REACTIVE",
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog", "No reactive flushing (Switch 2)"),
           QT_TRANSLATE_NOOP("Nx2SwitchesDialog",
                             "Drops the read traps that copy GPU results back when the game reads "
                             "them. Can be faster or slower depending on the game; can break "
                             "effects."),
           false, "1", "0"},
};

std::filesystem::path UserSwitchFile() {
#ifdef _WIN32
    const auto base = Common::FS::GetAppDataRoamingDirectory();
#else
    const auto base = Common::FS::GetDataDirectory("XDG_CONFIG_HOME");
#endif
    return base / EDEN_DIR / "nx2_switches.txt";
}

std::string Trim(std::string text) {
    constexpr const char* blank = " \t\r";
    text.erase(0, text.find_first_not_of(blank));
    text.erase(text.find_last_not_of(blank) + 1);
    return text;
}

/// The "NAME" of a "NAME=VALUE" or "#NAME=VALUE" line, else empty.
std::string LineName(const std::string& raw) {
    std::string line = Trim(raw);
    while (!line.empty() && line[0] == '#') {
        line = Trim(line.substr(1));
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
        return {};
    }
    std::string name = Trim(line.substr(0, eq));
    return name.find(' ') == std::string::npos ? name : std::string{};
}

/// The user file's active values.
std::map<std::string, std::string> ReadUserValues() {
    std::map<std::string, std::string> values;
    std::ifstream file{UserSwitchFile()};
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq != std::string::npos) {
            values[Trim(trimmed.substr(0, eq))] = Trim(trimmed.substr(eq + 1));
        }
    }
    return values;
}

/// On or off: the user file's value if it has one (saved since the start), else the value
/// CheezyEmu started with, else the default.
bool IsOn(const Switch& entry, const std::map<std::string, std::string>& user) {
    std::string value;
    if (const auto it = user.find(entry.name); it != user.end()) {
        value = it->second;
    } else if (const char* env = std::getenv(entry.name)) {
        value = env;
    } else {
        return entry.default_on;
    }
    if (value.empty()) {
        return entry.default_on;
    }
    return entry.default_on ? value[0] != '0' : value[0] == entry.on_value[0];
}

} // Anonymous namespace

Nx2SwitchesDialog::Nx2SwitchesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(Tr("Switch 2 Switches"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        Tr("Advanced switches for Switch 2 games (the ones in nx2_switches.txt). Saved to "
           "your own nx2_switches.txt:\n%1\nRestart CheezyEmu after changing them.")
            .arg(QString::fromStdWString(UserSwitchFile().wstring())),
        this);
    intro->setWordWrap(true);
    intro->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(intro);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* list = new QWidget(scroll);
    auto* list_layout = new QVBoxLayout(list);
    const auto user = ReadUserValues();
    for (const Switch& entry : switches) {
        auto* box = new QCheckBox(Tr(entry.label), list);
        box->setChecked(IsOn(entry, user));
        box->setToolTip(QString::fromLatin1(entry.name));
        auto* description = new QLabel(Tr(entry.description), list);
        description->setWordWrap(true);
        description->setContentsMargins(24, 0, 0, 8);
        QFont small = description->font();
        small.setPointSizeF(small.pointSizeF() * 0.9);
        description->setFont(small);
        list_layout->addWidget(box);
        list_layout->addWidget(description);
        boxes.push_back(box);
    }
    list_layout->addStretch();
    scroll->setWidget(list);
    layout->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { Save(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this] { RestoreDefaults(); });
    layout->addWidget(buttons);
    resize(560, 620);
}

void Nx2SwitchesDialog::RestoreDefaults() {
    for (size_t i = 0; i < switches.size(); ++i) {
        boxes[i]->setChecked(switches[i].default_on);
    }
}

void Nx2SwitchesDialog::Save() {
    const auto path = UserSwitchFile();
    // Keep every line that isn't one of these switches (comments, other switches).
    std::vector<std::string> kept;
    {
        std::ifstream file{path};
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::string name = LineName(line);
            const bool ours = std::any_of(switches.begin(), switches.end(),
                                          [&](const Switch& s) { return name == s.name; });
            if (!ours) {
                kept.push_back(line);
            }
        }
    }
    if (kept.empty()) {
        kept.push_back("# Your CheezyEmu switches: NAME=VALUE lines, read when CheezyEmu starts.");
        kept.push_back("# Tools > Switch 2 Switches edits the ones it lists.");
    }
    while (!kept.empty() && Trim(kept.back()).empty()) {
        kept.pop_back();
    }
    std::vector<std::string> changed;
    for (size_t i = 0; i < switches.size(); ++i) {
        const Switch& entry = switches[i];
        const bool on = boxes[i]->isChecked();
        if (on != entry.default_on) {
            changed.push_back(std::string{entry.name} + "=" + (on ? entry.on_value : entry.off_value));
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file{path, std::ios::trunc};
    for (const auto& line : kept) {
        file << line << '\n';
    }
    if (!changed.empty()) {
        file << "\n# Set in Tools > Switch 2 Switches\n";
        for (const auto& line : changed) {
            file << line << '\n';
        }
    }
    file.close();
    if (!file) {
        QMessageBox::warning(this, Tr("Switch 2 Switches"),
                             Tr("Couldn't save %1.")
                                 .arg(QString::fromStdWString(path.wstring())));
        return;
    }
    QMessageBox::information(this, Tr("Switch 2 Switches"),
                             Tr("Saved. Restart CheezyEmu for the changes to take effect."));
    accept();
}
