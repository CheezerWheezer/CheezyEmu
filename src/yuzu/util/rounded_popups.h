// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// Rounded popups and glass windows (rounded_popups.cpp).
///
/// The theme rounds menus, dropdown lists and tooltips, but each is its own window, which stayed
/// a square box behind the rounded shape: before such a window first shows, it is made frameless
/// and see-through, so only the rounded shape is drawn.
///
/// Configure, a game's Properties, menus and dropdown lists are honey glass, drawn by CheezyEmu
/// itself on every system: a blurred, more vivid photo of the window behind, tinted honey, with
/// a sheen, frost and a bright rim (rules on [glass="true"] in the CheezyEmu style put glass
/// cards and pills on top). CHEEZYEMU_GLASS=0 turns the glass off.
namespace RoundedPopups {

/// Whether the glass is on (CHEEZYEMU_GLASS=0 turns it off).
[[nodiscard]] bool GlassSupported();

/// Call once, after the QApplication exists.
void Install();

} // namespace RoundedPopups
