// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include <QColor>
#include <QRegularExpression>
#include <QString>

#include "common/common_types.h"

/// The Customize menu's choices for the CheezyEmu theme: an accent colour, a background colour
/// and a button shape. Colours are not repainted one by one: the theme's warm accent colours
/// (cheese yellow to amber, text included) take the accent's hue, saturation and brightness
/// relative to the default cheese yellow, and its warm dark charcoals the background's relative
/// to the default charcoal. The defaults change nothing. The stylesheet goes through Transform
/// when it loads; the parts CheezyEmu draws itself (glass, capsules, the home screen's light)
/// through Recolor and Radius, and redraw when Generation changes.
namespace CheezyTheme {

/// The theme's own accent and background, which map to themselves.
inline const QColor DefaultAccent{0xFF, 0xC8, 0x3D};
inline const QColor DefaultBackground{0x24, 0x1B, 0x12};

enum class Shape : u32 { Pill = 0, Soft = 1, Square = 2 };

namespace Detail {
inline QColor accent = DefaultAccent;
inline QColor background = DefaultBackground;
inline Shape shape = Shape::Pill;
inline std::atomic<u32> generation{0};
} // namespace Detail

/// Takes the choices (Customize menu, settings at startup).
inline void Configure(const QColor& accent, const QColor& background, Shape shape) {
    Detail::accent = accent.isValid() ? accent : DefaultAccent;
    Detail::background = background.isValid() ? background : DefaultBackground;
    Detail::shape = shape;
    Detail::generation.fetch_add(1, std::memory_order_relaxed);
}

/// Changes whenever the choices do: cached drawings compare it.
[[nodiscard]] inline u32 Generation() {
    return Detail::generation.load(std::memory_order_relaxed);
}

[[nodiscard]] inline bool IsDefault() {
    return Detail::accent == DefaultAccent && Detail::background == DefaultBackground;
}

/// A colour of the theme as the choices have it.
[[nodiscard]] inline QColor Recolor(const QColor& color) {
    if (IsDefault()) {
        return color;
    }
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
    float alpha = 0.0f;
    color.getHsvF(&hue, &saturation, &value, &alpha);
    const float degrees = hue < 0.0f ? -1.0f : hue * 360.0f;
    const bool warm = degrees >= 12.0f && degrees <= 68.0f;
    const auto shifted = [&](const QColor& to, const QColor& from) {
        float to_h = 0, to_s = 0, to_v = 0, from_h = 0, from_s = 0, from_v = 0;
        to.getHsvF(&to_h, &to_s, &to_v);
        from.getHsvF(&from_h, &from_s, &from_v);
        if (to_h < 0.0f) {
            to_h = from_h; // a grey: keep the hue, it loses its saturation below
        }
        float new_h = std::fmod(to_h + (hue - from_h) + 1.0f, 1.0f);
        const float new_s = std::clamp(saturation * (to_s / std::max(from_s, 0.01f)), 0.0f, 1.0f);
        const float new_v = std::clamp(value * (to_v / std::max(from_v, 0.01f)), 0.0f, 1.0f);
        return QColor::fromHsvF(new_h, new_s, new_v, alpha);
    };
    if (warm && saturation >= 0.18f && value >= 0.35f) {
        return shifted(Detail::accent, DefaultAccent);
    }
    if (warm && saturation >= 0.08f && value < 0.35f) {
        return shifted(Detail::background, DefaultBackground);
    }
    return color;
}

/// A corner radius as the button shape has it.
[[nodiscard]] inline qreal Radius(qreal radius) {
    switch (Detail::shape) {
    case Shape::Soft:
        return std::min(radius, qreal{6});
    case Shape::Square:
        return std::min(radius, qreal{2});
    case Shape::Pill:
        break;
    }
    return radius;
}

/// The CheezyEmu stylesheet with the choices applied: every #rrggbb, rgb() and rgba() colour
/// through Recolor, every border radius through Radius.
[[nodiscard]] inline QString Transform(const QString& stylesheet) {
    if (IsDefault() && Detail::shape == Shape::Pill) {
        return stylesheet;
    }
    QString result;
    result.reserve(stylesheet.size());
    static const QRegularExpression pattern{
        QStringLiteral(R"(#([0-9A-Fa-f]{6})\b|rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([0-9.]+)\s*)?\)|((?:border(?:-top-left|-top-right|-bottom-left|-bottom-right)?-radius)\s*:\s*)(\d+)px)")};
    qsizetype last = 0;
    auto it = pattern.globalMatch(stylesheet);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result.append(QStringView{stylesheet}.mid(last, match.capturedStart() - last));
        last = match.capturedEnd();
        if (!match.captured(1).isEmpty()) {
            const QColor color = Recolor(QColor(QLatin1Char('#') + match.captured(1)));
            result.append(color.name(QColor::HexRgb));
        } else if (!match.captured(2).isEmpty()) {
            const bool has_alpha = !match.captured(5).isEmpty();
            const QColor color = Recolor(QColor(match.captured(2).toInt(),
                                                match.captured(3).toInt(),
                                                match.captured(4).toInt()));
            if (has_alpha) {
                result.append(QStringLiteral("rgba(%1, %2, %3, %4)")
                                  .arg(color.red())
                                  .arg(color.green())
                                  .arg(color.blue())
                                  .arg(match.captured(5)));
            } else {
                result.append(QStringLiteral("rgb(%1, %2, %3)")
                                  .arg(color.red())
                                  .arg(color.green())
                                  .arg(color.blue()));
            }
        } else {
            const qreal radius = Radius(match.captured(7).toDouble());
            result.append(match.captured(6));
            result.append(QStringLiteral("%1px").arg(qRound(radius)));
        }
    }
    result.append(QStringView{stylesheet}.mid(last));
    return result;
}

} // namespace CheezyTheme
