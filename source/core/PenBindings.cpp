#include "PenBindings.h"
#include "PenHaptics.h"

#include <QSettings>

// The native hook needs the WM_POINTER API (Windows 8+). Qt5 builds still
// target Windows 7, so they go without it and keep the Qt-only behaviour.
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define SN_PEN_NATIVE_HOOK 1
#  include <QAbstractNativeEventFilter>
#  include <QCoreApplication>
#  include <windows.h>
#endif

namespace {

const char* const kKeys[PenBindings::SlotCount] = {
    "pen/sideTap", "pen/sideDoubleTap", "pen/sideHoldAir", "pen/sideHoldWrite", "pen/backWrite"
};

QString defaultBinding(PenBindings::Slot slot)
{
    // The back end erased before bindings existed; keep that.
    if (slot == PenBindings::BackWrite) return QStringLiteral("hold:eraser");
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Same default as OneNote. Windows with Qt6 only: it is the one setup
    // where Qt's side-button report was checked against the Qt source (Qt5
    // uses Wintab there, which reports buttons differently).
    if (slot == PenBindings::SideHoldWrite) return QStringLiteral("hold:lasso");
    // Hold the button over the page: the quick bar opens at the pen tip.
    if (slot == PenBindings::SideHoldAir) return QStringLiteral("cmd:view.quick_bar");
#endif
    return QString();
}

QString s_bindings[PenBindings::SlotCount];
bool s_loaded = false;
bool s_airBound = false;       ///< An "in the air" slot is set, so the native hook has work to do
bool s_nativeSideDown = false;

void updateAirBound()
{
    s_airBound = !s_bindings[PenBindings::SideTap].isEmpty()
              || !s_bindings[PenBindings::SideDoubleTap].isEmpty()
              || !s_bindings[PenBindings::SideHoldAir].isEmpty();
    if (!s_airBound) s_nativeSideDown = false;
}

void ensureLoaded()
{
    if (s_loaded) return;
    s_loaded = true;
    QSettings settings("SpeedyNote", "App");
    for (int i = 0; i < PenBindings::SlotCount; ++i) {
        const auto slot = static_cast<PenBindings::Slot>(i);
        s_bindings[i] = settings.value(kKeys[i], defaultBinding(slot)).toString();
    }
    updateAirBound();
}

struct ToolName { ToolType tool; QLatin1String name; };
const ToolName kTools[] = {
    { ToolType::Pen,          QLatin1String("pen") },
    { ToolType::Marker,       QLatin1String("marker") },
    { ToolType::Eraser,       QLatin1String("eraser") },
    { ToolType::Highlighter,  QLatin1String("highlighter") },
    { ToolType::Lasso,        QLatin1String("lasso") },
    { ToolType::ObjectSelect, QLatin1String("object_select") },
    { ToolType::Pan,          QLatin1String("pan") },
    { ToolType::Shape,        QLatin1String("shape") },
    { ToolType::Laser,        QLatin1String("laser") },
};

#ifdef SN_PEN_NATIVE_HOOK
class PenNativeHook : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const MSG* msg = static_cast<const MSG*>(message);
        switch (msg->message) {
        case WM_POINTERUPDATE:
        case WM_POINTERDOWN:
        case WM_POINTERUP:
        case WM_POINTERENTER:
        case WM_POINTERLEAVE: {
            // Speed: moves only need the pen's state for an "in the air"
            // binding; without one they skip the system calls below.
            const bool move = msg->message == WM_POINTERUPDATE;
            if (move && !s_airBound) break;
            const UINT32 id = GET_POINTERID_WPARAM(msg->wParam);
            POINTER_INPUT_TYPE type = PT_POINTER;
            if (!GetPointerType(id, &type) || type != PT_PEN) break;
            // Pen vibration: once per approach, touch, lift or leave, never per move.
            switch (msg->message) {
            case WM_POINTERENTER: PenHaptics::setPointerId(id); break;
            case WM_POINTERDOWN:  PenHaptics::penDown(id); break;
            case WM_POINTERUP:    PenHaptics::penUp(); break;
            case WM_POINTERLEAVE: PenHaptics::penLeft(); break;
            default: break;
            }
            if (!s_airBound) break;
            POINTER_PEN_INFO info;
            if (GetPointerPenInfo(id, &info)) {
                s_nativeSideDown = msg->message != WM_POINTERLEAVE
                                   && (info.penFlags & PEN_FLAG_BARREL) != 0;
            }
            break;
        }
        default:
            break;
        }
        return false;  // Only look; Qt still handles the message.
    }
};
#endif

} // namespace

