// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <QListView>
#include <QPixmap>

namespace CheezyBackdrop {
class Scene;
}
#include <QString>

class GameCard;
class GameListModel;
class QResizeEvent;

class GameCarousel : public QListView {
    Q_OBJECT

public:
    explicit GameCarousel(QWidget* parent = nullptr);
    ~GameCarousel() override;

    void SetModel(GameListModel* model);
    void ApplyFilter(const QString& edit_filter_text, GameListModel* model);
    void UpdateIconSize();

    QModelIndex indexAt(const QPoint& point) const override;

signals:
    /// The home view's "+ Add games" button.
    void AddDirectoryRequested();

protected:
    /// The warm background, a greeting and a clock, then the tiles.
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void PlaceAddButton();

    GameCard* m_gameCard = nullptr;
    // The backdrop (util/cheezy_backdrop.h): the picked game's blurred picture, cross-faded, and
    // the moving scene.
    void RebuildAmbient();
    QPixmap m_ambientSource;
    QPixmap m_ambient;
    QPixmap m_ambientPrevious;
    qreal m_ambientFade = 1.0;
    class QVariantAnimation* m_ambientAnimation = nullptr;
    std::unique_ptr<CheezyBackdrop::Scene> m_scene;
    class QElapsedTimer* m_frameClock = nullptr;
    class QSlider* m_sizeSlider = nullptr;
    class QVariantAnimation* m_liftAnimation = nullptr;
    class QPushButton* m_addButton = nullptr;
    /// The home screen music (menu_ambient) on or off, left of the size slider.
    class QPushButton* m_musicButton = nullptr;
    void UpdateMusicButton();
};
