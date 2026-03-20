#pragma once
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>
#include <QObject>
#include <functional>

// ── ThemeHelper ───────────────────────────────────────────────────────────
//
// Single source of truth for dark mode detection and theme-change signalling.
//
// Uses Qt::ColorScheme on platforms that report it (Windows 11, macOS 10.14+,
// GNOME/KDE via Qt 6.5+). Falls back to palette luminance for older Linux
// environments that do not expose a color scheme signal.
//
// Usage:
//   #include "utils/ThemeHelper.h"
//   if (ThemeHelper::isDark()) { ... }
//
//   // Re-apply styles when the system theme changes at runtime:
//   ThemeHelper::onThemeChanged(this, [this]() { applyStyles(); });

namespace ThemeHelper {

inline bool isDark() {
    const Qt::ColorScheme scheme =
        QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
    // Fallback: derive from window background luminance
    const QColor bg =
        QGuiApplication::palette().color(QPalette::Window);
    return bg.lightness() < 128;
}

// Connects a callback to the system color scheme change signal.
// The callback fires whenever the user switches between light and dark mode
// while the app is running. context is the QObject lifetime guard —
// the connection is automatically removed when context is destroyed.
inline void onThemeChanged(QObject* context, std::function<void()> callback) {
    QObject::connect(
        QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
        context, [cb = std::move(callback)](Qt::ColorScheme) { cb(); },
        Qt::UniqueConnection);
}

} // namespace ThemeHelper
