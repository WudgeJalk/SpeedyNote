#include "DrawingSettings.h"

#include "../strokes/StrokeStyle.h"

#include <QSettings>
#include <QtGlobal>

namespace {
DrawingSettings& cached()
{
    static DrawingSettings s;
    return s;
}
} // namespace

const DrawingSettings& DrawingSettings::get()
{
    return cached();
}

bool DrawingSettings::reload()
{
    QSettings settings("SpeedyNote", "App");

    DrawingSettings& s = cached();
    s.eraserPen = settings.value(QStringLiteral("drawing/eraserPen"), true).toBool();
    s.eraserMarker = settings.value(QStringLiteral("drawing/eraserMarker"), true).toBool();
    s.eraserAutoDeselect = settings.value(QStringLiteral("drawing/eraserAutoDeselect"), false).toBool();
    // The bounds match the pen window and the dialog.
    const auto number = [&settings](const char* key, int fallback, int lo, int hi) {
        return qBound(lo, settings.value(QLatin1String(key), fallback).toInt(), hi);
    };
    s.stabilisation = number("pen/stabilisation", 0, 0, 100);
    s.drawAndHold = settings.value(QStringLiteral("pen/drawAndHold"), true).toBool();
    s.holdMs = number("pen/holdMs", 300, 50, 1500);
    s.holdStillPx = number("pen/holdStillPx", 4, 1, 40);
    s.holdMinLength = number("pen/holdMinLength", 40, 0, 400);
    s.handArrows = settings.value(QStringLiteral("pen/handArrows"), true).toBool();
    s.handArrowTwoStrokes = settings.value(QStringLiteral("pen/handArrowTwoStrokes"), false).toBool();
    s.handArrowHead = number("pen/handArrowHead", 0, 0, 2);
    s.cornerAngle = number("shapes/cornerAngle", 35, 10, 90);
    s.closePercent = number("shapes/closePercent", 12, 0, 50);
    s.snapPx = number("shapes/snapPx", 12, 0, 60);
    s.roughPercent = number("shapes/roughPercent", 12, 0, 40);
    s.equalPercent = number("shapes/equalPercent", 12, 0, 40);
    s.angleSnap = number("shapes/angleSnap", 12, 0, 30);
    s.trapezoidAngle = number("shapes/trapezoidAngle", 25, 12, 45);
    s.levelAngle = number("shapes/levelAngle", 5, 0, 20);
    s.laserMode = number("laser/mode", 1, 0, 1);
    s.laserHoldMs = number("laser/holdMs", 1500, 0, 10000);
    s.laserFadeMs = number("laser/fadeMs", 400, 0, 5000);
    const QColor laser = settings.value(QStringLiteral("laser/color")).value<QColor>();
    s.laserColor = laser.isValid() ? laser : DrawingSettings().laserColor;
    s.rulerSnapPx = number("ruler/snapPx", 20, 0, 80);
    s.rulerRadians = settings.value(QStringLiteral("ruler/radians"), false).toBool();
    s.fingerTapSelects = settings.value(QStringLiteral("touch/tapSelects"), true).toBool();
    s.tapMs = number("touch/tapMs", 300, 100, 1000);
    s.tapSlopPx = number("touch/tapSlopPx", 10, 2, 40);
    s.tapReachPx = number("touch/tapReachPx", 16, 4, 60);
    s.hapticSnap = settings.value(QStringLiteral("haptics/snap"), true).toBool();
    s.hapticSnapStrength = number("haptics/snapStrength", 100, 0, 100);
    s.hapticTool = settings.value(QStringLiteral("haptics/tool"), true).toBool();
    s.hapticToolStrength = number("haptics/toolStrength", 60, 0, 100);
    s.hapticWriting = settings.value(QStringLiteral("haptics/writing"), true).toBool();
    s.hapticWritingStrength = number("haptics/writingStrength", 100, 0, 100);

    // Each spacing is a multiple of the line width. The bounds match the dialog.
    const StrokeStyle::Look defaults;
    const auto ratio = [&settings](const char* key, qreal fallback) {
        return qBound(0.5, settings.value(QLatin1String(key), fallback).toDouble(), 20.0);
    };
    StrokeStyle::Look look;
    look.dashLength = ratio("drawing/dashLength", defaults.dashLength);
    look.dashGap = ratio("drawing/dashGap", defaults.dashGap);
    look.dotGap = ratio("drawing/dotGap", defaults.dotGap);

    StrokeStyle::Look& current = StrokeStyle::look();
    const bool changed = look.dashLength != current.dashLength
                         || look.dashGap != current.dashGap
                         || look.dotGap != current.dotGap;
    if (changed) {
        current = look;
        ++StrokeStyle::generation();
    }
    return changed;
}
