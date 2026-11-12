// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QVariantAnimation>
#include "qt_common/config/uisettings.h"
#include "yuzu/util/cheezy_theme.h"
#include "qt_common/game_list/game_list_p.h"
#include "yuzu/util/cheezy_backdrop.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/common.h"
#include "yuzu/game/game_card.h"
#include "yuzu/game/carousel.h"

namespace {

/// A speaker, with sound waves when the music plays and a cross when it is muted.
QIcon SpeakerIcon(bool playing) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(48.0 / 20.0, 48.0 / 20.0);
    const QColor colour{0xFF, 0xD9, 0x66};
    QPainterPath speaker;
    speaker.moveTo(2.5, 7.5);
    speaker.lineTo(6, 7.5);
    speaker.lineTo(10, 3.5);
    speaker.lineTo(10, 16.5);
    speaker.lineTo(6, 12.5);
    speaker.lineTo(2.5, 12.5);
    speaker.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawPath(speaker);
    QPen pen{colour, 1.6};
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (playing) {
        painter.drawArc(QRectF(8.5, 6.5, 5, 7), -60 * 16, 120 * 16);
        painter.drawArc(QRectF(8.5, 3.5, 9, 13), -60 * 16, 120 * 16);
    } else {
        painter.drawLine(QPointF(12.5, 7.5), QPointF(17, 12.5));
        painter.drawLine(QPointF(17, 7.5), QPointF(12.5, 12.5));
    }
    return QIcon{pixmap};
}

} // Anonymous namespace

GameCarousel::GameCarousel(QWidget* parent) : QListView{parent} {
    m_gameCard = new GameCard(this);
    setItemDelegate(m_gameCard);

    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setSpacing(10);
    setWordWrap(true);
    setTextElideMode(Qt::ElideRight);
    setFlow(QListView::LeftToRight);
    setWrapping(false);

    // The home view: the background is painted here (paintEvent), the clock kept current.
    setObjectName(QStringLiteral("GameCarousel"));
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    // Adding a game folder: the tree view has a row for it, the home view this button.
    m_addButton = new QPushButton(tr("+ Add games"), viewport());
    m_addButton->setObjectName(QStringLiteral("HomeAddGamesButton"));
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setToolTip(tr("Add a folder with games"));
    connect(m_addButton, &QPushButton::clicked, this, &GameCarousel::AddDirectoryRequested);
    // The tiles' size: a slider in the bottom-right corner, remembered.
    m_sizeSlider = new QSlider(Qt::Horizontal, viewport());
    m_sizeSlider->setObjectName(QStringLiteral("HomeTileSizeSlider"));
    m_sizeSlider->setRange(60, 160);
    m_sizeSlider->setValue(static_cast<int>(UISettings::values.home_tile_scale.GetValue()));
    m_sizeSlider->setFixedWidth(150);
    m_sizeSlider->setCursor(Qt::PointingHandCursor);
    m_sizeSlider->setToolTip(tr("Game card size"));
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        UISettings::values.home_tile_scale = static_cast<u32>(value);
        UpdateIconSize();
        viewport()->update();
    });
    // The home screen music (the ambient hum, util/menu_sounds.cpp): one click mutes it. The
    // same setting as Configure > General > UI > Sounds > Ambient hum.
    m_musicButton = new QPushButton(viewport());
    m_musicButton->setObjectName(QStringLiteral("HomeMusicButton"));
    m_musicButton->setCursor(Qt::PointingHandCursor);
    m_musicButton->setFixedSize(32, 32);
    m_musicButton->setIconSize(QSize(20, 20));
    connect(m_musicButton, &QPushButton::clicked, this, [this] {
        UISettings::values.menu_ambient = !UISettings::values.menu_ambient.GetValue();
        UpdateMusicButton();
    });
    UpdateMusicButton();

    // The backdrop moves at 30 frames a second while the view is shown and CheezyEmu is the
    // active window (it drifts slowly; a frame cost ~1/4 of a CPU core at 60).
    m_scene = std::make_unique<CheezyBackdrop::Scene>();
    m_frameClock = new QElapsedTimer();
    m_frameClock->start();
    auto* const motion = new QTimer(this);
    motion->setTimerType(Qt::PreciseTimer);
    connect(motion, &QTimer::timeout, this, [this] {
        const qreal dt = std::min(0.05, m_frameClock->restart() / 1000.0);
        if (!isVisible() || QGuiApplication::applicationState() != Qt::ApplicationActive) {
            return;
        }
        m_scene->Step(dt, QRectF(viewport()->rect()));
        viewport()->update();
    });
    motion->start(33);
}

