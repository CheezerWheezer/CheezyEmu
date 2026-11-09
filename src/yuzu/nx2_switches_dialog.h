// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QDialog>

class QCheckBox;

/// Tools > Switch 2 Switches: on/off toggles for the switches players otherwise edit in
/// nx2_switches.txt. Saves to the user's own copy (%APPDATA%\cheezyemu\nx2_switches.txt, or
/// ~/.config/cheezyemu/nx2_switches.txt), which wins over the one shipped next to the program.
/// They are read at startup, so changes apply after a restart.
class Nx2SwitchesDialog : public QDialog {
public:
    explicit Nx2SwitchesDialog(QWidget* parent);

private:
    void Save();
    void RestoreDefaults();

    std::vector<QCheckBox*> boxes; ///< One per entry of the switch table, in order
};
