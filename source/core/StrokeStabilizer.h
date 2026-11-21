#pragma once

// ============================================================================
// StrokeStabilizer - "Stroke stabilisation" of the pen window
// ============================================================================
// An exponential moving average over time: each new point pulls the drawn
// point towards the pen by 1 - exp(-dt / tau). Measured in time, not in
// events, so a 60 Hz finger and a 360 Hz pen get the same smoothing.
// O(1) per pen event; at strength 0 the points pass through untouched.
// ============================================================================

#include <QPointF>
#include <QtGlobal>

#include <cmath>

class StrokeStabilizer {
public:
    /// Start a stroke at @p pos. @p strength 0..100 is the lag in ms (0 = off).
    void start(const QPointF& pos, qint64 timestampMs, int strength)
    {
        m_tauMs = qBound(0, strength, 100);
        m_pos = m_raw = pos;
        m_time = timestampMs;
    }

    /// The point to draw for the pen at @p raw.
    QPointF apply(const QPointF& raw, qint64 timestampMs)
    {
        m_raw = raw;
        if (m_tauMs <= 0) {
            return raw;
        }
        const qint64 dt = timestampMs - m_time;
        if (dt > 0) {
            m_time = timestampMs;
            m_pos += (raw - m_pos) * (1.0 - std::exp(-double(dt) / m_tauMs));
        }
        return m_pos;
    }

    bool active() const { return m_tauMs > 0; }
    /// Where the pen really was last; the stroke ends there.
    QPointF rawPosition() const { return m_raw; }

private:
    int m_tauMs = 0;
    QPointF m_pos;
    QPointF m_raw;
    qint64 m_time = 0;
};
