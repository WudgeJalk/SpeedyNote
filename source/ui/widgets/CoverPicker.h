#pragma once

// ============================================================================
// CoverPicker - choose a cover (Step N3)
// ============================================================================
// A small window: a preview, the designs (and "No cover"), the colours, "Show
// the title" and "Picture...". Used by the New note window and by "Cover..."
// of a notebook in the Library. The chosen picture is returned as an absolute
// path in "image"; the caller copies it (CoverPainter::adoptPicture).
// ============================================================================

#include <QDialog>
#include <QJsonObject>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;

class CoverPicker : public QDialog {
    Q_OBJECT

public:
    /// baseDir: where a relative "image" of cover lies.
    CoverPicker(const QJsonObject& cover, const QString& title, const QString& baseDir,
                QWidget* parent = nullptr);

    QJsonObject cover() const;   ///< empty: no cover

    /// Shows the window; true with *cover on OK.
    static bool edit(QWidget* parent, QJsonObject* cover, const QString& title, const QString& baseDir);

    /// Tests: choose without clicks.
    void setDesign(const QString& design);
    void setPicture(const QString& path);

private:
    void choose(QListWidgetItem* item);
    bool pickPicture();
    void refresh();

    QJsonObject m_cover;         ///< the design and colour even while "No cover" is picked
    bool m_none = false;
    QString m_title;
    QString m_baseDir;
    QWidget* m_preview = nullptr;
    QListWidget* m_designs = nullptr;
    QButtonGroup* m_colours = nullptr;
    QCheckBox* m_showTitle = nullptr;
    QLabel* m_pictureName = nullptr;
};
