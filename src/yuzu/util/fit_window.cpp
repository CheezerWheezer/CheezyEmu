// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QLayout>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include "yuzu/util/fit_window.h"

namespace FitWindow {
namespace {

const QScreen* ScreenOf(const QWidget* window) {
    const QScreen* screen = window != nullptr ? window->screen() : nullptr;
    return screen != nullptr ? screen : QGuiApplication::primaryScreen();
}

QMargins FrameOf(const QWidget* window) {
    QMargins frame;
    if (window != nullptr && window->windowHandle() != nullptr) {
        frame = window->windowHandle()->frameMargins();
    }
    // Not known before the window is shown: a title bar and thin borders.
    if (frame.top() <= 0) {
        frame = QMargins(8, 32, 8, 8);
    }
    return frame;
}

class Filter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show && !event->spontaneous() && watched->isWidgetType()) {
            auto* const widget = static_cast<QWidget*>(watched);
            const Qt::WindowType type = widget->windowType();
            if (widget->isWindow() && (type == Qt::Window || type == Qt::Dialog) &&
                !widget->windowFlags().testFlag(Qt::FramelessWindowHint)) {
                // After the show: the frame and the screen are known then.
                QTimer::singleShot(0, widget, [window = QPointer<QWidget>(widget)] {
                    if (window) {
                        Fit(window);
                    }
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // Anonymous namespace

QSize Room(const QWidget* window) {
    // CHEEZYEMU_FIT_ROOM=<width>x<height> (test): as if the screen had only this much room.
    static const QSize forced = [] {
        const QByteArray value = qgetenv("CHEEZYEMU_FIT_ROOM");
        const QList<QByteArray> parts = value.split('x');
        return parts.size() == 2 ? QSize(parts[0].toInt(), parts[1].toInt()) : QSize();
    }();
    if (forced.isValid() && !forced.isEmpty()) {
        return forced;
    }
    const QScreen* const screen = ScreenOf(window);
    if (screen == nullptr) {
        return QSize(1280, 720);
    }
    const QRect available = screen->availableGeometry();
    const QMargins frame = FrameOf(window);
    return QSize(std::max(available.width() - frame.left() - frame.right(), 320),
                 std::max(available.height() - frame.top() - frame.bottom(), 240));
}

void Fit(QWidget* window) {
    if (window == nullptr || !window->isWindow() || window->isFullScreen() ||
        window->isMaximized() || window->isMinimized()) {
        return;
    }
    const QScreen* const screen = ScreenOf(window);
    if (screen == nullptr) {
        return;
    }
    if (auto* const dialog = qobject_cast<QDialog*>(window)) {
        dialog->setSizeGripEnabled(true);
    }
    const QRect available = screen->availableGeometry();
    const QSize room = Room(window);
    // A minimum (the window's own or its layout's) the screen cannot hold would undo the resize.
    const QSize minimum = window->minimumSize();
    if (minimum.width() > room.width() || minimum.height() > room.height()) {
        window->setMinimumSize(minimum.boundedTo(room));
    }
    bool changed = false;
    const QSize size = window->size();
    if (size.width() > room.width() || size.height() > room.height()) {
        const QSize maximum = window->maximumSize();
        if (maximum.width() < size.width() || maximum.height() < size.height()) {
            window->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX); // Was a fixed size
        }
        window->resize(size.boundedTo(room));
        changed = true;
    }
    const QRect frame = window->frameGeometry();
    const bool unreachable = frame.top() < available.top() ||
                             frame.top() > available.bottom() - 48 ||
                             frame.right() < available.left() + 96 ||
                             frame.left() > available.right() - 96;
    if (changed || unreachable) {
        const int x = std::clamp(frame.left(), available.left(),
                                 std::max(available.left(), available.right() + 1 - frame.width()));
        const int y = std::clamp(frame.top(), available.top(),
                                 std::max(available.top(), available.bottom() + 1 - frame.height()));
        if (QPoint(x, y) != frame.topLeft()) {
            window->move(x, y);
        }
    }
}

void Install() {
    static bool installed = false;
    if (installed || qApp == nullptr) {
        return;
    }
    installed = true;
    qApp->installEventFilter(new Filter(qApp));
}

QScrollArea* ScrollWrap(QWidget* widget) {
    QWidget* const parent = widget->parentWidget();
    auto* const scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    if (parent != nullptr && parent->layout() != nullptr) {
        delete parent->layout()->replaceWidget(widget, scroll);
    }
    scroll->setWidget(widget);
    return scroll;
}

void OpenAt(QWidget* dialog, QSize natural) {
    dialog->setMinimumSize(0, 0);
    const QWidget* const anchor =
        dialog->parentWidget() != nullptr ? dialog->parentWidget()->window() : dialog;
    dialog->resize(natural.boundedTo(Room(anchor)));
}

} // namespace FitWindow
