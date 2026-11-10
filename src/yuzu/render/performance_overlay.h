// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <vector>

#include <QFont>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

namespace VideoCore {
class ShaderNotify;
}
namespace Core {
struct PerfStatsResults;
}

class MainWindow;

/// CheezyEmu's performance bar: one black strip with a yellow edge that shows the frame rate, the
/// frame time, a frame rate history, what AMD FSR and frame generation are doing, and memory.
/// It is dragged anywhere over the game (it snaps to the borders and keeps its place when the
/// window is resized or goes fullscreen), and its right-click menu places it in a corner, turns
/// its parts on and off, and holds the graphics choices of the status bar, which fullscreen hides.
class PerformanceOverlay : public QWidget {
    Q_OBJECT

public:
    explicit PerformanceOverlay(MainWindow* parent = nullptr);
    ~PerformanceOverlay() override;

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Where the bar sits over the game. Custom is wherever it was dragged to.
    enum Anchor : int {
        TopLeft,
        TopCentre,
        TopRight,
        BottomLeft,
        BottomCentre,
        BottomRight,
        Custom,
    };

    struct Cell {
        enum class Kind { Fps, Frame, Graph, Status, Memory };
        Kind kind;
        int x;
        int width;
    };

    /// The game picture on the screen.
    [[nodiscard]] QRect Area() const;
    /// Sizes the bar for the parts that are on and the texts they show.
    void Relayout();
    /// Moves the bar to its saved place.
    void Place();
    /// Saves where the bar was dropped: a corner when it snapped to one, its own spot otherwise.
    void SavePlace();
    void UpdateStats(const Core::PerfStatsResults& results, const VideoCore::ShaderNotify& shaders);

    MainWindow* m_main_window = nullptr;

    QFont m_big;
    QFont m_value;
    QFont m_label;

    std::vector<Cell> m_cells;

    // What the bar shows
    QString m_fps_text{QStringLiteral("--")};
    QString m_fps_detail;
    QString m_frame_text{QStringLiteral("-- ms")};
    QString m_low_text;
    QString m_upscale_text;
    QString m_frame_gen_text;
    QString m_ram_text;
    QString m_vram_text;
    double m_vram_share = 0.0;

    /// Frame rates of the last minute (two samples a second), oldest first.
    static constexpr size_t HistorySize = 120;
    std::deque<double> m_history;

    // Dragging
    bool m_dragging = false;
    int m_updates = 0;
    int m_toggle_updates = 0;
    bool m_opaque = false;
    QPoint m_drag_offset;
};
