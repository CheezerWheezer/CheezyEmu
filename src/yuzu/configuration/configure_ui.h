// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QWidget>
#include "common/settings_enums.h"

namespace Core {
class System;
}

namespace Ui {
class ConfigureUi;
}

class QCheckBox;
class QComboBox;
class QSlider;

class ConfigureUi : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureUi(Core::System& system_, QWidget* parent = nullptr);
    ~ConfigureUi() override;

    void ApplyConfiguration();

    void UpdateScreenshotInfo(Settings::AspectRatio ratio,
                              Settings::ResolutionSetup resolution_info);

private slots:
    void OnLanguageChanged(int index);

signals:
    void LanguageChanged(const QString& locale);

private:
    void RequestGameListUpdate();

    void SetConfiguration();

    void changeEvent(QEvent*) override;
    void RetranslateUI();

    void InitializeLanguageComboBox();
    void InitializeIconSizeComboBox();
    void InitializeRowComboBoxes();

    void UpdateFirstRowComboBox(bool init = false);
    void UpdateSecondRowComboBox(bool init = false);

    void UpdateWidthText();

    std::unique_ptr<Ui::ConfigureUi> ui;
    /// Sounds (menu_sounds.cpp): made in code, under the game list box.
    QCheckBox* menu_sounds_box = nullptr;
    QCheckBox* menu_ambient_box = nullptr;
    QCheckBox* menu_game_music_box = nullptr;
    QComboBox* menu_music_choice = nullptr;
    QSlider* menu_volume_slider = nullptr;

    Settings::AspectRatio ratio;
    Settings::ResolutionSetup resolution_setting;
    Core::System& system;
};
