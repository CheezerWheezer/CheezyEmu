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
/// picked game's picture blurred into an ambient glow, silky ribbons of light flowing across, and
/// fireflies drifting on a gentle current with short fading trails.
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

/// The moving parts. Step it every frame, then Paint.
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
            motes.resize(std::clamp(int(area.width() * area.height() / 18000), 24, 70));
            for (Mote& mote : motes) {
                Spawn(mote, area, random, true);
            }
        }
        for (Mote& mote : motes) {
            // A smooth current: the direction turns slowly with place and time.
            const qreal angle = 1.6 * std::sin(mote.pos.x() * 0.0042 + time * 0.13) +
                                1.6 * std::cos(mote.pos.y() * 0.0051 - time * 0.11) +
                                0.8 * std::sin((mote.pos.x() + mote.pos.y()) * 0.0023 + time * 0.07) +
                                mote.bias;
            const QPointF velocity{std::cos(angle), std::sin(angle) - 0.25};
            mote.pos += velocity * mote.speed * dt;
            mote.age += dt;
            // The trail keeps a point every 70 ms (a short, soft streak), the head is current.
            mote.trail_timer += dt;
            if (mote.trail_timer >= 0.07) {
                mote.trail_timer = 0;
                for (int k = int(mote.trail.size()) - 1; k > 1; --k) {
                    mote.trail[k] = mote.trail[k - 1];
                }
                mote.trail[1] = mote.trail[0];
            }
            mote.trail[0] = mote.pos;
            if (mote.age > mote.life || !area.adjusted(-40, -40, 40, 40).contains(mote.pos)) {
                Spawn(mote, area, random, false);
            }
        }
    }

    void Paint(QPainter& p, const QRect& area, const QPixmap& ambient, qreal ambient_strength,
               const QPixmap& previous = {}, qreal previous_strength = 0.0) const {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        QLinearGradient base{area.topLeft(), area.bottomLeft()};
        base.setColorAt(0, QColor(0x22, 0x1A, 0x11));
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
        PaintRibbons(p, area);
        PaintMotes(p);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // A vignette: the edges fall away, the middle stays lit.
        const qreal unit = std::max(area.width(), area.height());
        QRadialGradient vignette{QPointF(area.center()), unit * 0.75};
        vignette.setColorAt(0.5, QColor(0, 0, 0, 0));
        vignette.setColorAt(1, QColor(0, 0, 0, 160));
        p.setPen(Qt::NoPen);
        p.setBrush(vignette);
        p.drawRect(area);
        p.restore();
    }

