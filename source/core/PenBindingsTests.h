#pragma once

// ============================================================================
// PenBindingsTests - side button gestures and binding strings
// ============================================================================
// Run with: speedynote.exe --test-penbindings   (debug build)
// Only the pure logic is tested here. The real pen cannot be simulated: what
// the hardware sends must be checked on the device.
// ============================================================================

#include "PenBindings.h"
#include "PenHaptics.h"
#include "DrawingSettings.h"
#include "../strokes/StrokeStyle.h"

#include <cstdio>

namespace PenBindingsTests {

inline int g_failed = 0;

inline void check(bool ok, const char* what)
{
    std::printf("  %s... %s\n", what, ok ? "PASSED" : "FAILED");
    if (!ok) ++g_failed;
}

inline bool runAllTests()
{
    using G = PenGestures;
    g_failed = 0;
    std::printf("\n=== PenBindings Unit Tests ===\n\n");

    {   // Press and release in the air
        G g;
        bool ok = g.update(true, false, 0, false) == G::None;
        ok &= g.pending();
        ok &= g.update(false, false, 100, false) == G::Tap;
        ok &= !g.pending();
        check(ok, "tap fires on release");
    }
    {   // Nothing else is bound: the tap fires on the press, and only once
        G g;
        bool ok = g.update(true, false, 0, false, true) == G::Tap;
        ok &= g.update(false, false, 100, false, true) == G::None;
        ok &= !g.pending();
        ok &= g.update(true, true, 200, false, true) == G::None;  // Pressed mid-stroke
        check(ok, "tap fires on press when nothing else is bound");
    }
    {   // Held too long, no hold binding
        G g;
        g.update(true, false, 0, false);
        bool ok = g.poll(450, false) == G::None;
        ok &= g.update(false, false, 600, false) == G::None;
        ok &= !g.pending();
        check(ok, "long press without a hold binding does nothing");
    }
    {   // Held in the air, pen still over the screen
        G g;
        g.update(true, false, 0, false);
        g.update(true, false, 200, false);
        g.update(true, false, 390, false);
        bool ok = g.poll(300, true) == G::None;
        ok &= g.poll(410, true) == G::HoldAir;
        ok &= g.poll(450, true) == G::None;                // Only once
        ok &= g.update(false, false, 500, false) == G::None;  // The release is not a tap
        check(ok, "hold in the air fires once");
    }
    {   // Button down, then the pen leaves the screen: no more events
        G g;
        g.update(true, false, 0, false);
        bool ok = g.poll(410, true) == G::None;
        ok &= !g.pending();
        check(ok, "hold does not fire after the pen left");
    }
    {   // Held while the tip is down: that is for the "hold" tool, not a gesture
        G g;
        g.update(true, false, 0, false);
        g.update(true, true, 100, false);
        bool ok = g.poll(500, true) == G::None;
        ok &= g.update(true, false, 550, false) == G::None;
        ok &= g.update(false, false, 600, false) == G::None;
        check(ok, "hold while writing is not a tap or an air hold");
    }
    {   // Two taps
        G g;
        g.update(true, false, 0, true);
        bool ok = g.update(false, false, 50, true) == G::None;
        ok &= g.pending();
        g.update(true, false, 150, true);
        ok &= g.update(false, false, 200, true) == G::DoubleTap;
        ok &= g.poll(1000, false) == G::None;
        check(ok, "double tap fires, and no extra tap after it");
    }
    {   // One tap while a double-tap binding exists
        G g;
        g.update(true, false, 0, true);
        g.update(false, false, 50, true);
        bool ok = g.poll(300, false) == G::None;
        ok &= g.poll(410, false) == G::Tap;
        ok &= !g.pending();
        check(ok, "single tap waits for the double, then fires");
    }
    {   // A second tap arrives late, and the timer did not fire the first one yet
        G g;
        g.update(true, false, 0, true);
        g.update(false, false, 50, true);
        g.update(true, false, 900, true);
        bool ok = g.update(false, false, 950, true) == G::Tap;  // The first tap, not lost
        ok &= g.poll(1400, false) == G::Tap;                    // Then the second one
        ok &= !g.pending();
        check(ok, "late second tap gives two single taps");
    }
    {   // Tap, then the pen writes before the wait is over
        G g;
        g.update(true, false, 0, true);
        g.update(false, false, 50, true);
        bool ok = g.update(false, true, 100, true) == G::Tap;
        ok &= g.poll(1000, false) == G::None;
        check(ok, "waiting tap fires before the stroke starts");
    }
    {   // Button pressed in the middle of a stroke
        G g;
        g.update(false, true, 0, false);
        bool ok = g.update(true, true, 10, false) == G::None;
        ok &= g.update(false, true, 50, false) == G::None;
        ok &= g.update(false, false, 60, false) == G::None;
        ok &= !g.pending();
        check(ok, "button pressed mid-stroke is ignored");
    }
    {   // Binding strings
        ToolType tool = ToolType::Pen;
        bool ok = PenBindings::toolFor(QStringLiteral("hold:lasso"), QLatin1String("hold:"), &tool)
                  && tool == ToolType::Lasso;
        ok &= PenBindings::toolFor(QStringLiteral("toggle:object_select"), QLatin1String("toggle:"), &tool)
              && tool == ToolType::ObjectSelect;
        ok &= !PenBindings::toolFor(QStringLiteral("cmd:edit.undo"), QLatin1String("hold:"), &tool);
        ok &= !PenBindings::toolFor(QStringLiteral("hold:nope"), QLatin1String("hold:"), &tool);
        ok &= !PenBindings::toolFor(QString(), QLatin1String("hold:"), &tool);
        for (ToolType t : { ToolType::Pen, ToolType::Marker, ToolType::Eraser, ToolType::Highlighter,
                            ToolType::Lasso, ToolType::ObjectSelect, ToolType::Pan, ToolType::Shape,
                            ToolType::Laser }) {
            ToolType back = ToolType::Pen;
            ok &= PenBindings::toolFor(QStringLiteral("hold:") + PenBindings::toolName(t),
                                       QLatin1String("hold:"), &back) && back == t;
        }
        check(ok, "binding strings parse, and every tool name round-trips");
    }
    {   // Pen vibration: the paper feel follows the tool and the pen type
        using PenHaptics::Wave;
        const DrawingSettings s;   // Defaults: all three on
        const auto feel = [&s](ToolType t, int type = StrokeStyle::Fountain) {
            return PenHaptics::decide(PenHaptics::Writing, s, t, type).wave;
        };
        bool ok = feel(ToolType::Pen) == Wave::Ink;
        ok &= feel(ToolType::Pen, StrokeStyle::Ball) == Wave::Ink;
        ok &= feel(ToolType::Pen, StrokeStyle::Brush) == Wave::Brush;
        ok &= feel(ToolType::Pen, StrokeStyle::Marker) == Wave::Marker;
        ok &= feel(ToolType::Marker) == Wave::Marker;
        ok &= feel(ToolType::Eraser) == Wave::Eraser;
        ok &= feel(ToolType::Shape) == Wave::Ink;
        for (ToolType t : { ToolType::Lasso, ToolType::ObjectSelect, ToolType::Pan,
                            ToolType::Highlighter, ToolType::Laser }) {
            ok &= feel(t) == Wave::None;   // Nothing is written with these
        }
        check(ok, "vibration: the writing feel follows the tool and the pen type");
    }
    {   // Pen vibration: switches and strengths
        using PenHaptics::Wave;
        DrawingSettings s;
        const auto decide = [&s](PenHaptics::Kind k) { return PenHaptics::decide(k, s); };
        bool ok = decide(PenHaptics::Snap).wave == Wave::Click && decide(PenHaptics::Snap).intensity == 1.0;
        ok &= decide(PenHaptics::ToolChange).wave == Wave::Click && decide(PenHaptics::ToolChange).intensity == 0.6;
        s.hapticSnap = false;
        ok &= decide(PenHaptics::Snap).wave == Wave::None;
        ok &= decide(PenHaptics::ToolChange).wave == Wave::Click;   // The others stay on
        s.hapticTool = false;
        ok &= decide(PenHaptics::ToolChange).wave == Wave::None;
        s.hapticWriting = false;
        ok &= decide(PenHaptics::Writing).wave == Wave::None;
        s.hapticWriting = true;
        s.hapticWritingStrength = 40;
        ok &= decide(PenHaptics::Writing).wave == Wave::Ink && decide(PenHaptics::Writing).intensity == 0.4;
        s.hapticWritingStrength = 0;   // 0 % is off
        ok &= decide(PenHaptics::Writing).wave == Wave::None;
        // Without a pen, every call is harmless and does nothing.
        PenHaptics::click(PenHaptics::Snap);
        PenHaptics::startContinuous(ToolType::Pen, 0);
        PenHaptics::stop();
        PenHaptics::penUp();
        check(ok, "vibration: each switch turns its own feel off, strength 0 too");
    }

    std::printf("\n=== Results: %d failed ===\n", g_failed);
    std::fflush(stdout);
    return g_failed == 0;
}

} // namespace PenBindingsTests
