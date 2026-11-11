// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSize>

class QScrollArea;
class QWidget;

/// Windows that fit the screen they open on: a small laptop or handheld screen, or a high
/// Windows display scale, leaves less room than some of the emulator's windows asked for, and a
/// window that opens larger than the screen cannot be resized (its edges are off the screen).
namespace FitWindow {

/// The room a window's contents can take on its screen, without the frame and the taskbar.
[[nodiscard]] QSize Room(const QWidget* window);

/// Makes a top-level window fit its screen: a minimum size the screen cannot hold is lowered,
/// the window is made smaller if it is larger than the room, and it is moved back when its title
/// bar is off the screen. A dialog gets a size grip. Does nothing to a maximized or fullscreen
/// window, or to one that already fits.
void Fit(QWidget* window);

/// From now on every window and dialog of the application is fitted when it is shown.
void Install();

/// Puts `widget` (the body of a dialog, in a layout) into a scroll area in its place, so the
/// dialog can be smaller than its contents. Returns the scroll area.
QScrollArea* ScrollWrap(QWidget* widget);

/// For a dialog whose body scrolls: opens at `natural` (its size before the body scrolled), or
/// at what its screen has room for.
void OpenAt(QWidget* dialog, QSize natural);

} // namespace FitWindow