private:
    struct Mote {
        QPointF pos;
        std::array<QPointF, 10> trail;
        qreal speed = 0;
        qreal size = 0;
        qreal bias = 0;
        qreal age = 0;
        qreal life = 0;
        qreal hue = 0; // under 0.7: cheese, else cream
        qreal trail_timer = 0;
    };

    static void Spawn(Mote& mote, const QRectF& area, QRandomGenerator* random, bool anywhere) {
        const auto uniform = [random] { return random->generateDouble(); };
        mote.pos = {area.left() + uniform() * area.width(),
                    anywhere ? area.top() + uniform() * area.height()
                             : area.bottom() + 10 - uniform() * area.height() * 0.3};
        mote.trail.fill(mote.pos);
        mote.speed = 14 + uniform() * 26;
        mote.size = 1.4 + uniform() * 2.6;
        mote.bias = (uniform() - 0.5) * 1.2;
        mote.life = 8 + uniform() * 14;
        mote.age = anywhere ? uniform() * mote.life : 0;
        mote.hue = uniform();
    }

    void PaintRibbons(QPainter& p, const QRect& area) const {
        // Three silky bands of light, flowing and folding slowly.
        struct Ribbon {
            qreal y, amplitude, width, speed, frequency;
            int r, g, b, a;
        };
        static constexpr std::array<Ribbon, 3> ribbons{{
            {0.30, 0.10, 0.07, 0.17, 1.3, 0xFF, 0xC8, 0x3D, 34},
            {0.58, 0.13, 0.10, 0.11, 0.9, 0xFF, 0x96, 0x40, 26},
            {0.78, 0.08, 0.06, 0.23, 1.7, 0xFF, 0xE0, 0x9A, 22},
        }};
        constexpr int steps = 48;
        // Each ribbon is three bands of shrinking width on top of each other (added light), so
        // it is brightest in its middle and fades out softly at its edges.
        static constexpr std::array<std::pair<qreal, qreal>, 3> layers{
            {{1.0, 0.30}, {0.66, 0.35}, {0.33, 0.45}}};
        for (size_t i = 0; i < ribbons.size(); ++i) {
            const Ribbon& ribbon = ribbons[i];
            std::array<qreal, steps + 1> centres;
            std::array<qreal, steps + 1> halves;
            for (int k = 0; k <= steps; ++k) {
                const qreal u = qreal(k) / steps;
                const qreal wave =
                    std::sin(u * 6.2832 * ribbon.frequency + time * ribbon.speed * 2 + i * 2.1) *
                        ribbon.amplitude +
                    std::sin(u * 6.2832 * ribbon.frequency * 2.3 - time * ribbon.speed * 1.3 +
                             i) *
                        ribbon.amplitude * 0.35;
                centres[k] = area.top() + area.height() * (ribbon.y + wave);
                halves[k] = area.height() * ribbon.width *
                            (0.55 + 0.45 * std::sin(u * 9.0 + time * ribbon.speed * 3 + i));
            }
            for (const auto& [scale, strength] : layers) {
                QPainterPath band;
                for (int k = 0; k <= steps; ++k) {
                    const QPointF top{area.left() + area.width() * k / steps,
                                      centres[k] - halves[k] * scale};
                    if (k == 0) {
                        band.moveTo(top);
                    } else {
                        band.lineTo(top);
                    }
                }
                for (int k = steps; k >= 0; --k) {
                    band.lineTo(area.left() + area.width() * k / steps,
                                centres[k] + halves[k] * scale);
                }
                band.closeSubpath();
                const int alpha = int(ribbon.a * 2.2 * strength);
                const QColor colour{ribbon.r, ribbon.g, ribbon.b, alpha};
                const QColor clear{ribbon.r, ribbon.g, ribbon.b, 0};
                QLinearGradient fill{QPointF(area.left(), 0), QPointF(area.right(), 0)};
                fill.setColorAt(0, clear);
                fill.setColorAt(0.25 + 0.1 * std::sin(time * 0.2 + i), colour);
                fill.setColorAt(0.7 + 0.1 * std::cos(time * 0.17 + i), colour);
                fill.setColorAt(1, clear);
                p.setPen(Qt::NoPen);
                p.setBrush(fill);
                p.drawPath(band);
            }
        }
    }

    void PaintMotes(QPainter& p) const {
        for (const Mote& mote : motes) {
            // Fade in, glow, fade out; a little flicker.
            const qreal life_share = mote.age / mote.life;
            const qreal fade = std::sin(std::clamp(life_share, 0.0, 1.0) * 3.14159265);
            const qreal flicker = 0.75 + 0.25 * std::sin(time * 3.1 + mote.bias * 7);
            const int alpha = int(200 * fade * flicker);
            if (alpha <= 2) {
                continue;
            }
            const QColor colour =
                mote.hue < 0.7 ? QColor(0xFF, 0xC8, 0x3D) : QColor(0xFF, 0xEE, 0xC2);
            // The trail: thinner and fainter towards its end.
            for (size_t k = 1; k < mote.trail.size(); ++k) {
                const qreal share = 1.0 - qreal(k) / mote.trail.size();
                QColor c = colour;
                c.setAlpha(int(alpha * 0.35 * share));
                p.setPen(QPen(c, mote.size * share, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(mote.trail[k - 1], mote.trail[k]);
            }
            p.setPen(Qt::NoPen);
            QRadialGradient glow{mote.pos, mote.size * 4};
            glow.setColorAt(0, QColor(colour.red(), colour.green(), colour.blue(), alpha));
            glow.setColorAt(0.25, QColor(colour.red(), colour.green(), colour.blue(), alpha / 2));
            glow.setColorAt(1, QColor(colour.red(), colour.green(), colour.blue(), 0));
            p.setBrush(glow);
            p.drawEllipse(mote.pos, mote.size * 4, mote.size * 4);
        }
    }

    std::vector<Mote> motes;
    QRectF bounds;
    qreal time = 0;
};

} // namespace CheezyBackdrop
