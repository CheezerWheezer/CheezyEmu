// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <utility>

#include <QActionGroup>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWindow>

#include "common/logging.h"
#include "common/memory_detect.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/perf_stats.h"
#include "main_window.h"
#include "performance_overlay.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace {
constexpr int FullHeight = 54;
constexpr int CompactHeight = 42;
constexpr int Edge = 5;    // the yellow edge on the left
constexpr int Pad = 12;    // around the cells
constexpr int Gap = 14;    // between cells, with a hairline in the middle
constexpr int Margin = 14; // from the game picture's border, in a corner
constexpr int Snap = 18;   // how near a border the dragged bar jumps to it
constexpr int GraphWidth = 132;

const QColor Cheese{0xff, 0xd2, 0x3f};
const QColor Text{0xf4, 0xf4, 0xf0};
const QColor Dim{0x93, 0x93, 0x8c};
const QColor Slow{0xff, 0x9f, 0x1c};
const QColor Bad{0xff, 0x4d, 0x4d};

QString Gigabytes(u64 bytes) {
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1);
}
} // Anonymous namespace

PerformanceOverlay::PerformanceOverlay(MainWindow* parent)
    : QWidget(parent), m_main_window{parent} {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Drag to move. Right-click for position, parts and graphics options."));

    m_big = font();
    m_big.setPixelSize(27);
    m_big.setWeight(QFont::Black);
    m_value = font();
    m_value.setPixelSize(13);
    m_value.setWeight(QFont::DemiBold);
    m_label = font();
    m_label.setPixelSize(10);
    m_label.setWeight(QFont::Bold);
    m_label.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);

    Relayout();
    Place();

    // The bar follows the game picture: the window moving, resizing or going fullscreen.
    m_main_window->installEventFilter(this);
    if (QWidget* const surface = m_main_window->GameSurface()) {
        surface->installEventFilter(this);
    }
    connect(m_main_window, &MainWindow::positionChanged, this, [this] { Place(); });
    connect(m_main_window, &MainWindow::statsUpdated, this, &PerformanceOverlay::UpdateStats);
    raise();
}

PerformanceOverlay::~PerformanceOverlay() = default;

QRect PerformanceOverlay::Area() const {
    const QWidget* const surface = m_main_window->GameSurface();
    if (surface == nullptr) {
        return m_main_window->geometry();
    }
    return QRect{surface->mapToGlobal(QPoint{0, 0}), surface->size()};
}

void PerformanceOverlay::Relayout() {
    const auto& ui = UISettings::values;
    const bool compact = ui.perf_overlay_compact.GetValue();
    const QFontMetrics big{m_big};
    const QFontMetrics value{m_value};
    const QFontMetrics label{m_label};

    m_cells.clear();
    int x = Edge + Pad;
    const auto add = [this, &x](Cell::Kind kind, int width) {
        if (!m_cells.empty()) {
            x += Gap;
        }
        m_cells.push_back({kind, x, width});
        x += width;
    };
    // Wide enough for three digits, so the bar doesn't change width as the rate moves.
    const int digits = std::max(big.horizontalAdvance(QStringLiteral("000")),
                                big.horizontalAdvance(m_fps_text));
    const int beside = std::max(label.horizontalAdvance(tr("FPS")),
                                label.horizontalAdvance(m_fps_detail));
    add(Cell::Kind::Fps, digits + 7 + beside);
    add(Cell::Kind::Frame, std::max({value.horizontalAdvance(QStringLiteral("000.0 ms")),
                                     value.horizontalAdvance(m_frame_text),
                                     label.horizontalAdvance(m_low_text)}));
    if (!compact) {
        if (ui.perf_overlay_graph.GetValue()) {
            add(Cell::Kind::Graph, GraphWidth);
        }
        if (!m_upscale_text.isEmpty() || !m_frame_gen_text.isEmpty()) {
            add(Cell::Kind::Status, std::max(value.horizontalAdvance(m_upscale_text),
                                             value.horizontalAdvance(m_frame_gen_text)));
        }
        if (ui.perf_overlay_memory.GetValue() && !m_ram_text.isEmpty()) {
            const int names = std::max(label.horizontalAdvance(tr("RAM")),
                                       label.horizontalAdvance(tr("VRAM")));
            add(Cell::Kind::Memory, names + 8 +
                                        std::max(value.horizontalAdvance(m_ram_text),
                                                 value.horizontalAdvance(m_vram_text)));
        }
    }
    const QSize wanted{x + Pad, compact ? CompactHeight : FullHeight};
    if (wanted != size()) {
        setFixedSize(wanted);
    }
}

