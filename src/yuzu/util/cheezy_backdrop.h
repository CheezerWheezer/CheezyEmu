// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QRandomGenerator>

/// CheezyEmu's backdrop for the home view and the loading screen: a warm dark base, the picked
/// game's picture blurred into an ambient glow, soft light blobs drifting with `phase` (seconds),
/// a fine dot texture and a vignette.
namespace CheezyBackdrop {

/// A scattered, repeatable number in [0, 1) for element `i`, stream `k`.
inline qreal Scatter(int i, int k) {
    const qreal v = std::sin(i * 12.9898 + k * 78.233) * 43758.5453;
    return v - std::floor(v);
}

/// A blurred copy of `picture` for Paint's ambient glow (scaled down and back up, smoothly).
inline QPixmap MakeAmbient(const QPixmap& picture) {
    if (picture.isNull()) {
        return {};
    }
    const QPixmap tiny = picture.scaled(18, 18, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return tiny.scaled(360, 360, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

inline void Paint(QPainter& p, const QRect& area, qreal phase, const QPixmap& ambient = {},
                  qreal ambient_strength = 0.0, const QPixmap& previous = {},
                  qreal previous_strength = 0.0) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QLinearGradient base{area.topLeft(), area.bottomLeft()};
    base.setColorAt(0, QColor(0x24, 0x1B, 0x12));
    base.setColorAt(1, QColor(0x12, 0x0E, 0x0A));
    p.fillRect(area, base);

    // The game's picture, blurred, filling the screen.
    const auto draw_ambient = [&](const QPixmap& pixmap, qreal strength) {
        if (pixmap.isNull() || strength <= 0.0) {
            return;
        }
        p.setOpacity(strength);
        p.drawPixmap(area, pixmap);
        p.setOpacity(1.0);
    };
    draw_ambient(previous, previous_strength);
    draw_ambient(ambient, ambient_strength);

    // Soft light blobs, drifting slowly.
    struct Blob {
        qreal x, y, r, speed;
        QColor color;
    };
    const Blob blobs[] = {
        {0.22, 0.30, 0.55, 0.11, QColor(0xFF, 0xC8, 0x3D, 46)},
        {0.80, 0.25, 0.45, 0.08, QColor(0xFF, 0x8A, 0x3D, 34)},
        {0.62, 0.85, 0.50, 0.06, QColor(0xFF, 0x6B, 0x6B, 22)},
        {0.10, 0.90, 0.40, 0.09, QColor(0xFF, 0xD9, 0x66, 26)},
    };
    const qreal unit = std::max(area.width(), area.height());
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 4; ++i) {
        const Blob& blob = blobs[i];
        const QPointF centre{area.left() + area.width() * (blob.x + 0.06 * std::sin(phase * blob.speed * 6 + i)),
                             area.top() + area.height() * (blob.y + 0.05 * std::cos(phase * blob.speed * 5 + i * 2))};
        QRadialGradient glow{centre, blob.r * unit};
        glow.setColorAt(0, blob.color);
        QColor clear = blob.color;
        clear.setAlpha(0);
        glow.setColorAt(1, clear);
        p.setBrush(glow);
        p.drawRect(area);
    }

    // Melted cheese: a few big, slow blobs with wobbling outlines, wandering in loops (a lava
    // lamp of cheese). Each has soft holes and a glossy highlight; the big ones move slowest.
    static const qreal session_seed = [] {
        return static_cast<qreal>(QRandomGenerator::global()->bounded(1000)) / 7.0;
    }();
    const auto blob_outline = [&](QPointF centre, qreal radius, qreal t, qreal seed) {
        constexpr int points = 14;
        QPointF ring[points];
        for (int k = 0; k < points; ++k) {
            const qreal angle = 6.28318530718 * k / points;
            const qreal wobble = 1.0 + 0.10 * std::sin(3 * angle + t * 0.9 + seed) +
                                 0.06 * std::sin(5 * angle - t * 0.7 + seed * 2.3) +
                                 0.04 * std::sin(2 * angle + t * 0.5 + seed * 4.1);
            ring[k] = centre + QPointF(std::cos(angle), std::sin(angle)) * radius * wobble;
        }
        // A smooth closed curve through the points (Catmull-Rom as cubic Beziers).
        QPainterPath path;
        path.moveTo(ring[0]);
        for (int k = 0; k < points; ++k) {
            const QPointF p0 = ring[(k + points - 1) % points];
            const QPointF p1 = ring[k];
            const QPointF p2 = ring[(k + 1) % points];
            const QPointF p3 = ring[(k + 2) % points];
            path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
        }
        path.closeSubpath();
        return path;
    };
    for (int i = 0; i < 6; ++i) {
        const qreal seed = session_seed + i * 3.7;
        const qreal depth = 0.45 + 0.55 * Scatter(i, 7); // 1 = near
        const qreal radius = unit * (0.028 + 0.04 * depth);
        const qreal slow = 0.035 + 0.05 * (1.0 - depth);
        // Wandering: two loops of different speeds added together.
        // Each blob has its own zone (three across, two down), so they never pile up.
        const qreal bx = (i % 3 + 0.2 + 0.6 * Scatter(i + int(session_seed), 8)) / 3.0;
        const qreal by = (i / 3 + 0.2 + 0.6 * Scatter(i + int(session_seed), 9)) / 2.0;
        const QPointF centre{
            area.left() + area.width() * (bx + 0.10 * std::sin(phase * slow * 1.7 + seed) +
                                          0.05 * std::sin(phase * slow * 3.1 + seed * 1.3)),
            area.top() + area.height() * (by + 0.08 * std::cos(phase * slow * 1.3 + seed * 0.7) +
                                          0.04 * std::sin(phase * slow * 2.6 + seed * 2.1))};
        const qreal t = phase * (0.6 + 0.4 * depth) + seed;
        const QPainterPath shape = blob_outline(centre, radius, t, seed);
        const int alpha = static_cast<int>(18 + 26 * depth);

        // Light, not matter: a soft halo and a glowing melty body, added onto the background.
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        QRadialGradient halo{centre, radius * 2.2};
        halo.setColorAt(0, QColor(0xFF, 0xB8, 0x30, alpha));
        halo.setColorAt(1, QColor(0xFF, 0xB8, 0x30, 0));
        p.setBrush(halo);
        p.drawEllipse(centre, radius * 2.2, radius * 2.2);
        QRadialGradient body{centre, radius * 1.15};
        body.setColorAt(0, QColor(0xFF, 0xE6, 0x9A, alpha + 30));
        body.setColorAt(0.6, QColor(0xFF, 0xC4, 0x3A, alpha + 10));
        body.setColorAt(1, QColor(0xFF, 0xA8, 0x20, 0));
        p.setBrush(body);
        p.drawPath(shape);
        p.restore();
    }

    // Twinkles: soft four-point sparkles fading on and off.
    for (int i = 0; i < 4; ++i) {
        const qreal sx = 0.05 + 0.9 * Scatter(i, 5);
        const qreal sy = 0.05 + 0.9 * Scatter(i, 6);
        const qreal glow =
            std::pow(std::max(0.0, std::sin(phase * (0.21 + 0.07 * i) + i * 1.7)), 8);
        if (glow <= 0.02) {
            continue;
        }
        const QPointF c{area.left() + area.width() * sx, area.top() + area.height() * sy};
        const qreal r = (5.0 + 3.0 * std::fmod(i * 0.31, 1.0)) * (0.7 + 0.3 * glow);
        QPainterPath star;
        star.moveTo(c + QPointF(0, -r));
        star.quadTo(c, c + QPointF(r, 0));
        star.quadTo(c, c + QPointF(0, r));
        star.quadTo(c, c + QPointF(-r, 0));
        star.quadTo(c, c + QPointF(0, -r));
        p.setBrush(QColor(0xFF, 0xEC, 0xB0, static_cast<int>(200 * glow)));
        p.drawPath(star);
        QRadialGradient halo{c, r * 2.2};
        halo.setColorAt(0, QColor(0xFF, 0xE0, 0x8A, static_cast<int>(60 * glow)));
        halo.setColorAt(1, QColor(0xFF, 0xE0, 0x8A, 0));
        p.setBrush(halo);
        p.drawEllipse(c, r * 2.2, r * 2.2);
    }

    // A fine dot texture (one tile, drawn once).
    static const QPixmap dots = [] {
        constexpr int step = 22;
        QPixmap tile(step * 2, step * 2);
        tile.fill(Qt::transparent);
        QPainter t{&tile};
        t.setRenderHint(QPainter::Antialiasing);
        t.setPen(Qt::NoPen);
        t.setBrush(QColor(0xFF, 0xF0, 0xD0, 12));
        t.drawEllipse(QPointF(step / 2.0, step / 2.0), 1.1, 1.1);
        t.drawEllipse(QPointF(step * 1.5, step * 1.5), 1.1, 1.1);
        return tile;
    }();
    p.drawTiledPixmap(area, dots);

    // A vignette: the edges fall off, the middle stays lit.
    QRadialGradient vignette{QPointF(area.center()), unit * 0.75};
    vignette.setColorAt(0.55, QColor(0, 0, 0, 0));
    vignette.setColorAt(1, QColor(0, 0, 0, 150));
    p.setBrush(vignette);
    p.drawRect(area);
    p.restore();
}

} // namespace CheezyBackdrop
