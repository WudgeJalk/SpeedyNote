#include "PenHaptics.h"

#include "DrawingSettings.h"
#include "../strokes/StrokeStyle.h"

// C++/WinRT is there when the Windows Ink OCR is (64-bit Qt6 builds, see
// CMakeLists.txt). PenDevice::SimpleHapticsController needs Windows 11; on an
// older Windows the call fails and there is simply no feel.
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && defined(SPEEDYNOTE_HAS_WINDOWS_INK)
#  define SN_PEN_HAPTICS 1
#  include <QDebug>
#  include <QThreadPool>
#  include <atomic>
#  include <functional>
#  include <winrt/Windows.Foundation.h>
#  include <winrt/Windows.Foundation.Collections.h>
#  include <winrt/Windows.Devices.Haptics.h>
#  include <winrt/Windows.Devices.Input.h>
#endif

namespace PenHaptics {

namespace {

Wave writingWave(ToolType tool, int penType)
{
    switch (tool) {
    case ToolType::Pen:
        return penType == StrokeStyle::Brush ? Wave::Brush
             : penType == StrokeStyle::Marker ? Wave::Marker
             : Wave::Ink;                          // Fountain and ball
    case ToolType::Shape:  return Wave::Ink;       // The free shape is drawn like the pen
    case ToolType::Marker: return Wave::Marker;
    case ToolType::Eraser: return Wave::Eraser;
    default:               return Wave::None;      // Selection, pan, laser, text: nothing is written
    }
}

} // namespace

Request decide(Kind kind, const DrawingSettings& s, ToolType tool, int penType)
{
    const bool on[] = {s.hapticSnap, s.hapticTool, s.hapticWriting};
    const int strength[] = {s.hapticSnapStrength, s.hapticToolStrength, s.hapticWritingStrength};
    Request r;
    if (!on[kind] || strength[kind] <= 0) {
        return r;
    }
    r.wave = kind == Writing ? writingWave(tool, penType) : Wave::Click;
    r.intensity = qMin(strength[kind], 100) / 100.0;
    return r;
}

#ifdef SN_PEN_HAPTICS

namespace {

using namespace winrt::Windows::Devices::Haptics;
using winrt::Windows::Devices::Input::PenDevice;

std::atomic<int> s_status{int(Status::NoPenYet)};
std::atomic<bool> s_intensity{false};

// GUI thread only.
quint32 s_pointerId = 0;
bool s_near = false;      ///< A pen is in range over a window of the app
bool s_contact = false;   ///< ...and touches the screen
bool s_playing = false;   ///< A continuous wave was sent and not stopped yet

// Worker thread only.
struct Device {
    quint32 id = 0;
    SimpleHapticsController controller{nullptr};
    bool intensity = false;
    quint16 ids[int(Wave::Count)] = {};   ///< The waveform id of each Wave
};

Device& device()
{
    static Device* d = new Device;   // Never deleted: nothing of WinRT runs at exit
    return *d;
}

/// One worker thread, so the calls keep their order and the GUI never waits.
void post(std::function<void()> job)
{
    static QThreadPool* pool = [] {
        auto* p = new QThreadPool;   // Never deleted: a call that hangs must not hang the exit
        p->setMaxThreadCount(1);
        p->setExpiryTimeout(-1);     // Always the same thread: one COM apartment
        return p;
    }();
    pool->start([job = std::move(job)]() {
        try {
            static thread_local const bool apartment = [] {
                try {
                    winrt::init_apartment(winrt::apartment_type::multi_threaded);
                } catch (...) {
                }
                return true;
            }();
            Q_UNUSED(apartment);
            job();
        } catch (...) {
            // No pen, no haptics, an old Windows, a failed call: no feel, nothing else.
        }
    });
}

void resolve(quint32 id)
{
    Device& d = device();
    if (d.id == id) {
        return;
    }
    d = Device();
    d.id = id;
    try {
        const PenDevice pen = PenDevice::GetFromPointerId(id);
        d.controller = pen ? pen.SimpleHapticsController() : nullptr;   // Throws before Windows 11
        if (d.controller) {
            d.ids[int(Wave::Click)] = KnownSimpleHapticsControllerWaveforms::Click();
            d.ids[int(Wave::Ink)] = KnownSimpleHapticsControllerWaveforms::InkContinuous();
            d.ids[int(Wave::Marker)] = KnownSimpleHapticsControllerWaveforms::MarkerContinuous();
            d.ids[int(Wave::Brush)] = KnownSimpleHapticsControllerWaveforms::BrushContinuous();
            d.ids[int(Wave::Eraser)] = KnownSimpleHapticsControllerWaveforms::EraserContinuous();
            d.intensity = d.controller.IsIntensitySupported();
        }
    } catch (...) {
        d.controller = nullptr;
    }
    s_intensity = d.intensity;
    s_status = int(d.controller ? Status::Vibration : Status::NoVibration);
    static bool logged = false;
    if (!logged) {
        logged = true;
        qDebug() << "Pen vibration: pointer" << id
                 << (d.controller ? "has a haptics controller" : "has no haptics controller")
                 << "intensity:" << d.intensity;
    }
}

void play(const Request& r, bool continuous)
{
    Device& d = device();
    if (!d.controller) {
        return;
    }
    const auto find = [&d](Wave w) -> SimpleHapticsControllerFeedback {
        for (const SimpleHapticsControllerFeedback& f : d.controller.SupportedFeedback()) {
            if (f.Waveform() == d.ids[int(w)]) {
                return f;
            }
        }
        return nullptr;
    };
    SimpleHapticsControllerFeedback f = find(r.wave);
    // Every haptic pen has the ink feel and the click; the eraser feel has no stand-in.
    if (!f && r.wave != Wave::Eraser) {
        f = find(continuous ? Wave::Ink : Wave::Click);
    }
    if (!f) {
        return;
    }
    if (d.intensity) {
        d.controller.SendHapticFeedback(f, r.intensity);
    } else {
        d.controller.SendHapticFeedback(f);
    }
}

} // namespace

void click(Kind kind)
{
    if (!s_near) {
        return;   // The pen is away from the screen: nobody would feel it
    }
    const Request r = decide(kind, DrawingSettings::get());
    if (r.wave != Wave::None) {
        post([r]() { play(r, false); });
    }
}

void startContinuous(ToolType tool, int penType)
{
    // Not once the pen has lifted: the stroke's queued press can come after WM_POINTERUP,
    // and a feel started then would never stop.
    if (!s_contact) {
        return;
    }
    const Request r = decide(Writing, DrawingSettings::get(), tool, penType);
    if (r.wave != Wave::None) {
        s_playing = true;
        post([r]() { play(r, true); });
    }
}

void stop()
{
    if (!s_playing) {
        return;
    }
    s_playing = false;
    post([]() {
        if (device().controller) {
            device().controller.StopFeedback();
        }
    });
}

void setPointerId(quint32 id)
{
    s_near = true;
    if (id != s_pointerId) {
        s_pointerId = id;
        post([id]() { resolve(id); });
    }
}

void penDown(quint32 id)
{
    setPointerId(id);
    s_contact = true;
}

void penUp()
{
    s_contact = false;
    stop();
}

void penLeft()
{
    s_near = false;
    penUp();
}

Status status()
{
    return Status(s_status.load());
}

bool intensitySupported()
{
    return s_intensity;
}

#else // No haptics on this platform or build

void click(Kind) {}
void startContinuous(ToolType, int) {}
void stop() {}
void setPointerId(quint32) {}
void penDown(quint32) {}
void penUp() {}
void penLeft() {}
Status status() { return Status::NotBuilt; }
bool intensitySupported() { return false; }

#endif

} // namespace PenHaptics
