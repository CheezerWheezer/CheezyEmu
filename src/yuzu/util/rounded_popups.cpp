// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/util/rounded_popups.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QPushButton>
#include <QTabBar>
#include <QEvent>
#include <QHash>
#include <QImage>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QMenu>
#include <QObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QStyle>
#include <QToolButton>
#include <QWidget>

#include "qt_common/config/uisettings.h"
#include "yuzu/util/cheezy_theme.h"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

/// Rounded popups and honey glass.
///
/// The theme rounds menus, dropdown lists and tooltips, but each is its own window, which stayed
/// a square box behind the rounded shape: before such a window first shows, it is made frameless
/// and see-through, so only the rounded shape is drawn.
///
/// Configure, a game's Properties, menus and dropdown lists are honey glass, drawn by CheezyEmu
/// itself (the same on Windows, Linux and macOS): when one opens, the window under it is
/// photographed, blurred and made a little more vivid; the glass shows the part of that photo
/// that is behind it (following it when it moves), tinted honey, with a sheen across the top,
/// a fine frost and a bright rim. The theme's [glass="true"] rules then put lighter glass cards,
/// pills and fields on top. CHEEZYEMU_GLASS=0 turns the glass off.
namespace RoundedPopups {

[[nodiscard]] bool GlassSupported() {
    static const bool supported = [] {
        const char* const value = std::getenv("CHEEZYEMU_GLASS");
        return value == nullptr || value[0] != '0';
    }();
    return supported;
}

namespace {

/// Separable box blur, `passes` times (three is close to a Gaussian).
void BoxBlur(QImage& image, int radius, int passes) {
    const int w = image.width();
    const int h = image.height();
    std::vector<QRgb> line(std::max(w, h));
    const auto blur_line = [&](auto at, int count) {
        for (int i = 0; i < count; ++i) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int k = std::max(0, i - radius); k <= std::min(count - 1, i + radius); ++k) {
                const QRgb c = at(k);
                r += qRed(c);
                g += qGreen(c);
                b += qBlue(c);
                ++n;
            }
            line[i] = qRgb(r / n, g / n, b / n);
        }
    };
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < h; ++y) {
            auto* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
            blur_line([row](int k) { return row[k]; }, w);
            std::copy(line.begin(), line.begin() + w, row);
        }
        for (int x = 0; x < w; ++x) {
            blur_line(
                [&image, x](int k) {
                    return reinterpret_cast<const QRgb*>(image.constScanLine(k))[x];
                },
                h);
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(image.scanLine(y))[x] = line[y];
            }
        }
    }
}

/// What is behind a glass window: `source` (the window under it) photographed, blurred lightly
/// at a quarter of its size (shapes behind stay recognisable) and made more vivid. Kept that small; the glass
/// scales its part up in steps.
QImage BlurredPhoto(QWidget* source) {
    const QPixmap photo = source->grab();
    if (photo.isNull()) {
        return {};
    }
    const QSize full = source->size();
    QImage shrunk = photo.toImage()
                       .scaled(std::max(8, full.width() / 4), std::max(8, full.height() / 4),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGB32);
    BoxBlur(shrunk, 3, 3);
    for (int y = 0; y < shrunk.height(); ++y) {
        auto* const row = reinterpret_cast<QRgb*>(shrunk.scanLine(y));
        for (int x = 0; x < shrunk.width(); ++x) {
            const int r = qRed(row[x]);
            const int g = qGreen(row[x]);
            const int b = qBlue(row[x]);
            const int gray = (r * 77 + g * 151 + b * 28) >> 8;
            const auto vivid = [gray](int c) {
                return std::clamp(gray + (c - gray) * 3 / 2 + 6, 0, 255);
            };
            row[x] = qRgb(vivid(r), vivid(g), vivid(b));
        }
    }
    return shrunk;
}

