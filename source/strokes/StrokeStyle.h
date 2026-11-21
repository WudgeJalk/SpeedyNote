#pragma once

// ============================================================================
// StrokeStyle - pen type, line style and arrow heads of a stroke
// ============================================================================
// Saved in a stroke's JSON only when not the default, so old files and the
// original app keep working: a missing or unknown key reads as the default.
// ============================================================================

#include <QLatin1String>
#include <QString>
#include <QtGlobal>

#include <cmath>
#include <cstddef>

namespace StrokeStyle {

enum PenType : quint8 { Fountain = 0, Ball, Brush, Marker };
enum LineStyle : quint8 { Solid = 0, Dashed, Dotted };
enum Arrow : quint8 { None = 0, Open, Filled };

/// The pen's current choice. `rounded` is a creation option only (never saved).
struct Style {
    quint8 penType = Fountain;
    quint8 lineStyle = Solid;
    quint8 arrowStart = None;
    quint8 arrowEnd = None;
    bool rounded = true;
};

// JSON key strings; index = enum value, index 0 = the default.
inline constexpr const char* kPenTypeNames[] = {"fountain", "ball", "brush", "marker"};
inline constexpr const char* kLineStyleNames[] = {"solid", "dash", "dot"};
inline constexpr const char* kArrowNames[] = {"none", "open", "filled"};

template <std::size_t N>
inline QString nameOf(const char* const (&names)[N], quint8 value)
{
    return QString::fromLatin1(names[value < N ? value : 0]);
}

template <std::size_t N>
inline quint8 valueOf(const char* const (&names)[N], const QString& name)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (name == QLatin1String(names[i])) {
            return static_cast<quint8>(i);
        }
    }
    return 0;
}

inline QString penTypeName(quint8 v) { return nameOf(kPenTypeNames, v); }
inline QString lineStyleName(quint8 v) { return nameOf(kLineStyleNames, v); }
inline QString arrowName(quint8 v) { return nameOf(kArrowNames, v); }
inline quint8 penTypeFromName(const QString& s) { return valueOf(kPenTypeNames, s); }
inline quint8 lineStyleFromName(const QString& s) { return valueOf(kLineStyleNames, s); }
inline quint8 arrowFromName(const QString& s) { return valueOf(kArrowNames, s); }

/// Line width at pressure p (0..1), as a multiple of the base thickness.
/// Ball barely follows the pressure; brush follows it strongly (at most 1.5).
inline qreal widthFactor(quint8 penType, qreal p)
{
    switch (penType) {
    case Ball:  return 0.75 + 0.25 * p;
    case Brush: return 0.1 + 1.4 * p * p;
    default:    return p;   // Fountain and Marker: today's look
    }
}

/// The pressure at which widthFactor() reaches f (for the minimum width floor).
inline qreal pressureForFactor(quint8 penType, qreal f)
{
    switch (penType) {
    case Ball:  return (f - 0.75) / 0.25;
    case Brush: return f > 0.1 ? std::sqrt((f - 0.1) / 1.4) : 0.0;
    default:    return f;
    }
}

/// Length of an arrow head for a stroke of base thickness `base`.
inline qreal arrowLength(qreal base) { return 4 * base + 6; }

/// Dash and dot spacing, each a multiple of the base thickness (user settings).
struct Look {
    qreal dashLength = 3;
    qreal dashGap = 2.5;
    qreal dotGap = 2;
};
inline Look& look() { static Look l; return l; }
/// Bumped when look() changes, so stroke caches rebuild.
inline int& generation() { static int g = 0; return g; }

} // namespace StrokeStyle
