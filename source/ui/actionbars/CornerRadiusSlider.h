#pragma once

// ============================================================================
// CornerRadiusSlider - "Sharp ... Round" for the corners of a selected shape
// ============================================================================
// Sits in the corners menu of the selection panel. Pressing it begins, moving
// it previews (the shape is redrawn), releasing it commits: one undo step per
// drag. A key or the wheel commits after a short pause.
// ============================================================================

#include <QWidget>

class QSlider;
class QTimer;

class CornerRadiusSlider : public QWidget {
    Q_OBJECT

public:
    explicit CornerRadiusSlider(QWidget* parent = nullptr);

    /// Show @p fraction (0 sharp .. 1 round) without a signal.
    void setFraction(qreal fraction);
    /// Commit a change still open (the menu closes).
    void finish();
    QSlider* slider() const { return m_slider; }

signals:
    void begin();
    void preview(qreal fraction);
    void commit();

private:
    QSlider* m_slider = nullptr;
    QTimer* m_pause = nullptr;   ///< A key or the wheel: commit once it stops
    bool m_open = false;         ///< begin() sent, commit() not yet
    bool m_quiet = false;        ///< setFraction() is moving it
};
