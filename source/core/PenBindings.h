#pragma once

// ============================================================================
// PenBindings - what the pen's side button and back end do
// ============================================================================
// A binding is a short string, saved in QSettings under "pen/<slot>":
//   ""               nothing
//   "cmd:<actionId>" run a ShortcutManager action (any app command)
//   "toggle:<tool>"  switch to the tool; the same gesture again goes back
//   "hold:<tool>"    use the tool only while the button is held / the back end
//                    is on the screen
// ============================================================================

#include <QString>
#include <QLatin1String>

#include "ToolType.h"

namespace PenBindings {

enum Slot {
    SideTap,        ///< Side button pressed and released, pen in the air
    SideDoubleTap,  ///< The same, twice
    SideHoldAir,    ///< Side button held, pen in the air
    SideHoldWrite,  ///< Side button held while the tip is on the screen
    BackWrite,      ///< Back end (eraser end) on the screen
    SlotCount
};

/// Current binding. Cached, so it is cheap enough for every tablet event.
const QString& binding(Slot slot);

/// Change a binding and save it.
void setBinding(Slot slot, const QString& value);

/// Tool name used in bindings and in "tool.<name>" action ids, e.g. "object_select".
QLatin1String toolName(ToolType tool);

/// Parse "<prefix><tool>". @return false if the binding has another form.
bool toolFor(const QString& binding, QLatin1String prefix, ToolType* tool);

/// Qt on Windows reports the side button only while the tip touches the
/// screen. This hook reads it from Windows, so it is also known in the air.
/// Safe to call many times. Does nothing on other platforms.
void installNativeHook();

/// Side button state from the native hook. Always false without the hook.
bool nativeSideButtonDown();

} // namespace PenBindings

/**
 * @brief Turns side-button changes into gestures. Pure logic, no Qt events.
 *
 * Feed it every tablet event with update(), and call poll() from a timer
 * while pending() is true.
 */
class PenGestures {
public:
    enum Fired { None, Tap, DoubleTap, HoldAir };

    static constexpr qint64 HOLD_MS = 400;    ///< Held this long in the air = hold, not tap
    static constexpr qint64 DOUBLE_MS = 350;  ///< Max gap between the two taps of a double tap
    static constexpr qint64 STALE_MS = 250;   ///< No event for this long = the pen left the screen

    /**
     * @param sideDown      side button is pressed
     * @param contact       tip is on the screen
     * @param doubleEnabled a double-tap binding exists (a single tap then waits DOUBLE_MS)
     * @param tapOnPress    no double-tap and no hold binding exists, so a press
     *                      can only be a tap: fire at once, do not wait for the release
     */
    Fired update(bool sideDown, bool contact, qint64 nowMs, bool doubleEnabled, bool tapOnPress = false);

    /// Timer part: a tap that waited for its double, or a hold in the air.
    Fired poll(qint64 nowMs, bool holdAirEnabled);

    bool pending() const { return m_pendingTapAt >= 0 || (m_down && !m_consumed); }

private:
    bool m_down = false;         ///< Side button state at the last event
    bool m_consumed = false;     ///< This press already did something, or cannot be a tap
    qint64 m_downAt = 0;
    qint64 m_lastEventAt = 0;
    qint64 m_pendingTapAt = -1;  ///< A tap that waits for a possible second tap
};