GameCarousel::~GameCarousel() {
    delete m_frameClock;
}

void GameCarousel::RebuildAmbient() {
    m_ambient = CheezyBackdrop::MakeAmbient(m_ambientSource, viewport()->size());
}

void GameCarousel::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    PlaceAddButton();
    if (m_ambient.size() != viewport()->size()) {
        RebuildAmbient();
        m_ambientPrevious = {};
    }
}

void GameCarousel::PlaceAddButton() {
    if (m_addButton == nullptr) {
        return;
    }
    // Under the clock, on the right (the viewport has its final size only once it is shown).
    m_addButton->adjustSize();
    const int clock_bottom = 18 + std::clamp(viewport()->height() / 18, 18, 30) + 16;
    const QPoint place{viewport()->width() - 40 - m_addButton->width(), clock_bottom};
    if (m_addButton->pos() != place) {
        m_addButton->move(place);
    }
    if (m_sizeSlider != nullptr) {
        const QPoint slider_place{viewport()->width() - 40 - m_sizeSlider->width(),
                                  viewport()->height() - 36};
        if (m_sizeSlider->pos() != slider_place) {
            m_sizeSlider->move(slider_place);
        }
        if (m_musicButton != nullptr) {
            const QPoint music_place{slider_place.x() - 14 - m_musicButton->width(),
                                     slider_place.y() + m_sizeSlider->height() / 2 -
                                         m_musicButton->height() / 2};
            if (m_musicButton->pos() != music_place) {
                m_musicButton->move(music_place);
            }
            // Configure may have changed it.
            if (m_musicButton->property("playing").toBool() !=
                UISettings::values.menu_ambient.GetValue()) {
                UpdateMusicButton();
            }
        }
    }
}

void GameCarousel::UpdateMusicButton() {
    const bool playing = UISettings::values.menu_ambient.GetValue();
    m_musicButton->setProperty("playing", playing);
    m_musicButton->setIcon(SpeakerIcon(playing));
    m_musicButton->setToolTip(playing ? tr("Mute the home screen music")
                                      : tr("Play the home screen music"));
}

void GameCarousel::paintEvent(QPaintEvent* event) {
    PlaceAddButton();
    {
        QPainter painter{viewport()};
        painter.setRenderHint(QPainter::Antialiasing);
        const QRect area = viewport()->rect();

        if (m_ambient.size() != area.size() && !m_ambientSource.isNull()) {
            RebuildAmbient();
        }
        m_scene->Paint(painter, area, m_ambient, 0.5 * m_ambientFade, m_ambientPrevious,
                       0.5 * (1.0 - m_ambientFade));

        // A greeting on the left, the clock on the right.
        const int hour = QTime::currentTime().hour();
        const QString greeting = hour < 5    ? tr("Up late?")
                                 : hour < 12 ? tr("Good morning!")
                                 : hour < 18 ? tr("Good afternoon!")
                                             : tr("Good evening!");
        QFont big = font();
        big.setBold(true);
        big.setPixelSize(std::clamp(area.height() / 18, 18, 30));
        QFont small = font();
        small.setPixelSize(std::clamp(area.height() / 34, 12, 16));
        const int left = 40;
        const int top = 18;
        painter.setFont(big);
        painter.setPen(CheezyTheme::Recolor(QColor(0xFF, 0xF4, 0xDA)));
        const QFontMetrics big_metrics{big};
        painter.drawText(QPoint(left, top + big_metrics.ascent()), greeting);
        painter.setFont(small);
        painter.setPen(CheezyTheme::Recolor(QColor(0xE8, 0xD9, 0xB5, 190)));
        painter.drawText(QPoint(left + 2, top + big_metrics.height() + QFontMetrics{small}.ascent()),
                         model() != nullptr && model()->rowCount() > 0
                             ? tr("Pick a game and let's play.")
                             : tr("Add a game folder to get started."));
        painter.setFont(big);
        painter.setPen(CheezyTheme::Recolor(QColor(0xFF, 0xC8, 0x3D)));
        const QString time = QTime::currentTime().toString(QStringLiteral("h:mm"));
        painter.drawText(QPoint(area.width() - left - big_metrics.horizontalAdvance(time),
                                top + big_metrics.ascent()),
                         time);
    }
    QListView::paintEvent(event);
}

