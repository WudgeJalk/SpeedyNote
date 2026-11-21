#pragma once

// ============================================================================
// PenHaptics - vibration of a pen with haptics (Windows 11, e.g. Surface Slim Pen 2)
// ============================================================================
// A click when something snaps into place, a light click when the tool
// changes, and a "paper" feel while the pen writes. It only adds feel: nothing
// depends on it. Real on Windows with Qt6 and C++/WinRT; every call does
// nothing elsewhere. The WinRT calls run on one worker thread, so the GUI
// thread never waits, and nothing runs per pen move: one call when the pen
// touches, one when it lifts. Settings: DrawingSettings::haptic*.
// ============================================================================

#include <QtGlobal>   // Before ToolType.h, which reads QT_VERSION

#include "ToolType.h"

struct DrawingSettings;

namespace PenHaptics {

enum Kind { Snap, ToolChange, Writing };

/// The waveforms used (named as in KnownSimpleHapticsControllerWaveforms).
enum class Wave : quint8 { None, Click, Ink, Marker, Brush, Eraser, Count };

struct Request {
    Wave wave = Wave::None;
    double intensity = 1.0;   ///< 0..1, used when the pen supports it
};

/// What to play for @p kind with these settings (pure, tested). @p tool and
/// @p penType (StrokeStyle::PenType) only matter for Writing.
Request decide(Kind kind, const DrawingSettings& settings, ToolType tool = ToolType::Pen, int penType = 0);

void click(Kind kind);                              ///< Snap or ToolChange
void startContinuous(ToolType tool, int penType);   ///< The pen just touched the page
void stop();                                        ///< Stops the continuous feel

// Fed by the native pen hook (PenBindings.cpp), once per approach, touch or lift.
void setPointerId(quint32 id);   ///< A pen came near the screen (WM_POINTERENTER)
void penDown(quint32 id);        ///< WM_POINTERDOWN of a pen
void penUp();                    ///< WM_POINTERUP of a pen: the continuous feel stops
void penLeft();                  ///< WM_POINTERLEAVE of a pen: no click reaches it now

enum class Status { NotBuilt, NoPenYet, NoVibration, Vibration };
Status status();                 ///< For the settings page
bool intensitySupported();       ///< The pen found can play softer or stronger

} // namespace PenHaptics
