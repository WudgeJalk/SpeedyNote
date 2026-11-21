#pragma once

// ============================================================================
// LaserPointer - the marks of the laser tool
// ============================================================================
// Point mode: one glowing dot where the pen is. Line mode: a glowing trail per
// pen contact. The marks stay while the user keeps pointing; after the pen
// lifts they wait, then all fade out together. Never saved, no undo.
// Coordinates are document coordinates, so the marks stay on the page while
// the view scrolls.
// ============================================================================

#include <QElapsedTimer>
#include <QObject>
#include <QPolygonF>
#include <QRectF>
#include <QTimer>
#include <QVector>

class QColor;
class QPainter;
class QTransform;

class LaserPointer : public QObject {
    Q_OBJECT

public:
    enum Mode { Point = 0, Line = 1 };
    /// Width of a trail's glow on screen. The dot is 1.5 times wider.
    static constexpr qreal GLOW_PX = 14.0;
    /// Room to repaint around a mark (screen px).
    static constexpr qreal MARGIN_PX = GLOW_PX * 0.75 + 2.0;

    explicit LaserPointer(QObject* parent = nullptr);

    /// The pen touches: a fade stops. Line starts a new trail, Point moves the dot.
    /// @return True when marks already there changed look (repaint their bounds).
    bool press(const QPointF& docPt, Mode mode);
    /// The pen moves. Steps shorter than @p minStep are skipped.
    /// @return True with the changed area in @p changed (document coordinates).
    bool move(const QPointF& docPt, qreal minStep, QRectF* changed);
    /// The pen lifts. After @p holdMs (Point: at once) every mark fades over @p fadeMs.
    void release(int holdMs, int fadeMs);
    /// Remove every mark now (no signal: the caller repaints).
    void clear();

    bool isEmpty() const { return m_lines.isEmpty(); }
    bool isFading() const { return m_fading; }
    qreal opacity() const { return m_opacity; }
    int lineCount() const { return static_cast<int>(m_lines.size()); }
    int pointCount() const;
    /// Every mark, in document coordinates, without the glow.
    QRectF bounds() const;

    /// Draw the marks that reach @p viewClip (viewport coordinates).
    void paint(QPainter& painter, const QTransform& docToView, const QRectF& viewClip,
               const QColor& color) const;

signals:
    /// A fade step, or the fade ended and the marks are gone: repaint @p docRect.
    void changed(const QRectF& docRect);

private:
    void onTimer();

    QVector<QPolygonF> m_lines;     ///< Line: one per pen contact. Point: one single point.
    QVector<QRectF> m_lineBounds;   ///< Box of each line (may be 0 x 0)
    Mode m_mode = Line;
    QTimer m_timer;                 ///< The wait, then about 30 frames a second while fading
    QElapsedTimer m_fadeClock;
    int m_fadeMs = 400;
    qreal m_opacity = 1.0;
    bool m_fading = false;
};