void PerformanceOverlay::Place() {
    if (m_dragging) {
        return;
    }
    const auto& ui = UISettings::values;
    const QRect area = Area();
    const int free_x = std::max(0, area.width() - width());
    const int free_y = std::max(0, area.height() - height());
    const int margin_x = std::min(Margin, free_x / 2);
    const int margin_y = std::min(Margin, free_y / 2);
    int x = margin_x;
    int y = margin_y;
    switch (static_cast<Anchor>(ui.perf_overlay_anchor.GetValue())) {
    case TopLeft:
        break;
    case TopCentre:
        x = free_x / 2;
        break;
    case TopRight:
        x = free_x - margin_x;
        break;
    case BottomLeft:
        y = free_y - margin_y;
        break;
    case BottomCentre:
        x = free_x / 2;
        y = free_y - margin_y;
        break;
    case BottomRight:
        x = free_x - margin_x;
        y = free_y - margin_y;
        break;
    case Custom:
    default:
        // Kept as a share of the free room, so it holds its spot at any window size.
        x = free_x * std::clamp(ui.perf_overlay_x.GetValue(), 0, 1000) / 1000;
        y = free_y * std::clamp(ui.perf_overlay_y.GetValue(), 0, 1000) / 1000;
        break;
    }
    move(area.left() + x, area.top() + y);
}

void PerformanceOverlay::SavePlace() {
    auto& ui = UISettings::values;
    const QRect area = Area();
    const int free_x = std::max(1, area.width() - width());
    const int free_y = std::max(1, area.height() - height());
    const int x = std::clamp(pos().x() - area.left(), 0, free_x);
    const int y = std::clamp(pos().y() - area.top(), 0, free_y);
    const int margin_x = std::min(Margin, free_x / 2);
    const int margin_y = std::min(Margin, free_y / 2);
    const bool left = x == margin_x;
    const bool right = x == free_x - margin_x;
    const bool centre = std::abs(x - free_x / 2) <= 1;
    const bool top = y == margin_y;
    const bool bottom = y == free_y - margin_y;
    Anchor anchor = Custom;
    if (top || bottom) {
        if (left) {
            anchor = top ? TopLeft : BottomLeft;
        } else if (right) {
            anchor = top ? TopRight : BottomRight;
        } else if (centre) {
            anchor = top ? TopCentre : BottomCentre;
        }
    }
    ui.perf_overlay_anchor.SetValue(static_cast<int>(anchor));
    ui.perf_overlay_x.SetValue(x * 1000 / free_x);
    ui.perf_overlay_y.SetValue(y * 1000 / free_y);
}