void GameCarousel::SetModel(GameListModel* model) {
    QListView::setModel(model);
    UpdateIconSize();
    // Games arrive after the view is set up (the scan runs in the background): pick the first
    // one as soon as there is one, as a console's home screen does.
    const auto pick_first = [this] {
        if (!currentIndex().isValid() && this->model() != nullptr && this->model()->rowCount() > 0) {
            setCurrentIndex(this->model()->index(0, 0));
        }
    };
    connect(model, &QAbstractItemModel::rowsInserted, this, pick_first);
    connect(model, &QAbstractItemModel::modelReset, this, pick_first);

    // The picked tile springs up: a short animation with a little overshoot.
    if (m_liftAnimation == nullptr) {
        m_liftAnimation = new QVariantAnimation(this);
        m_liftAnimation->setStartValue(0.0);
        m_liftAnimation->setEndValue(1.0);
        m_liftAnimation->setDuration(380);
        m_liftAnimation->setEasingCurve(QEasingCurve::OutBack);
        connect(m_liftAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& lift) {
                    m_gameCard->setLift(lift.toReal());
                    viewport()->update();
                });
    }
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                m_liftAnimation->stop();
                m_liftAnimation->start();
                // The backdrop takes the picked game's colours, cross-fading from the last.
                m_ambientPrevious = m_ambient;
                m_ambientSource = current.data(GameListItemPath::LargePictureRole).value<QPixmap>();
                RebuildAmbient();
                if (m_ambientAnimation == nullptr) {
                    m_ambientAnimation = new QVariantAnimation(this);
                    m_ambientAnimation->setStartValue(0.0);
                    m_ambientAnimation->setEndValue(1.0);
                    m_ambientAnimation->setDuration(600);
                    m_ambientAnimation->setEasingCurve(QEasingCurve::InOutSine);
                    connect(m_ambientAnimation, &QVariantAnimation::valueChanged, this,
                            [this](const QVariant& fade) { m_ambientFade = fade.toReal(); });
                }
                m_ambientAnimation->stop();
                m_ambientAnimation->start();
                // Glide the row so the picked game is in view (no jump).
                if (!current.isValid()) {
                    return;
                }
                const QRect tile = visualRect(current);
                QScrollBar* const bar = horizontalScrollBar();
                const int target = std::clamp(
                    bar->value() + tile.center().x() - viewport()->width() / 2, bar->minimum(),
                    bar->maximum());
                if (target == bar->value()) {
                    return;
                }
                auto* const glide = new QPropertyAnimation(bar, "value", this);
                glide->setDuration(320);
                glide->setEasingCurve(QEasingCurve::OutCubic);
                glide->setStartValue(bar->value());
                glide->setEndValue(target);
                glide->start(QAbstractAnimation::DeleteWhenStopped);
            });
}

void GameCarousel::ApplyFilter(const QString& edit_filter_text, GameListModel* model) {
    int row_count = model->rowCount();

    for (int i = 0; i < row_count; ++i) {
        QStandardItem* item = model->item(i, 0);
        if (!item)
            continue;

        if (Yuzu::FilterMatches(edit_filter_text, item)) {
            setRowHidden(i, false);
        } else {
            setRowHidden(i, true);
        }
    }
}

void GameCarousel::UpdateIconSize() {
    // Big tiles that follow the window's height, whatever the game list's icon size.
    const int tile =
        std::clamp(static_cast<int>(viewport()->height() * 9 / 20 *
                                    UISettings::values.home_tile_scale.GetValue() / 100),
                   90, 460);
    const QSize grid_size(tile + tile / 3, std::max(tile + 120, viewport()->height()));
    if (gridSize() != grid_size) {
        setUpdatesEnabled(false);
        setGridSize(grid_size);
        m_gameCard->setSize(grid_size, QSize(tile, tile), 0, 0);
        setUpdatesEnabled(true);
    }
    // Something is always picked, as on a console's home screen.
    if (!currentIndex().isValid() && model() != nullptr && model()->rowCount() > 0) {
        setCurrentIndex(model()->index(0, 0));
    }
}

QModelIndex GameCarousel::indexAt(const QPoint& point) const {
    QModelIndex index = QListView::indexAt(point);
    if (!index.isValid())
        return {};
    if (m_gameCard && !m_gameCard->hitTest(point, index, this, visualRect(index)))
        return {};
    return index;
}