namespace PenBindings {

const QString& binding(Slot slot)
{
    ensureLoaded();
    return s_bindings[slot];
}

void setBinding(Slot slot, const QString& value)
{
    ensureLoaded();
    s_bindings[slot] = value;
    updateAirBound();
    QSettings("SpeedyNote", "App").setValue(kKeys[slot], value);
}

QLatin1String toolName(ToolType tool)
{
    for (const ToolName& t : kTools) {
        if (t.tool == tool) return t.name;
    }
    return QLatin1String("pen");
}

bool toolFor(const QString& binding, QLatin1String prefix, ToolType* tool)
{
    if (!binding.startsWith(prefix)) return false;
    const auto name = QStringView(binding).mid(prefix.size());
    for (const ToolName& t : kTools) {
        if (name == t.name) {
            *tool = t.tool;
            return true;
        }
    }
    return false;
}

void installNativeHook()
{
#ifdef SN_PEN_NATIVE_HOOK
    static bool installed = false;
    if (installed || !QCoreApplication::instance()) return;
    installed = true;
    // Lives as long as the application; Qt does not take ownership.
    static PenNativeHook hook;
    QCoreApplication::instance()->installNativeEventFilter(&hook);
#endif
}

bool nativeSideButtonDown()
{
#ifdef SN_PEN_NATIVE_HOOK
    return s_nativeSideDown;
#else
    return false;
#endif
}

} // namespace PenBindings

PenGestures::Fired PenGestures::update(bool sideDown, bool contact, qint64 nowMs,
                                       bool doubleEnabled, bool tapOnPress)
{
    Fired fired = None;
    m_lastEventAt = nowMs;

    // The pen starts to write while a tap still waits for its double: the tap
    // must take effect before this stroke, not in the middle of it.
    if (contact && m_pendingTapAt >= 0) {
        m_pendingTapAt = -1;
        fired = Tap;
    }

    if (sideDown && !m_down) {
        m_down = true;
        m_downAt = nowMs;
        m_consumed = contact;  // Pressed in the middle of a stroke: not a gesture
        if (tapOnPress && !contact) {
            m_consumed = true;
            fired = Tap;
        }
    } else if (!sideDown && m_down) {
        m_down = false;
        if (!m_consumed && !contact && nowMs - m_downAt <= HOLD_MS) {
            if (!doubleEnabled) {
                fired = Tap;
            } else if (m_pendingTapAt >= 0 && nowMs - m_pendingTapAt <= DOUBLE_MS) {
                m_pendingTapAt = -1;
                fired = DoubleTap;
            } else {
                // An older tap whose wait is over but that poll() has not
                // fired yet (busy GUI thread) must not be lost.
                if (m_pendingTapAt >= 0) fired = Tap;
                m_pendingTapAt = nowMs;
            }
        }
    }

    if (m_down && contact) m_consumed = true;  // It is a "hold while writing" now
    return fired;
}

PenGestures::Fired PenGestures::poll(qint64 nowMs, bool holdAirEnabled)
{
    if (m_pendingTapAt >= 0 && nowMs - m_pendingTapAt > DOUBLE_MS) {
        m_pendingTapAt = -1;
        return Tap;
    }
    if (m_down && !m_consumed && nowMs - m_downAt >= HOLD_MS) {
        m_consumed = true;
        // No recent event: the pen left the screen with the button still down,
        // so nobody is holding it over the page.
        if (holdAirEnabled && nowMs - m_lastEventAt <= STALE_MS) return HoldAir;
    }
    return None;
}
