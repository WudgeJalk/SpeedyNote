#pragma once

// ============================================================================
// LibraryPlacePicker - where a note goes (Step N1)
// ============================================================================
// A button that shows a folder as "Notebook › Section" when it lies in the
// Library, else as its path. A tap opens a menu of the Library's notebooks and
// sections (library/notebooks + LibraryTree), plus "Another folder..." on the
// desktop. choose() is the same menu without the button.
// ============================================================================

#include <QPushButton>

class LibraryPlacePicker : public QPushButton {
    Q_OBJECT

public:
    explicit LibraryPlacePicker(QWidget* parent = nullptr);

    QString place() const { return m_place; }
    void setPlace(const QString& folder);

    /// "HEIG26 › 04_Math_TIN" inside a Library notebook, else the folder's path.
    static QString describe(const QString& folder);
    /// The menu alone, opened at globalPos. Returns the chosen folder, or empty.
    static QString choose(QWidget* parent, const QPoint& globalPos, const QString& current);

    QSize sizeHint() const override;

signals:
    void placeChanged(const QString& folder);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshText();

    QString m_place;
};
