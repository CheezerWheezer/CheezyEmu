// SPDX-FileCopyrightText: 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QObject>
#include <QWidget>

/// Rounded popups: the theme rounds menus, dropdown lists and tooltips, but each is its own
/// window, which stayed a square box behind the rounded shape. Before such a window is first
/// shown it is made frameless and see-through, so only the rounded shape is drawn.
namespace RoundedPopups {

class Filter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Polish) {
            if (auto* const widget = qobject_cast<QWidget*>(watched); widget && IsPopup(widget)) {
                widget->setAttribute(Qt::WA_TranslucentBackground, true);
                widget->setWindowFlag(Qt::FramelessWindowHint, true);
                widget->setWindowFlag(Qt::NoDropShadowWindowHint, true);
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
};

/// Call once, after the QApplication exists.
inline void Install() {
    qApp->installEventFilter(new Filter(qApp));
}

} // namespace RoundedPopups
