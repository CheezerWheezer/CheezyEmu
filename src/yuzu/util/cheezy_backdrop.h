// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QRandomGenerator>

/// CheezyEmu's moving backdrop for the home view and the loading screen: a warm dark base, the
/// picked game's picture blurred into an ambient glow, and cozy sunbeams with dust motes.
namespace CheezyBackdrop {

/// A blurred copy of `picture` covering `size` (cropped to its shape, never stretched): made
/// small, blurred there, then scaled up once. Rebuild it when the size changes.
inline QPixmap MakeAmbient(const QPixmap& picture, QSize size) {
    if (picture.isNull() || size.isEmpty()) {
        return {};
    }
    // Cover: crop the picture to the target's aspect ratio.
    const qreal target_aspect = qreal(size.width()) / size.height();
    QRect crop = picture.rect();
    if (qreal(crop.width()) / crop.height() > target_aspect) {
        const int w = int(crop.height() * target_aspect);
        crop = {(crop.width() - w) / 2, 0, w, crop.height()};
    } else {
        const int h = int(crop.width() / target_aspect);
        crop = {0, (crop.height() - h) / 2, crop.width(), h};
    }
    const QSize small{std::max(8, size.width() / 10), std::max(8, size.height() / 10)};
    QImage image = picture.copy(crop)
                       .scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .toImage()
                       .convertToFormat(QImage::Format_ARGB32);
    // Three box blur passes (close to a Gaussian), each across then down.
    const int w = image.width();
    const int h = image.height();
    constexpr int radius = 3;
    std::vector<QRgb> line(std::max(w, h));
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            auto* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, x - radius); k <= std::min(w - 1, x + radius); ++k) {
                    r += qRed(row[k]);
                    g += qGreen(row[k]);
                    b += qBlue(row[k]);
                    ++n;
                }
                line[x] = qRgb(r / n, g / n, b / n);
            }
            std::copy(line.begin(), line.begin() + w, row);
        }
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                int r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, y - radius); k <= std::min(h - 1, y + radius); ++k) {
                    const QRgb c = reinterpret_cast<const QRgb*>(image.constScanLine(k))[x];
                    r += qRed(c);
                    g += qGreen(c);
                    b += qBlue(c);
                    ++n;
                }
                line[y] = qRgb(r / n, g / n, b / n);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(image.scanLine(y))[x] = line[y];
            }
        }
    }
    return QPixmap::fromImage(
        image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

/// The moving parts: sunbeams slanting in from the top-left corner, swaying and breathing
/// slowly, and dust motes drifting lazily that light up as they float through a beam. Step it
/// every frame, then Paint.
class Scene {
public:
    void Step(qreal dt, const QRectF& area) {
        time += dt;
        if (area.isEmpty()) {
            return;
        }
        auto* const random = QRandomGenerator::global();
        if (motes.empty() || bounds != area) {
            bounds = area;
            motes.resize(std::clamp(int(area.width() * area.height() / 9000), 50, 140));
            for (Mote& mote : motes) {
                Spawn(mote, area, random);
            }
        }
        for (Mote& mote : motes) {
            // Lazy, wandering drift: each mote's heading turns slowly on its own.
            mote.heading += std::sin(time * mote.turn + mote.seed) * 0.6 * dt;
            const QPointF velocity{std::cos(mote.heading), std::sin(mote.heading) * 0.6 + 0.15};
            mote.pos += velocity * mote.speed * dt;
            // Wrap around to the other side, so the room never empties.
            if (mote.pos.x() < area.left() - 10) {
                mote.pos.setX(area.right() + 8);
            } else if (mote.pos.x() > area.right() + 10) {
                mote.pos.setX(area.left() - 8);
            }
            if (mote.pos.y() < area.top() - 10) {
                mote.pos.setY(area.bottom() + 8);
            } else if (mote.pos.y() > area.bottom() + 10) {
                mote.pos.setY(area.top() - 8);
            }
        }
    }

    void Paint(QPainter& p, const QRect& area, const QPixmap& ambient, qreal ambient_strength,
               const QPixmap& previous = {}, qreal previous_strength = 0.0) const {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        QLinearGradient base{area.topLeft(), area.bottomLeft()};
        base.setColorAt(0, QColor(0x24, 0x1B, 0x12));
        base.setColorAt(1, QColor(0x10, 0x0C, 0x08));
        p.fillRect(area, base);

        // The game's picture, blurred, at its own size (never stretched).
        const auto draw_ambient = [&](const QPixmap& pixmap, qreal strength) {
            if (pixmap.isNull() || strength <= 0.0) {
                return;
            }
            p.setOpacity(strength);
            p.drawPixmap(area.topLeft(), pixmap);
            p.setOpacity(1.0);
        };
        draw_ambient(previous, previous_strength);
        draw_ambient(ambient, ambient_strength);

        p.setCompositionMode(QPainter::CompositionMode_Screen);
        PaintSun(p, area);
        PaintMotes(p, area);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // A vignette: the far corner and the bottom fall into shade.
        const qreal unit = std::max(area.width(), area.height());
        QRadialGradient vignette{
            QPointF(area.left() + area.width() * 0.35, area.top() + area.height() * 0.3),
            unit * 0.95};
        vignette.setColorAt(0.45, QColor(0, 0, 0, 0));
        vignette.setColorAt(1, QColor(0, 0, 0, 170));
        p.setPen(Qt::NoPen);
        p.setBrush(vignette);
        p.drawRect(area);
        p.restore();
    }

private:
    struct Beam {
        qreal angle;     // radians from the source, pointing down and right
        qreal spread;    // half width, radians
        qreal intensity; // 0..1
    };

    struct Mote {
        QPointF pos;
        qreal heading = 0;
        qreal turn = 0;
        qreal seed = 0;
        qreal speed = 0;
        qreal size = 0;
    };

    static void Spawn(Mote& mote, const QRectF& area, QRandomGenerator* random) {
        const auto uniform = [random] { return random->generateDouble(); };
        mote.pos = {area.left() + uniform() * area.width(), area.top() + uniform() * area.height()};
        mote.heading = uniform() * 6.2832;
        mote.turn = 0.15 + uniform() * 0.35;
        mote.seed = uniform() * 100;
        mote.speed = 3 + uniform() * 9;
        mote.size = 0.9 + uniform() * 1.8;
    }

    static QPointF Source(const QRectF& area) {
        return {area.left() - area.width() * 0.08, area.top() - area.height() * 0.22};
    }

    /// The beams now: a slow sway and breathing, each at its own pace.
    std::array<Beam, 6> Beams() const {
        // angle (deg), spread (deg), sway speed, breathe speed
        static constexpr std::array<std::array<qreal, 4>, 6> shape{{
            {{30, 2.6, 0.050, 0.21}},
            {{38, 1.6, 0.070, 0.17}},
            {{45, 3.4, 0.040, 0.13}},
            {{53, 1.9, 0.060, 0.23}},
            {{61, 2.8, 0.035, 0.19}},
            {{70, 1.5, 0.055, 0.15}},
        }};
        constexpr qreal to_rad = 3.14159265 / 180;
        std::array<Beam, 6> beams;
        for (size_t i = 0; i < beams.size(); ++i) {
            const qreal deg = shape[i][0];
            const qreal spread = shape[i][1];
            const qreal sway = shape[i][2];
            const qreal breathe = shape[i][3];
            const qreal pulse = 0.5 + 0.5 * std::sin(time * breathe * 1.9 + i * 2.3);
            beams[i] = {(deg + 2.2 * std::sin(time * sway * 6.2832 + i * 1.7)) * to_rad,
                        spread * to_rad * (0.85 + 0.15 * std::sin(time * breathe * 4.1 + i)),
                        0.55 + 0.45 * pulse * pulse};
        }
        return beams;
    }

    void PaintSun(QPainter& p, const QRect& area) const {
        const QPointF source = Source(area);
        const qreal reach = std::hypot(area.width(), area.height()) * 1.15;
        p.setPen(Qt::NoPen);
        // The glow where the light comes in.
        QRadialGradient bloom{source, reach * 0.55};
        bloom.setColorAt(0, QColor(0xFF, 0xE2, 0x9A, 70));
        bloom.setColorAt(0.5, QColor(0xFF, 0xC8, 0x5A, 18));
        bloom.setColorAt(1, QColor(0xFF, 0xC8, 0x5A, 0));
        p.setBrush(bloom);
        p.drawRect(area);
        // Each beam: three wedges of shrinking width (soft edges), fading with distance.
        static constexpr std::array<std::array<qreal, 2>, 3> layers{
            {{{1.0, 0.30}}, {{0.6, 0.35}}, {{0.28, 0.45}}}};
        for (const Beam& beam : Beams()) {
            for (const auto& layer : layers) {
                const qreal half = beam.spread * layer[0];
                QPainterPath wedge;
                wedge.moveTo(source);
                wedge.lineTo(source + QPointF(std::cos(beam.angle - half),
                                              std::sin(beam.angle - half)) * reach);
                wedge.lineTo(source + QPointF(std::cos(beam.angle + half),
                                              std::sin(beam.angle + half)) * reach);
                wedge.closeSubpath();
                const int alpha = int(95 * beam.intensity * layer[1]);
                QRadialGradient fall{source, reach};
                fall.setColorAt(0.0, QColor(0xFF, 0xEC, 0xB8, alpha));
                fall.setColorAt(0.45, QColor(0xFF, 0xD8, 0x86, int(alpha * 0.55)));
                fall.setColorAt(0.85, QColor(0xFF, 0xC8, 0x5A, 0));
                p.setBrush(fall);
                p.drawPath(wedge);
            }
        }
    }

    void PaintMotes(QPainter& p, const QRect& area) const {
        const QPointF source = Source(area);
        const qreal reach = std::hypot(area.width(), area.height()) * 1.15;
        const auto beams = Beams();
        p.setPen(Qt::NoPen);
        for (const Mote& mote : motes) {
            // How much sunlight is on this mote: inside a beam, near its middle, not too far.
            const QPointF d = mote.pos - source;
            const qreal angle = std::atan2(d.y(), d.x());
            const qreal distance = std::hypot(d.x(), d.y()) / reach;
            qreal light = 0;
            for (const Beam& beam : beams) {
                const qreal across = std::abs(angle - beam.angle) / beam.spread;
                if (across < 1.2) {
                    const qreal edge = 1.0 - across / 1.2;
                    light += edge * edge * beam.intensity;
                }
            }
            light = std::min(1.0, light) * std::clamp(1.1 - distance, 0.0, 1.0);
            const qreal twinkle = 0.8 + 0.2 * std::sin(time * 2.3 + mote.seed);
            const int alpha = int((34 + 221 * light) * twinkle);
            const qreal r = mote.size * (1.0 + 1.6 * light);
            QRadialGradient glow{mote.pos, r * 2.4};
            glow.setColorAt(0, QColor(0xFF, 0xF4, 0xD6, alpha));
            glow.setColorAt(0.35, QColor(0xFF, 0xE2, 0x9A, alpha / 2));
            glow.setColorAt(1, QColor(0xFF, 0xD0, 0x70, 0));
            p.setBrush(glow);
            p.drawEllipse(mote.pos, r * 2.4, r * 2.4);
        }
    }

    std::vector<Mote> motes;
    QRectF bounds;
    qreal time = 0;
};

} // namespace CheezyBackdrop