void PerformanceOverlay::UpdateStats(const Core::PerfStatsResults& results,
                                     const VideoCore::ShaderNotify&) {
    const double fps = results.average_game_fps;
    if (!std::isnan(fps)) {
        // What reaches the screen with frame generation, as the status bar shows it.
        const double shown = m_main_window->DisplayedFps(fps);
        m_fps_text = QString::number(std::lround(shown));
        m_fps_detail = shown > fps + 0.5 ? tr("%1 + FG").arg(std::lround(fps)) : QString{};
        // Under 3 fps is a load or a freeze, not a frame rate to chart.
        if (fps > 3.0) {
            m_history.push_back(fps);
            if (m_history.size() > HistorySize) {
                m_history.pop_front();
            }
        }
        if (m_history.size() >= 4) {
            const double low = *std::min_element(m_history.begin(), m_history.end());
            m_low_text = tr("LOW %1").arg(std::lround(low));
        }
    }
    if (!std::isnan(results.frametime)) {
        m_frame_text = tr("%1 ms").arg(results.frametime * 1000.0, 0, 'f', 1);
    }
    m_upscale_text = m_main_window->UpscaleStatusText();
    m_frame_gen_text = m_main_window->FrameGenStatusText();

    const auto memory = Common::GetProcessMemoryUsage();
    const u64 ram = memory.working_set != 0 ? memory.working_set : memory.commit;
    m_ram_text = ram != 0 ? tr("%1 GB").arg(Gigabytes(ram)) : QString{};
    m_vram_text.clear();
    m_vram_share = 0.0;
    if (QtCommon::system != nullptr && QtCommon::system->IsPoweredOn()) {
        const auto video = QtCommon::system->GPU().Renderer().GetDeviceMemoryStatus();
        if (video.used != 0 && video.budget != 0) {
            m_vram_text = tr("%1 / %2 GB").arg(Gigabytes(video.used), Gigabytes(video.budget));
            m_vram_share = std::clamp(static_cast<double>(video.used) /
                                          static_cast<double>(video.budget),
                                      0.0, 1.0);
        } else if (video.used != 0) {
            m_vram_text = tr("%1 GB").arg(Gigabytes(video.used));
        }
    }

    const QSize before = size();
    Relayout();
    if (size() != before) {
        Place(); // a bar on the right or in the middle keeps its border
    }
    update();

    // CHEEZYEMU_OVERLAY_SHOT=<file.png>: saves a picture of the bar every few updates and logs
    // where it sits, to check it without capturing the screen.
    static const QString shot = qEnvironmentVariable("CHEEZYEMU_OVERLAY_SHOT");
    if (!shot.isEmpty() && ++m_updates % 6 == 0) {
        grab().save(shot);
        const QRect area = Area();
        LOG_INFO(Frontend, "Performance bar at {},{} {}x{} over the game picture at {},{} {}x{}",
                 x(), y(), width(), height(), area.x(), area.y(), area.width(), area.height());
    }
}

void PerformanceOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int opacity = std::clamp(UISettings::values.perf_overlay_opacity.GetValue(), 20, 100);
    const QRectF box{0.5, 0.5, width() - 1.0, height() - 1.0};
    QPainterPath shape;
    shape.addRoundedRect(box, 5.0, 5.0);
    painter.fillPath(shape, QColor{9, 9, 11, opacity * 255 / 100});
    // The yellow edge, cut by the box's corners.
    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(QRect{0, 0, Edge, height()}, Cheese);
    painter.restore();
    painter.setPen(QPen{QColor{0xff, 0xd2, 0x3f, 70}, 1.0});
    painter.drawPath(shape);

    const QFontMetrics big{m_big};
    const QFontMetrics value{m_value};
    const QFontMetrics label{m_label};
    const int h = height();
    // Two text rows, centred in the bar.
    const int rows = value.height() + 3 + label.height();
    const int row1 = (h - rows) / 2 + value.ascent();
    const int row2 = (h - rows) / 2 + value.height() + 3 + label.ascent();

    for (size_t index = 0; index < m_cells.size(); ++index) {
        const Cell& cell = m_cells[index];
        if (index != 0) {
            painter.setPen(QPen{QColor{255, 255, 255, 34}, 1.0});
            const qreal line = cell.x - Gap / 2.0;
            painter.drawLine(QPointF{line, 9.0}, QPointF{line, h - 9.0});
        }
        switch (cell.kind) {
        case Cell::Kind::Fps: {
            painter.setFont(m_big);
            painter.setPen(Cheese);
            const int digits = std::max(big.horizontalAdvance(QStringLiteral("000")),
                                        big.horizontalAdvance(m_fps_text));
            // Right-aligned in their room, so "60" and "120" end at the same place.
            painter.drawText(QRect{cell.x, 0, digits, h}, Qt::AlignRight | Qt::AlignVCenter,
                             m_fps_text);
            painter.setFont(m_label);
            painter.setPen(Text);
            const int beside = cell.x + digits + 7;
            if (m_fps_detail.isEmpty()) {
                painter.drawText(QRect{beside, 0, cell.width, h}, Qt::AlignLeft | Qt::AlignVCenter,
                                 tr("FPS"));
            } else {
                painter.drawText(beside, row1 - 1, tr("FPS"));
                painter.setPen(Dim);
                painter.drawText(beside, row2, m_fps_detail);
            }
            break;
        }
        case Cell::Kind::Frame:
            painter.setFont(m_value);
            painter.setPen(Text);
            painter.drawText(cell.x, row1, m_frame_text);
            painter.setFont(m_label);
            painter.setPen(Dim);
            painter.drawText(cell.x, row2, m_low_text.isEmpty() ? tr("FRAME TIME") : m_low_text);
            break;
        case Cell::Kind::Graph: {
            // One bar per half second, newest on the right; a dip stands out in orange or red.
            const QRectF plot{static_cast<qreal>(cell.x), 10.0, static_cast<qreal>(cell.width),
                              h - 20.0};
            painter.setPen(Qt::NoPen);
            painter.fillRect(QRectF{plot.left(), plot.bottom(), plot.width(), 1.0},
                             QColor{255, 255, 255, 40});
            if (m_history.empty()) {
                break;
            }
            std::vector<double> sorted(m_history.begin(), m_history.end());
            std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
            const double median = sorted[sorted.size() / 2];
            const double top = std::max(*std::max_element(m_history.begin(), m_history.end()),
                                        1.0);
            const qreal step = plot.width() / static_cast<qreal>(HistorySize);
            const size_t first = HistorySize - m_history.size();
            for (size_t i = 0; i < m_history.size(); ++i) {
                const double sample = m_history[i];
                const qreal bar = std::max<qreal>(1.0, plot.height() * sample / top);
                const QColor colour =
                    sample < median * 0.6 ? Bad : sample < median * 0.8 ? Slow : Cheese;
                painter.fillRect(QRectF{plot.left() + step * static_cast<qreal>(first + i),
                                        plot.bottom() - bar, std::max<qreal>(step - 0.25, 0.5),
                                        bar},
                                 colour);
            }
            break;
        }
        case Cell::Kind::Status: {
            painter.setFont(m_value);
            const bool both = !m_upscale_text.isEmpty() && !m_frame_gen_text.isEmpty();
            if (both) {
                // Two lines of the same size, centred together.
                const int block = value.height() * 2 + 2;
                const int first = (h - block) / 2 + value.ascent();
                painter.setPen(Cheese);
                painter.drawText(cell.x, first, m_upscale_text);
                painter.setPen(Text);
                painter.drawText(cell.x, first + value.height() + 2, m_frame_gen_text);
            } else {
                painter.setPen(Cheese);
                painter.drawText(QRect{cell.x, 0, cell.width, h}, Qt::AlignLeft | Qt::AlignVCenter,
                                 m_upscale_text.isEmpty() ? m_frame_gen_text : m_upscale_text);
            }
            break;
        }
        case Cell::Kind::Memory: {
            const int names = std::max(label.horizontalAdvance(tr("RAM")),
                                       label.horizontalAdvance(tr("VRAM"))) +
                              8;
            const int block = value.height() * 2 + 2;
            const int first = (h - block) / 2 + value.ascent();
            const int second = first + value.height() + 2;
            painter.setFont(m_label);
            painter.setPen(Dim);
            painter.drawText(cell.x, first, tr("RAM"));
            if (!m_vram_text.isEmpty()) {
                painter.drawText(cell.x, second, tr("VRAM"));
            }
            painter.setFont(m_value);
            painter.setPen(Text);
            painter.drawText(cell.x + names, first, m_ram_text);
            if (!m_vram_text.isEmpty()) {
                // Video memory near its budget is where textures start to be thrown out.
                painter.setPen(m_vram_share > 0.95 ? Bad : m_vram_share > 0.85 ? Slow : Text);
                painter.drawText(cell.x + names, second, m_vram_text);
            }
            break;
        }
        }
    }
}

void PerformanceOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    // Wayland ignores move() on top-level windows: the compositor drags it instead.
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")) && windowHandle() &&
        windowHandle()->startSystemMove()) {
        return;
    }
    m_dragging = true;
    m_drag_offset = event->globalPosition().toPoint() - pos();
    setCursor(Qt::ClosedHandCursor);
}

void PerformanceOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging || !(event->buttons() & Qt::LeftButton)) {
        return;
    }
    const QRect area = Area();
    const int free_x = std::max(0, area.width() - width());
    const int free_y = std::max(0, area.height() - height());
    const int margin_x = std::min(Margin, free_x / 2);
    const int margin_y = std::min(Margin, free_y / 2);
    const QPoint target = event->globalPosition().toPoint() - m_drag_offset - area.topLeft();
    // Near a border, or the middle of the top or bottom one, the bar jumps to its place there.
    const auto snap = [](int at, int to) { return std::abs(at - to) <= Snap ? to : at; };
    int x = snap(snap(target.x(), margin_x), free_x - margin_x);
    int y = snap(snap(target.y(), margin_y), free_y - margin_y);
    if (y == margin_y || y == free_y - margin_y) {
        x = snap(x, free_x / 2);
    }
    // Anywhere over the picture, never lost outside it.
    x = std::clamp(x, 0, free_x);
    y = std::clamp(y, 0, free_y);
    move(area.left() + x, area.top() + y);
}

void PerformanceOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_dragging) {
        return;
    }
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    SavePlace();
}

void PerformanceOverlay::contextMenuEvent(QContextMenuEvent* event) {
    auto& ui = UISettings::values;
    QMenu menu(this);

    QMenu* const position = menu.addMenu(tr("Position"));
    auto* const places = new QActionGroup(position);
    const std::pair<Anchor, QString> anchors[]{
        {TopLeft, tr("Top left")},
        {TopCentre, tr("Top centre")},
        {TopRight, tr("Top right")},
        {BottomLeft, tr("Bottom left")},
        {BottomCentre, tr("Bottom centre")},
        {BottomRight, tr("Bottom right")},
    };
    for (const auto& [anchor, name] : anchors) {
        QAction* const action = position->addAction(name, [this, anchor = anchor] {
            UISettings::values.perf_overlay_anchor.SetValue(static_cast<int>(anchor));
            Place();
        });
        action->setCheckable(true);
        action->setChecked(ui.perf_overlay_anchor.GetValue() == static_cast<int>(anchor));
        places->addAction(action);
    }
    if (ui.perf_overlay_anchor.GetValue() == static_cast<int>(Custom)) {
        position->addSeparator();
        QAction* const dragged = position->addAction(tr("Where I dragged it"));
        dragged->setCheckable(true);
        dragged->setChecked(true);
        dragged->setEnabled(false);
    }

    const auto toggle = [this, &menu](const QString& name, Settings::Setting<bool>& setting) {
        QAction* const action = menu.addAction(name, [this, &setting] {
            setting.SetValue(!setting.GetValue());
            Relayout();
            Place();
            update();
        });
        action->setCheckable(true);
        action->setChecked(setting.GetValue());
    };
    toggle(tr("Compact (frame rate and frame time only)"), ui.perf_overlay_compact);
    toggle(tr("Frame rate history"), ui.perf_overlay_graph);
    toggle(tr("Memory"), ui.perf_overlay_memory);

    QMenu* const opacity = menu.addMenu(tr("Background"));
    auto* const levels = new QActionGroup(opacity);
    for (const int percent : {100, 85, 70, 50, 30}) {
        QAction* const action = opacity->addAction(tr("%1%").arg(percent), [this, percent] {
            UISettings::values.perf_overlay_opacity.SetValue(percent);
            update();
        });
        action->setCheckable(true);
        action->setChecked(ui.perf_overlay_opacity.GetValue() == percent);
        levels->addAction(action);
    }

    // The status bar's graphics choices: fullscreen hides the status bar, not this bar.
    menu.addSeparator();
    m_main_window->AddQuickGraphicsMenu(menu);

    menu.addSeparator();
    menu.addAction(tr("Hide the performance bar"), [this] { close(); });
    menu.exec(event->globalPos());
}

void PerformanceOverlay::closeEvent(QCloseEvent*) {
    emit closed();
}

bool PerformanceOverlay::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::WindowStateChange:
        Place();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
