// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/util/rounded_popups.h"


#include <cstdlib>

#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QObject>
#include <QOperatingSystemVersion>
#include <QStyle>
#include <QWidget>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

/// Rounded popups and glass windows.
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

[[nodiscard]] bool GlassSupported() {
#ifdef _WIN32
    static const bool supported = [] {
        const char* const value = std::getenv("CHEEZYEMU_GLASS");
        if (value != nullptr && value[0] == '0') {
            return false;
        }
        return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11_22H2;
    }();
    return supported;
#else
    return false;
#endif
}

namespace {
class Filter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Polish) {
            if (auto* const widget = qobject_cast<QWidget*>(watched); widget != nullptr) {
                const bool popup = IsPopup(widget);
                const bool glass = GlassSupported() && (IsGlassDialog(widget) || IsGlassPopup(widget));
                if (popup || glass) {
                    widget->setAttribute(Qt::WA_TranslucentBackground, true);
                }
                if (popup) {
                    widget->setWindowFlag(Qt::FramelessWindowHint, true);
                    widget->setWindowFlag(Qt::NoDropShadowWindowHint, true);
                }
                if (glass && !widget->property("glass").toBool()) {
                    widget->setProperty("glass", true);
                    widget->style()->unpolish(widget);
                    widget->style()->polish(widget);
                }
            }
        } else if (event->type() == QEvent::Show) {
            if (auto* const widget = qobject_cast<QWidget*>(watched);
                widget != nullptr && widget->isWindow() && widget->property("glass").toBool()) {
                ApplyAcrylic(widget, IsGlassPopup(widget));
            }
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

    static void ApplyAcrylic([[maybe_unused]] QWidget* widget, [[maybe_unused]] bool popup) {
#ifdef _WIN32
        const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
        // The acrylic fills the whole window behind its (see-through) contents.
        const MARGINS margins{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        const int backdrop = 3; // DWMSBT_TRANSIENTWINDOW: acrylic
        DwmSetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop,
                              sizeof(backdrop));
        if (popup) {
            const int corners = 2; // DWMWCP_ROUND: Windows rounds the popup itself
            DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners,
                                  sizeof(corners));
        }
#endif
    }
};

} // Anonymous namespace

void Install() {
    qApp->installEventFilter(new Filter(qApp));
}

} // namespace RoundedPopups