/// The part of `behind` (the small blurred photo, each pixel `step` window pixels) under a
/// window `size` big at `offset` from the photo's corner: sampled smoothly at any offset (so it
/// slides, not jumps, as the window moves), the photo's edges carried on past its border (no
/// seam), and scaled up in steps of two (soft, not stretched). Within `lens` window pixels of
/// the glass's edges the view bends like the rim of a lens: what is further in is drawn
/// stretched out to the edge.
QImage BehindPart(const QImage& behind, QSizeF step, QPointF offset, QSize size, qreal lens,
                  QSizeF& part_step) {
    const int w = int(std::ceil(size.width() / step.width())) + 2;
    const int h = int(std::ceil(size.height() / step.height())) + 2;
    QImage part(w, h, QImage::Format_RGB32);
    const double ox = offset.x() / step.width();
    const double oy = offset.y() / step.height();
    const int bw = behind.width();
    const int bh = behind.height();
    // The glass's size and the lens band, in pixels of `behind`.
    const double gw = size.width() / step.width();
    const double gh = size.height() / step.height();
    const double band_x = lens / step.width();
    const double band_y = lens / step.height();
    const auto bend = [](double from_start, double from_end, double band) {
        double shift = 0.0;
        if (band > 0 && from_start < band) {
            shift += (band - from_start) * (band - from_start) / band * 0.85;
        }
        if (band > 0 && from_end < band) {
            shift -= (band - from_end) * (band - from_end) / band * 0.85;
        }
        return shift;
    };
    for (int y = 0; y < h; ++y) {
        const double gy = y + 0.5;
        const double v = std::clamp(oy + y + bend(gy, gh - gy, band_y), 0.0, double(bh - 1));
        const int y0 = int(v);
        const int y1 = std::min(y0 + 1, bh - 1);
        const double fy = v - y0;
        const auto* const row0 = reinterpret_cast<const QRgb*>(behind.constScanLine(y0));
        const auto* const row1 = reinterpret_cast<const QRgb*>(behind.constScanLine(y1));
        auto* const out = reinterpret_cast<QRgb*>(part.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double gx = x + 0.5;
            const double u =
                std::clamp(ox + x + bend(gx, gw - gx, band_x), 0.0, double(bw - 1));
            const int x0 = int(u);
            const int x1 = std::min(x0 + 1, bw - 1);
            const double fx = u - x0;
            const auto mix = [&](auto channel) {
                const double top = channel(row0[x0]) * (1 - fx) + channel(row0[x1]) * fx;
                const double bottom = channel(row1[x0]) * (1 - fx) + channel(row1[x1]) * fx;
                return int(top * (1 - fy) + bottom * fy + 0.5);
            };
            out[x] = qRgb(mix([](QRgb c) { return qRed(c); }),
                          mix([](QRgb c) { return qGreen(c); }),
                          mix([](QRgb c) { return qBlue(c); }));
        }
    }
    QImage up = part;
    while (up.width() * 2 < w * step.width()) {
        up = up.scaled(up.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    part_step = QSizeF(w * step.width(), h * step.height());
    return up;
}

/// A fine frost: a tile of faint grain (it also hides banding in the soft gradients).
const QImage& Frost() {
    static const QImage tile = [] {
        QImage image(96, 96, QImage::Format_ARGB32_Premultiplied);
        QRandomGenerator random{0xC4EE5E};
        for (int y = 0; y < image.height(); ++y) {
            auto* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int a = int(random.bounded(10));
                row[x] = qRgba(a, a, a * 9 / 10, a); // premultiplied warm white
            }
        }
        return image;
    }();
    return tile;
}

/// How a glass control looks: clear or honey (picked), resting, under the pointer or pressed.
enum class Look { Clear, Hover, Pressed, Honey, HoneyHover, HoneyPressed };

/// `image` at (x, y), smoothly between pixels, held at its edges.
QRgb SampleAt(const QImage& image, double x, double y) {
    const int w = image.width();
    const int h = image.height();
    x = std::clamp(x, 0.0, w - 1.001);
    y = std::clamp(y, 0.0, h - 1.001);
    const int x0 = int(x);
    const int y0 = int(y);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const auto* const r0 = reinterpret_cast<const QRgb*>(image.constScanLine(y0));
    const auto* const r1 = reinterpret_cast<const QRgb*>(image.constScanLine(y1));
    const auto mix = [&](auto channel) {
        const double top = channel(r0[x0]) * (1 - fx) + channel(r0[x1]) * fx;
        const double bottom = channel(r1[x0]) * (1 - fx) + channel(r1[x1]) * fx;
        return int(top * (1 - fy) + bottom * fy + 0.5);
    };
    return qRgb(mix([](QRgb c) { return qRed(c); }), mix([](QRgb c) { return qGreen(c); }),
                mix([](QRgb c) { return qBlue(c); }));
}

/// A liquid glass capsule over `backdrop` (the glass window's own picture, in device pixels) at
/// `where`: a clear lens that magnifies what is under it and bends it strongly toward the rim,
/// a faint body (honey when picked), a glow that grows toward the rim, a soft gloss on the upper
/// half, and a fine specular rim lit from the top left (and catching light again at the bottom
/// right), darker along the sides between, so it reads as a thick piece of glass.
QImage Droplet(const QImage& backdrop, QRectF where, qreal radius, Look look, qreal dpr) {
    const bool honey = look == Look::Honey || look == Look::HoneyHover || look == Look::HoneyPressed;
    const bool hover = look == Look::Hover || look == Look::HoneyHover;
    const bool pressed = look == Look::Pressed || look == Look::HoneyPressed;
    const QColor body_color = honey ? CheezyTheme::Recolor(QColor(255, 196, 46))
                                    : CheezyTheme::Recolor(QColor(255, 244, 222));
    const double body_r = body_color.red();
    const double body_g = body_color.green();
    const double body_b = body_color.blue();
    const double body = honey ? (hover ? 0.70 : 0.62) : (hover ? 0.15 : 0.07);
    const double light = hover ? 1.25 : (pressed ? 0.7 : 1.0);
    const double zoom = pressed ? 0.85 : 0.92;
    const int w = std::max(1, int(std::lround(where.width())));
    const int h = std::max(1, int(std::lround(where.height())));
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const double hx = w / 2.0 - dpr;
    const double hy = h / 2.0 - dpr;
    if (hx <= 2 || hy <= 2) {
        return out;
    }
    const double r = std::min({radius, hx, hy});
    const double band = std::min(std::min(hx, hy), 18.0 * dpr);
    const double rim = 1.7 * dpr;
    constexpr double lx = -0.55;
    constexpr double ly = -0.835;
    const double cx = where.center().x();
    const double cy = where.center().y();
    for (int y = 0; y < h; ++y) {
        auto* const row = reinterpret_cast<QRgb*>(out.scanLine(y));
        const double py = y + 0.5 - h / 2.0;
        for (int x = 0; x < w; ++x) {
            const double px = x + 0.5 - w / 2.0;
            // Distance to the capsule's edge (negative inside) and the way out from it.
            const double qx = std::abs(px) - (hx - r);
            const double qy = std::abs(py) - (hy - r);
            const double ox = std::max(qx, 0.0);
            const double oy = std::max(qy, 0.0);
            const double out_len = std::hypot(ox, oy);
            const double d = out_len + std::min(std::max(qx, qy), 0.0) - r;
            const double cover = std::clamp(0.5 - d, 0.0, 1.0);
            if (cover <= 0.0) {
                continue;
            }
            double nx = 0.0;
            double ny = 0.0;
            if (qx > 0 && qy > 0 && out_len > 0) {
                nx = ox / out_len;
                ny = oy / out_len;
            } else if (qx > qy) {
                nx = 1.0;
            } else {
                ny = 1.0;
            }
            nx = px < 0 ? -nx : nx;
            ny = py < 0 ? -ny : ny;
            const double e = std::max(0.0, -d);
            const double t = std::clamp(1.0 - e / band, 0.0, 1.0);
            // The lens: magnified in the middle, pulled in strongly toward the rim.
            const double pull = band * 0.85 * t * t * t;
            const QRgb seen = SampleAt(backdrop, cx + px * zoom - nx * pull,
                                       cy + py * zoom - ny * pull);
            double cr = qRed(seen);
            double cg = qGreen(seen);
            double cb = qBlue(seen);
            cr += (body_r - cr) * body;
            cg += (body_g - cg) * body;
            cb += (body_b - cb) * body;
            // Glass catches light: a little overall, more toward the rim.
            const double glow = (honey ? 0.03 : 0.06) + (honey ? 0.18 : 0.30) * t * t * light;
            const double gloss =
                py < 0 ? (honey ? 0.14 : 0.10) * light * std::pow(std::min(1.0, -py / hy), 1.6)
                       : 0.0;
            const double s = std::clamp(1.0 - e / rim, 0.0, 1.0);
            const double facing = nx * lx + ny * ly;
            const double spec = s * light *
                                (std::pow(std::max(0.0, facing), 2.0) +
                                 0.55 * std::pow(std::max(0.0, -facing), 2.0));
            const double white = std::min(1.0, glow + gloss + spec);
            cr += (255 - cr) * white;
            cg += (252 - cg) * white;
            cb += (244 - cb) * white;
            const double dark = s * 0.30 * (1.0 - std::min(1.0, std::abs(facing) * 1.4));
            cr *= 1.0 - dark;
            cg *= 1.0 - dark;
            cb *= 1.0 - dark;
            const int a = int(255 * cover + 0.5);
            row[x] = qRgba(int(cr * cover + 0.5), int(cg * cover + 0.5), int(cb * cover + 0.5), a);
        }
    }
    return out;
}

/// One glass window's photo of what is behind it and its finished background.
struct Glass {
    QImage behind;         ///< The blurred photo of `source`, small
    QSizeF step;           ///< Window pixels per pixel of `behind`
    QPoint source_origin;  ///< Where `source`'s top-left was on screen when photographed
    QPixmap composed;      ///< The finished glass for `composed_at` and the window's size
    QImage composed_image; ///< `composed`, to read from (the glass controls refract it)
    QPoint composed_at;
    u32 composed_generation = 0; ///< CheezyTheme::Generation it was drawn for
};

class Filter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* const widget = qobject_cast<QWidget*>(watched);
        if (widget == nullptr) {
            return QObject::eventFilter(watched, event);
        }
        switch (event->type()) {
        case QEvent::Polish: {
            const bool popup = IsPopup(widget);
            const bool wants_glass =
                GlassSupported() && UISettings::values.theme == "cheezy" &&
                (IsGlassDialog(widget) || IsGlassPopup(widget));
            if (popup) {
                widget->setAttribute(Qt::WA_TranslucentBackground, true);
                widget->setWindowFlag(Qt::FramelessWindowHint, true);
                widget->setWindowFlag(Qt::NoDropShadowWindowHint, true);
            }
            if (wants_glass && !widget->property("glass").toBool()) {
                widget->setProperty("glass", true);
                widget->style()->unpolish(widget);
                widget->style()->polish(widget);
                // Parts already styled (the dialog's buttons are made and sized early) take the
                // glass rules too.
                for (QWidget* const child : widget->findChildren<QWidget*>()) {
                    if (child->testAttribute(Qt::WA_WState_Polished)) {
                        child->style()->unpolish(child);
                        child->style()->polish(child);
                    }
                }
            }
            break;
        }
        case QEvent::Show:
            if (widget->isWindow() && widget->property("glass").toBool()) {
                Photograph(widget);
                DarkFrame(widget);
            }
            break;
        case QEvent::Move:
            // A moved window keeps its old picture; the glass must show what is now behind.
            if (widget->isWindow() && glass.contains(widget)) {
                widget->update();
            }
            break;
        case QEvent::Paint:
            if (PaintTabScroller(widget)) {
                return true;
            }
            // Painted before the window's own painting, so everything else draws on the glass.
            if (widget->isWindow() && glass.contains(widget)) {
                Paint(widget, static_cast<QPaintEvent*>(event));
                if (auto* const menu = qobject_cast<QMenu*>(widget)) {
                    if (QAction* const active = menu->activeAction();
                        active != nullptr && active->isEnabled() && !active->isSeparator()) {
                        const QRect item = menu->actionGeometry(active).adjusted(4, 1, -4, -1);
                        Draw(widget, 0, item, std::min(12, item.height() / 2), Look::Honey);
                    }
                }
            } else if (GlassSupported() && !widget->isWindow()) {
                PaintControl(widget);
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static bool IsPopup(const QWidget* widget) {
        if (qobject_cast<const QMenu*>(widget) != nullptr) {
            return true;
        }
        const char* const name = widget->metaObject()->className();
        // A combo box's dropdown list and the tooltip label (Qt-internal classes).
        return qstrcmp(name, "QComboBoxPrivateContainer") == 0 ||
               qstrcmp(name, "QTipLabel") == 0;
    }

    static bool IsGlassPopup(const QWidget* widget) {
        return qobject_cast<const QMenu*>(widget) != nullptr ||
               qstrcmp(widget->metaObject()->className(), "QComboBoxPrivateContainer") == 0;
    }

    static bool IsGlassDialog(const QWidget* widget) {
        const char* const name = widget->metaObject()->className();
        return qstrcmp(name, "ConfigureDialog") == 0 || qstrcmp(name, "ConfigurePerGame") == 0;
    }

    /// A tab bar's scroll button (the arrows when the tabs do not fit): drawn here, a capsule
    /// (liquid glass in a glass window) with a chevron. The theme's arrow pictures came from a
    /// theme that is not bundled, so the arrows were invisible.
    bool PaintTabScroller(QWidget* widget) {
        auto* const button = qobject_cast<QToolButton*>(widget);
        if (button == nullptr || qobject_cast<QTabBar*>(widget->parentWidget()) == nullptr ||
            (button->arrowType() != Qt::LeftArrow && button->arrowType() != Qt::RightArrow)) {
            return false;
        }
        const QRect area = button->rect().adjusted(1, 2, -1, -2);
        const bool hover = button->underMouse() && button->isEnabled();
        QWidget* const window = widget->window();
        if (glass.contains(window) && !glass[window].composed_image.isNull()) {
            Draw(widget, 0, area, area.height() / 2,
                 button->isDown() ? Look::Pressed : hover ? Look::Hover : Look::Clear);
        } else {
            QPainter painter{widget};
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(CheezyTheme::Recolor(QColor(0xFF, 0xE2, 0x96, 110)), 1.0));
            painter.setBrush(CheezyTheme::Recolor(QColor(0xFF, 0xD6, 0x78, hover ? 80 : 40)));
            const QRectF shape = QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5);
            const qreal radius = std::min(shape.width(), shape.height()) / 2;
            painter.drawRoundedRect(shape, radius, radius);
        }
        QPainter painter{widget};
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen{button->isEnabled() ? CheezyTheme::Recolor(QColor(0xFF, 0xF1, 0xC8)) : CheezyTheme::Recolor(QColor(0xFF, 0xF1, 0xC8, 80)),
                 2.0};
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        const QPointF centre = QRectF(area).center();
        const qreal size = std::max(3.0, std::min(area.width(), area.height()) * 0.2);
        const qreal tip = button->arrowType() == Qt::LeftArrow ? -size * 0.55 : size * 0.55;
        const QPointF chevron[3] = {QPointF(centre.x() - tip, centre.y() - size),
                                    QPointF(centre.x() + tip, centre.y()),
                                    QPointF(centre.x() - tip, centre.y() + size)};
        painter.drawPolyline(chevron, 3);
        return true;
    }

    /// A glass control in a glass window, drawn as a liquid glass capsule before it paints its
    /// text on it (the theme leaves these controls' own backgrounds clear).
    void PaintControl(QWidget* widget) {
        QWidget* const window = widget->window();
        if (!glass.contains(window)) {
            return;
        }
        const auto look = [](bool honey, bool hover, bool pressed) {
            if (honey) {
                return pressed ? Look::HoneyPressed : hover ? Look::HoneyHover : Look::Honey;
            }
            return pressed ? Look::Pressed : hover ? Look::Hover : Look::Clear;
        };
        if (auto* const button = qobject_cast<QPushButton*>(widget)) {
            const QRect area = button->rect();
            Draw(widget, 0, area, area.height() / 2,
                 look(button->isDefault() || button->isChecked(), button->underMouse(),
                      button->isDown()));
        } else if (auto* const combo = qobject_cast<QComboBox*>(widget)) {
            const QRect area = combo->rect();
            Draw(widget, 0, area, area.height() / 2,
                 look(false, combo->underMouse() || combo->view()->isVisible(), false));
        } else if (auto* const tabs = qobject_cast<QTabBar*>(widget)) {
            const int hovered =
                tabs->underMouse() ? tabs->tabAt(tabs->mapFromGlobal(QCursor::pos())) : -1;
            for (int index = 0; index < tabs->count(); ++index) {
                const QRect area = tabs->tabRect(index).adjusted(0, 0, -6, 0);
                Draw(widget, index, area, area.height() / 2,
                     look(index == tabs->currentIndex(), index == hovered, false));
            }
        } else if (widget->objectName() == QStringLiteral("qt_scrollarea_viewport")) {
            auto* const view = qobject_cast<QAbstractItemView*>(widget->parentWidget());
            const bool popup = IsGlassPopup(window);
            if (view == nullptr || view->selectionModel() == nullptr ||
                (!popup && view->objectName() != QStringLiteral("selectorList"))) {
                return;
            }
            const QModelIndex hovered =
                widget->underMouse() ? view->indexAt(widget->mapFromGlobal(QCursor::pos()))
                                     : QModelIndex{};
            const auto item_area = [view, popup](const QModelIndex& index) {
                return view->visualRect(index).adjusted(popup ? 4 : 0, popup ? 1 : 2,
                                                        popup ? -4 : 0, popup ? -1 : -2);
            };
            // In a dropdown list the item under the pointer is the picked one.
            const QModelIndexList picked = popup && hovered.isValid()
                                               ? QModelIndexList{hovered}
                                               : view->selectionModel()->selectedIndexes();
            for (const QModelIndex& index : picked) {
                const QRect area = item_area(index);
                Draw(widget, index.row() + 1, area, std::min(15, area.height() / 2),
                     look(true, index == hovered && !popup, false));
            }
            if (hovered.isValid() && !picked.contains(hovered)) {
                const QRect area = item_area(hovered);
                Draw(widget, -1 - hovered.row(), area, std::min(15, area.height() / 2),
                     Look::Hover);
            }
        }
    }

    /// One capsule at `area` (in `widget`), made once per look, place and window picture.
    void Draw(QWidget* widget, int index, QRect area, int radius, Look look) {
        if (area.width() < 6 || area.height() < 6) {
            return;
        }
        QWidget* const window = widget->window();
        const Glass& state = glass[window];
        if (state.composed_image.isNull()) {
            return;
        }
        const qreal dpr = state.composed.devicePixelRatio();
        const QRect in_window = area.translated(widget->mapTo(window, QPoint{}));
        const QString key = QStringLiteral("%1/%2/%3/%4,%5,%6,%7/%8/%9")
                                .arg(quintptr(widget))
                                .arg(index)
                                .arg(int(look))
                                .arg(in_window.x())
                                .arg(in_window.y())
                                .arg(in_window.width())
                                .arg(in_window.height())
                                .arg(radius)
                                .arg(state.composed.cacheKey() ^
                                     (qint64{CheezyTheme::Generation()} << 48));
        auto cached = droplets.find(key);
        if (cached == droplets.end()) {
            if (droplets.size() > 300) {
                droplets.clear();
            }
            const QRectF device{in_window.x() * dpr, in_window.y() * dpr, in_window.width() * dpr,
                                in_window.height() * dpr};
            cached = droplets.insert(
                key, Droplet(state.composed_image, device, CheezyTheme::Radius(radius) * dpr,
                             look, dpr));
        }
        QPainter painter{widget};
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(area), cached.value());
    }

    /// The window under `widget` (its parent's window), photographed and blurred.
    void Photograph(QWidget* widget) {
        QWidget* const parent = widget->parentWidget();
        QWidget* const source = parent != nullptr ? parent->window() : nullptr;
        if (!glass.contains(widget)) {
            connect(widget, &QObject::destroyed, this,
                    [this, widget] { glass.remove(widget); });
        }
        Glass& state = glass[widget];
        state.composed = {};
        if (source == nullptr || source == widget || !source->isVisible()) {
            state.behind = {};
            return;
        }
        state.behind = BlurredPhoto(source);
        if (!state.behind.isNull()) {
            state.step = QSizeF(qreal(source->width()) / state.behind.width(),
                                qreal(source->height()) / state.behind.height());
        }
        state.source_origin = source->mapToGlobal(QPoint{});
    }

    void Paint(QWidget* widget, QPaintEvent* event) {
        Glass& state = glass[widget];
        const qreal dpr = widget->devicePixelRatioF();
        const QPoint at = widget->mapToGlobal(QPoint{});
        if (state.composed.isNull() || state.composed_at != at ||
            state.composed_generation != CheezyTheme::Generation() ||
            state.composed.size() != widget->size() * dpr) {
            Compose(widget, state, at, dpr);
        }
        QPainter painter{widget};
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.setClipRegion(event->region());
        painter.drawPixmap(0, 0, state.composed);
    }

    static void Compose(QWidget* widget, Glass& state, QPoint at, qreal dpr) {
        const bool popup = IsGlassPopup(widget);
        const QRectF area{QPointF{}, QSizeF(widget->size())};
        QPixmap pixmap(widget->size() * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter p{&pixmap};
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // Popups are rounded like their border in the theme; windows keep the system's frame.
        const qreal radius = popup ? CheezyTheme::Radius(14.0) : 0.0;
        QPainterPath shape;
        shape.addRoundedRect(area, radius, radius);
        p.setClipPath(shape);

        // What is behind, blurred (the warm base when there is nothing to photograph).
        p.fillRect(area, CheezyTheme::Recolor(QColor(0x3A, 0x28, 0x0C)));
        if (!state.behind.isNull()) {
            QSizeF covered;
            const QImage part = BehindPart(state.behind, state.step,
                                           QPointF(at - state.source_origin), widget->size(),
                                           popup ? 16.0 : 44.0, covered);
            p.drawImage(QRectF(QPointF{}, covered), part);
        }
        // Honey: colours behind are pulled toward gold (multiply), then a warm amber tint,
        // deeper toward the bottom so text stays easy to read.
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        p.fillRect(area, CheezyTheme::Recolor(QColor(0xFF, 0xDC, 0x96)));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QLinearGradient tint{area.topLeft(), area.bottomLeft()};
        tint.setColorAt(0, CheezyTheme::Recolor(QColor(0x7A, 0x52, 0x08, 34)));
        tint.setColorAt(1, CheezyTheme::Recolor(QColor(0x2A, 0x1A, 0x04, 100)));
        p.fillRect(area, tint);
        // Light through the glass: a warm glow from the top left, a sheen across the top.
        QRadialGradient glow{QPointF(area.width() * 0.18, -area.height() * 0.1),
                             std::max(area.width(), area.height()) * 0.7};
        glow.setColorAt(0, CheezyTheme::Recolor(QColor(0xFF, 0xD8, 0x70, 60)));
        glow.setColorAt(1, CheezyTheme::Recolor(QColor(0xFF, 0xE2, 0x9A, 0)));
        p.fillRect(area, glow);
        QLinearGradient sheen{area.topLeft(), area.bottomLeft()};
        sheen.setColorAt(0, CheezyTheme::Recolor(QColor(0xFF, 0xF4, 0xD6, 34)));
        sheen.setColorAt(popup ? 0.5 : 0.12, CheezyTheme::Recolor(QColor(0xFF, 0xEA, 0xB0, 12)));
        sheen.setColorAt(1, CheezyTheme::Recolor(QColor(0xFF, 0xEA, 0xB0, 0)));
        p.fillRect(area, sheen);
        // Frost.
        p.fillRect(area, QBrush(Frost()));
        // The rim, lit from the top left: a soft glow inside the edge, then a fine bright line
        // that shines at the top-left and bottom-right and fades along the sides between.
        QConicalGradient light{area.center(), 135.0};
        const auto shine = [&light](int strongest) {
            light.setColorAt(0.0, CheezyTheme::Recolor(QColor(0xFF, 0xFB, 0xEC, strongest)));
            light.setColorAt(0.2, CheezyTheme::Recolor(QColor(0xFF, 0xE6, 0xAA, strongest / 5)));
            light.setColorAt(0.5, CheezyTheme::Recolor(QColor(0xFF, 0xF0, 0xC8, strongest * 3 / 5)));
            light.setColorAt(0.8, CheezyTheme::Recolor(QColor(0xFF, 0xE6, 0xAA, strongest / 5)));
            light.setColorAt(1.0, CheezyTheme::Recolor(QColor(0xFF, 0xFB, 0xEC, strongest)));
        };
        p.setBrush(Qt::NoBrush);
        shine(popup ? 40 : 34);
        p.setPen(QPen(QBrush(light), popup ? 6.0 : 10.0));
        p.drawRoundedRect(area, radius, radius);
        p.setClipping(false);
        shine(235);
        p.setPen(QPen(QBrush(light), 1.3));
        p.drawRoundedRect(area.adjusted(0.65, 0.65, -0.65, -0.65), radius, radius);
        p.end();
        state.composed = pixmap;
        state.composed_generation = CheezyTheme::Generation();
        state.composed_image = pixmap.toImage();
        state.composed_at = at;
    }

    /// Windows: a dark honey title bar and frame to match the glass.
    static void DarkFrame([[maybe_unused]] QWidget* widget) {
#ifdef _WIN32
        if (IsGlassPopup(widget)) {
            return;
        }
        const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        const COLORREF caption = RGB(0x2E, 0x22, 0x0E);
        DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
        const COLORREF border = RGB(0x9A, 0x74, 0x30);
        DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
#endif
    }

    QHash<QWidget*, Glass> glass;
    QHash<QString, QImage> droplets; ///< Drawn glass capsules (Draw)
};

} // Anonymous namespace

void Install() {
    qApp->installEventFilter(new Filter(qApp));
}

} // namespace RoundedPopups
