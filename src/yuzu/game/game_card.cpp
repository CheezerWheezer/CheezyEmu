// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QPainter>
#include <QPainterPath>
#include "game_card.h"
#include "yuzu/util/cheezy_theme.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

namespace {
// CheezyEmu's colours (dist/qt_themes/cheezy/style.qss).
const QColor CheeseBase{0xFF, 0xC8, 0x3D};
const QColor CheeseDeepBase{0xE8, 0x9E, 0x10};
const QColor CreamBase{0xFF, 0xF4, 0xDA};
/// How far the home tiles sit in from their cells' centre (room for the first one's glow).
constexpr int HomeInset = 28;
} // Anonymous namespace

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid())
        return;
    if (m_columns < 1) {
        PaintHomeTile(painter, option, index);
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    constexpr int cardCornerRadius = 14;

    const QRect cardRect = getCardRect(option, index);

    QPalette palette = option.palette;
    QColor backgroundColor = palette.window().color();
    QColor borderColor = palette.dark().color();
    QColor textColor = palette.text().color();

    // highlight on select or hover: a cheese outline, as on the home view
    QPen borderPen{borderColor, 1};
    if (option.state & QStyle::State_Selected) {
        backgroundColor = backgroundColor.lighter(125);
        borderPen = QPen(CheezyTheme::Recolor(CheeseBase), 3);
        textColor = CheezyTheme::Recolor(CreamBase);
    } else if (option.state & QStyle::State_MouseOver) {
        backgroundColor = backgroundColor.lighter(120);
        borderPen = QPen(CheezyTheme::Recolor(CheeseBase).darker(130), 1);
    }

    painter->setBrush(backgroundColor);
    painter->setPen(borderPen);
    painter->drawRoundedRect(cardRect, cardCornerRadius, cardCornerRadius);

    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    QPixmap icon_pixmap = index.data(Qt::DecorationRole).value<QPixmap>();

    QRect iconRect;
    if (!icon_pixmap.isNull()) {
        QSize scaled = icon_pixmap.size();
        scaled.scale(icon_size, icon_size, Qt::KeepAspectRatio);

        iconRect = {cardRect.left() + (cardRect.width() - scaled.width()) / 2,
                    cardRect.top() + cardMargin - 1, scaled.width(), scaled.height()};

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        painter->save();
        QPainterPath clip_path;
        clip_path.addRoundedRect(iconRect, cardCornerRadius, cardCornerRadius);
        painter->setClipPath(clip_path);
        painter->drawPixmap(iconRect, icon_pixmap);
        painter->restore();
    } else {
        iconRect = {cardRect.left() + cardMargin, cardRect.top() + cardMargin,
                    static_cast<int>(icon_size), static_cast<int>(icon_size)};
    }

    if (UISettings::values.show_game_name.GetValue()) {
        QRect textRect = cardRect;
        textRect.setTop(iconRect.bottom() + cardMargin);
        textRect.adjust(cardMargin, 0, -cardMargin, -cardMargin);

        QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();

        painter->setPen(textColor);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::max(11.0, std::sqrt(static_cast<double>(icon_size))));
        painter->setFont(font);

        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title);
    }

    painter->restore();
}

