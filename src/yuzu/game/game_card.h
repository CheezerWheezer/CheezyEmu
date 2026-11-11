// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>

/**
 * A stylized "card"-like delegate for the game grid view.
 * Adapted from QML
 */
class GameCard : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GameCard(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QRect getCardRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;

    bool hitTest(const QPoint& point, const QModelIndex& index,
                 const QWidget* widget, const QRect& cellRect) const;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setSize(const QSize& newSize, const QSize& contentSize, const int padding, const int columns);

    /// How far the selected home tile is lifted, 0 to 1 (an easing with overshoot animates it).
    void setLift(qreal lift) {
        m_lift = lift;
    }

private:
    /// The home view (carousel): a big rounded tile, the selected one lifted with a glowing
    /// cheese outline and its name under it.
    void PaintHomeTile(QPainter* painter, const QStyleOptionViewItem& option,
                       const QModelIndex& index) const;

    static constexpr int cardMargin = 8;

    qreal m_lift = 1.0;
    QSize m_size;
    QSize m_contentSize;
    int m_padding;
    int m_columns;
};
