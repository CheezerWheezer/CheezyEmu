// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// Rounded popups and glass windows (rounded_popups.cpp).
///
/// The theme rounds menus, dropdown lists and tooltips, but each is its own window, which stayed
/// a square box behind the rounded shape: before such a window first shows, it is made frameless
/// and see-through, so only the rounded shape is drawn.
///
/// On Windows 11 (22H2 and later) Configure, a game's Properties, menus and dropdown lists are
/// also glass: the system's acrylic material blurs what is behind them, and the theme tints it
/// honey yellow (rules on [glass="true"] in the CheezyEmu style). Elsewhere they stay solid.
/// CHEEZYEMU_GLASS=0 turns the glass off.
namespace RoundedPopups {

/// Whether this system gets the glass (Windows 11 22H2 or later, not turned off).
[[nodiscard]] bool GlassSupported();

/// Call once, after the QApplication exists.
void Install();

} // namespace RoundedPopups
