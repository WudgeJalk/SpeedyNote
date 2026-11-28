#pragma once

#include "../strokes/VectorStroke.h"

// ============================================================================
// DrawingSettings - drawing thresholds the user can change, read once
// ============================================================================
// Nothing on the pen path reads QSettings: it reads these cached values.
// Call reload() after any UI writes the drawing keys to QSettings.
// Keys live in QSettings("SpeedyNote", "App") under "drawing/".
// ============================================================================

struct DrawingSettings {
    // Each stage adds its plain members here (the dash and dot spacing live
    // in StrokeStyle::look(), next to the geometry that uses them).

    // Eraser filter: the eraser leaves alone the lines of a tool that is off.
    bool eraserPen = true;            ///< "drawing/eraserPen"
    bool eraserMarker = true;         ///< "drawing/eraserMarker"
    /// "drawing/eraserAutoDeselect": go back to the last tool after erasing.
    bool eraserAutoDeselect = false;

    // Pen window and Settings > Drawing > Pen.
    int stabilisation = 0;            ///< "pen/stabilisation", 0..100 (0 = off)
    bool drawAndHold = true;          ///< "pen/drawAndHold": hold the pen still to clean the line
    int holdMs = 300;                 ///< "pen/holdMs", 50..1500
    int holdStillPx = 4;              ///< "pen/holdStillPx": the pen counts as still within this (screen px)
    int holdMinLength = 40;           ///< "pen/holdMinLength": shorter lines are left alone (screen px)
    bool handArrows = true;           ///< "pen/handArrows": a line, then a head at its end, becomes an arrow
    bool handArrowTwoStrokes = false; ///< "pen/handArrowTwoStrokes": a head drawn after the line (a second stroke) makes an arrow too. Off: a 7 or a 4 stays ink.
    int handArrowHead = 0;            ///< "pen/handArrowHead": 0 as drawn (V open, triangle filled), 1 always open, 2 always filled

    // Cleaned strokes (draw and hold, free shape). Settings > Drawing > Shapes.
    int cornerAngle = 35;             ///< "shapes/cornerAngle": a turn this sharp (degrees) is a corner
    int closePercent = 12;            ///< "shapes/closePercent": ends this close (% of the size) close the shape
    int snapPx = 12;                  ///< "shapes/snapPx": ends snap to a line this near (screen px, 0 = off)
    int roughPercent = 12;            ///< "shapes/roughPercent": a line this near an exact shape becomes it (% of its size, 0 = off)
    int equalPercent = 12;            ///< "shapes/equalPercent": lengths this close are made equal (circle, square)
    int angleSnap = 12;               ///< "shapes/angleSnap": sides this close to parallel or square are made so (degrees)
    int trapezoidAngle = 25;          ///< "shapes/trapezoidAngle": a box becomes a trapezoid only past this (degrees)
    int levelAngle = 5;              ///< "shapes/levelAngle": a shape this close to level is made level (degrees)

    // Laser pointer. The mode is chosen in the laser bar, the rest in Settings > Drawing.
    int laserMode = 1;                ///< "laser/mode": 0 point (a dot), 1 line (a trail)
    int laserHoldMs = 1500;           ///< "laser/holdMs": the lines stay this long after the pen lifts
    int laserFadeMs = 400;            ///< "laser/fadeMs": then they fade out over this time
    QColor laserColor = QColor(255, 32, 32);   ///< "laser/color"

    // Ruler. Settings > Drawing > Ruler.
    int rulerSnapPx = 20;             ///< "ruler/snapPx": a line started this near an edge follows it (screen px, 0 = off)
    bool rulerRadians = false;        ///< "ruler/radians": show and set the angle in radians

    // Finger tap. Settings > Drawing > Finger.
    bool fingerTapSelects = true;     ///< "touch/tapSelects": a finger tap selects a line or a shape
    int tapMs = 300;                  ///< "touch/tapMs": a tap lasts at most this long
    int tapSlopPx = 10;               ///< "touch/tapSlopPx": a tap moves less than this (screen px)
    int tapReachPx = 16;              ///< "touch/tapReachPx": a tap this near a line hits it (screen px)

    // Pen vibration (a pen with haptics, Windows 11). Settings > Stylus. See PenHaptics.
    bool hapticSnap = true;           ///< "haptics/snap": a click when a line or a shape snaps into place
    int hapticSnapStrength = 100;     ///< "haptics/snapStrength", 0..100 %
    bool hapticTool = true;           ///< "haptics/tool": a light click when the tool changes
    int hapticToolStrength = 60;      ///< "haptics/toolStrength", 0..100 %
    bool hapticWriting = true;        ///< "haptics/writing": a paper feel while the pen writes
    int hapticWritingStrength = 100;  ///< "haptics/writingStrength", 0..100 %

    /// True when the eraser filter lets the eraser remove @p stroke.
    bool eraserMayTouch(const VectorStroke& stroke) const {
        return stroke.isMarker() ? eraserMarker : eraserPen;
    }

    /// The cached values. Valid after the first reload().
    static const DrawingSettings& get();

    /**
     * @brief Read the values from QSettings again.
     * @return True when the look of dashed or dotted lines changed; the
     *         stroke caches then rebuild, and the caller repaints the views.
     */
    static bool reload();
};