void GameCard::PaintHomeTile(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    const QRect cell = option.rect;
    const int tile = m_contentSize.width();
    // The tile sits a little above the middle; the name goes under it.
    // Shifted in from the view's left edge (the first tile's glow and name need the room).
    QRect tile_rect{cell.center().x() - tile / 2 + HomeInset, cell.top() + (cell.height() - tile) / 2 - 16,
                    tile, tile};
    if (selected) {
        // Lifted towards the player, like a picked card (springs up when picked).
        const int grow = static_cast<int>(tile / 20.0 * m_lift);
        tile_rect.adjust(-grow, -grow - grow / 2, grow, grow - grow / 2);
    } else if (hovered) {
        tile_rect.translate(0, -3);
    }
    const qreal radius = tile_rect.width() * 0.11;

    // A soft shadow under the tile.
    painter->setPen(Qt::NoPen);
    for (int i = 3; i >= 1; --i) {
        painter->setBrush(QColor(0, 0, 0, 30));
        painter->drawRoundedRect(QRectF(tile_rect).translated(0, 3 + 3 * i).adjusted(-i, -i, i, i),
                                 radius + i, radius + i);
    }

    // The selected tile glows cheese.
    if (selected) {
        for (int i = 8; i >= 1; --i) {
            QColor glow = CheezyTheme::Recolor(CheeseBase);
            glow.setAlpha(static_cast<int>((6 + (8 - i) * 4) * m_lift));
            painter->setPen(QPen(glow, 2));
            painter->setBrush(Qt::NoBrush);
            const qreal spread = 3.0 + i * 2.2;
            painter->drawRoundedRect(QRectF(tile_rect).adjusted(-spread, -spread, spread, spread),
                                     radius + spread, radius + spread);
        }
    }

    // The picture fills the tile (cropped to a square), or a cheese tile with the initials.
    QPainterPath clip;
    clip.addRoundedRect(QRectF(tile_rect), radius, radius);
    painter->setClipPath(clip);
    QPixmap picture = index.data(GameListItemPath::LargePictureRole).value<QPixmap>();
    if (picture.isNull()) {
        picture = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    const QString title =
        index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    if (!picture.isNull()) {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QSize target = tile_rect.size() * dpr;
        // Scaled once per picture and size (this ran every frame of the moving home view).
        const QString key = QStringLiteral("%1-%2x%3")
                                .arg(picture.cacheKey())
                                .arg(target.width())
                                .arg(target.height());
        auto cached = m_scaled.find(key);
        if (cached == m_scaled.end()) {
            if (m_scaled.size() > 96) {
                m_scaled.clear();
            }
            cached = m_scaled.insert(key, picture.scaled(target, Qt::KeepAspectRatioByExpanding,
                                                         Qt::SmoothTransformation));
        }
        const QPixmap& scaled = cached.value();
        const QRect source{(scaled.width() - target.width()) / 2,
                           (scaled.height() - target.height()) / 2, target.width(),
                           target.height()};
        painter->drawPixmap(tile_rect, scaled, source);
    } else {
        QLinearGradient fill{tile_rect.topLeft(), tile_rect.bottomRight()};
        fill.setColorAt(0, CheezyTheme::Recolor(CheeseBase));
        fill.setColorAt(1, CheezyTheme::Recolor(CheeseDeepBase));
        painter->fillRect(tile_rect, fill);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(tile_rect.height() / 4);
        painter->setFont(font);
        painter->setPen(CheezyTheme::Recolor(QColor(0x3A, 0x26, 0x00)));
        painter->drawText(tile_rect, Qt::AlignCenter, title.left(2).toUpper());
    }
    if (!selected) {
        // The others step back a little.
        painter->fillRect(tile_rect, QColor(10, 8, 4, hovered ? 25 : 80));
    }
    painter->setClipping(false);

    // Outline: thick cheese when selected, a soft cream line on hover.
    painter->setBrush(Qt::NoBrush);
    if (selected) {
        painter->setPen(QPen(CheezyTheme::Recolor(CheeseBase), std::max(3, tile / 70)));
    } else if (hovered) {
        painter->setPen(QPen(CheezyTheme::Recolor(QColor(0xFF, 0xF4, 0xDA, 170)), 2));
    } else {
        painter->setPen(QPen(CheezyTheme::Recolor(QColor(0xFF, 0xFF, 0xFF, 28)), 1));
    }
    painter->drawRoundedRect(QRectF(tile_rect), radius, radius);

    // The selected game's name, in a little pill under it.
    if (selected && UISettings::values.show_game_name.GetValue()) {
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::clamp(tile / 13, 13, 20));
        painter->setFont(font);
        const QFontMetrics metrics{font};
        const int max_width = cell.width() * 2;
        const QString shown = metrics.elidedText(title, Qt::ElideRight, max_width);
        const int text_width = metrics.horizontalAdvance(shown);
        const QRect pill{tile_rect.center().x() - text_width / 2 - 14, tile_rect.bottom() + 18,
                         text_width + 28, metrics.height() + 10};
        painter->setPen(Qt::NoPen);
        painter->setBrush(CheezyTheme::Recolor(QColor(0x2A, 0x22, 0x14, 230)));
        painter->drawRoundedRect(QRectF(pill), pill.height() / 2.0, pill.height() / 2.0);
        painter->setPen(CheezyTheme::Recolor(CreamBase));
        painter->drawText(pill, Qt::AlignCenter, shown);
    }
    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode

        // center everything in-line, such that the leftmost and rightmost cards
        // have ~ equal padding to the edge of the viewport
        // spacing between each card is larger, but equal to each other
        const int column = index.row() % m_columns;

        const int row_width = m_columns * cell_width;
        const int total_gap = row_width - cardMargin * 2 - m_columns * card_width;
        const int gap = (m_columns > 1) ? (total_gap / (m_columns - 1)) : 0;
        card_left =
            option.rect.left() - column * cell_width + cardMargin + column * (card_width + gap) + 4;

        // fill cell vertically
        card_top = option.rect.top() + cardMargin;
        card_height = option.rect.height() - cardMargin - 1;
    } else {
        // carousel mode: the tile (PaintHomeTile)
        const int tile = m_contentSize.width();
        return QRect(option.rect.center().x() - tile / 2 + HomeInset,
                     option.rect.top() + (option.rect.height() - tile) / 2 - 16, tile, tile);
        card_left = option.rect.left() + cardMargin + 4;

        // the delegate itself takes up the full height, but the card itself
        // gets centered
        const int content_height = m_contentSize.height() - cardMargin;
        const int cell_height = option.rect.height();

        card_height = std::min(content_height, cell_height - cardMargin * 2) - 1;
        card_top = option.rect.top() + (cell_height - card_height) / 2;
    }

    return QRect(card_left, card_top, card_width - cardMargin, card_height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, const int padding,
                       const int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}
