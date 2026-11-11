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
            {{28, 4.5, 0.050, 0.21}},
            {{36, 2.8, 0.070, 0.17}},
            {{44, 5.5, 0.040, 0.13}},
            {{52, 3.2, 0.060, 0.23}},
            {{60, 4.8, 0.035, 0.19}},
            {{69, 2.6, 0.055, 0.15}},
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

    /// How much sunlight reaches a point at `angle` (radians from the source) and `distance`
    /// (share of the beams' reach): soft bell-shaped beams with a faint halo, broken into finer
    /// shafts that drift and shimmer, fading with distance.
    qreal LightAt(const std::array<Beam, 6>& beams, qreal angle, qreal distance) const {
        if (distance >= 1.0) {
            return 0.0;
        }
        qreal light = 0;
        for (const Beam& beam : beams) {
            const qreal across = (angle - beam.angle) / beam.spread;
            const qreal core = std::exp(-across * across * 1.6);
            const qreal halo = std::exp(-across * across * 0.18) * 0.22;
            light += (core + halo) * beam.intensity;
        }
        // Finer shafts: light through leaves, slowly shifting.
        const qreal shafts = 0.72 + 0.16 * std::sin(angle * 97.0 + time * 0.37) +
                             0.12 * std::sin(angle * 211.0 - time * 0.23 + distance * 3.0);
        // Strongest a little way in from the source, then fading out.
        const qreal fall = std::pow(1.0 - distance, 1.6) * std::min(1.0, distance * 6.0 + 0.35);
        return std::min(1.0, light * shafts) * fall;
    }

    void PaintSun(QPainter& p, const QRect& area) const {
        const QPointF source = Source(area);
        const qreal reach = std::hypot(area.width(), area.height()) * 1.15;
        p.setPen(Qt::NoPen);
        // The glow where the light comes in.
        QRadialGradient bloom{source, reach * 0.6};
        bloom.setColorAt(0, QColor(0xFF, 0xE2, 0x9A, 64));
        bloom.setColorAt(0.5, QColor(0xFF, 0xC8, 0x5A, 16));
        bloom.setColorAt(1, QColor(0xFF, 0xC8, 0x5A, 0));
        p.setBrush(bloom);
        p.drawRect(area);

        // The beams, computed per pixel at a fifth of the size and scaled up smoothly: no edges.
        const int w = std::max(16, area.width() / 5);
        const int h = std::max(16, area.height() / 5);
        if (rays.width() != w || rays.height() != h) {
            rays = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        }
        const auto beams = Beams();
        const qreal sx = qreal(area.width()) / w;
        const qreal sy = qreal(area.height()) / h;
        for (int y = 0; y < h; ++y) {
            auto* const row = reinterpret_cast<QRgb*>(rays.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const qreal px = area.left() + (x + 0.5) * sx - source.x();
                const qreal py = area.top() + (y + 0.5) * sy - source.y();
                const qreal light =
                    LightAt(beams, std::atan2(py, px), std::hypot(px, py) / reach);
                const int a = std::clamp(int(light * 120), 0, 255);
                // Warm, a little whiter where it is brightest (premultiplied).
                row[x] = qRgba(a * 255 / 255, a * (220 + light * 30) / 255,
                               a * (150 + light * 70) / 255, a);
            }
        }
        p.drawImage(area, rays);
    }

    void PaintMotes(QPainter& p, const QRect& area) const {
        const QPointF source = Source(area);
        const qreal reach = std::hypot(area.width(), area.height()) * 1.15;
        const auto beams = Beams();
        p.setPen(Qt::NoPen);
        for (const Mote& mote : motes) {
            // Motes catch the light: bright inside a beam, barely there in the shade.
            const QPointF d = mote.pos - source;
            const qreal light =
                std::min(1.0, 1.5 * LightAt(beams, std::atan2(d.y(), d.x()),
                                            std::hypot(d.x(), d.y()) / reach));
            const qreal twinkle = 0.8 + 0.2 * std::sin(time * 2.3 + mote.seed);
            const int alpha = int((30 + 225 * light) * twinkle);
            const qreal r = mote.size * (1.0 + 1.6 * light);
            QRadialGradient glow{mote.pos, r * 2.4};
            glow.setColorAt(0, QColor(0xFF, 0xF4, 0xD6, alpha));
            glow.setColorAt(0.35, QColor(0xFF, 0xE2, 0x9A, alpha / 2));
            glow.setColorAt(1, QColor(0xFF, 0xD0, 0x70, 0));
            p.setBrush(glow);
            p.drawEllipse(mote.pos, r * 2.4, r * 2.4);
        }
    }

    mutable QImage rays; ///< PaintSun's light, at a fifth of the size

    std::vector<Mote> motes;
    QRectF bounds;
    qreal time = 0;
};

} // namespace CheezyBackdrop
