#pragma once

// ============================================================================
// NewNoteDialog - the "New note" window (Step N1)
// ============================================================================
// Every way to make a note comes here (MainWindow::newNote): the title, the
// page size, paper colour and orientation, paged or endless, a gallery of the
// papers drawn by code (core/Paper) and of your own papers, and the place in
// the Library or another folder. MainWindow::createNote then saves the note at
// that place at once.
//
// Settings ("SpeedyNote"/"App"):
//   newNote/ask          show this window (false: a new note uses the last choices)
//   newNote/landscape    last orientation
//   newNote/edgeless     last mode of "New note with options..."
//   newNote/place        last folder used
//   newNote/customImage  last paper of your own (a PNG in Paper::userPapersDir())
//   newNote/cover        last cover (JSON, see CoverPainter), not kept with a picture
// and, shared with Settings > Page: page/width, page/height, background/color,
// background/gridColor, background/paper + background/type.
// ============================================================================

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "../../core/Paper.h"

class Document;
class LengthSpinBox;
class LibraryPlacePicker;
class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QWidget;

/// What a new note is made of.
struct NewNoteSpec {
    QString name;          ///< file name without ".snb"
    QString folder;        ///< where the .snb goes
    QSizeF sizePx;         ///< page size as listed (portrait for presets), 96 per inch
    bool landscape = false;
    QColor paperColor;
    QColor lineColor;
    QString paperId;       ///< a Paper id; "plain" = no lines; "grid"/"lines" = the classic
                           ///< background of Settings > Page, at `spacing`
    int spacing = 32;      ///< px between the lines of "grid" or "lines"
    QString customImage;   ///< a paper of your own: a PNG path (paged notes only)
    bool edgeless = false;
    QJsonObject cover;     ///< a note cover (Step N3); empty = none; "image" absolute until apply()

    QSizeF pageSize() const { return landscape ? sizePx.transposed() : sizePx; }
};

namespace NewNote {
/// The last choices, as the fast way ("do not ask") uses them.
NewNoteSpec defaults(bool edgeless, const QString& folderHint = QString());
/// Keeps the choices of spec as the next defaults.
void remember(const NewNoteSpec& spec);
/// newNote/place, else the last save folder, else Documents/SpeedyNote
/// (the notebooks folder of the app on a phone or tablet).
QString defaultPlace();
/// The line colour that suits a paper colour: the stored one when it fits.
QColor lineColorFor(const QColor& paper, const QColor& stored);
/// folder/<name>.snb, or "<name> (2).snb" ... when taken.
QString bundlePathFor(const NewNoteSpec& spec);
/// "grid" or "lines": the classic background of Settings > Page, not a Paper.
bool isClassic(const QString& paperId);
/// Puts spec on doc's defaults, its first page and its cover. Not an edit: an
/// untouched new note stays unmodified. False with *error when your own paper
/// or the cover's picture could not be copied into the note (the note then
/// keeps plain paper, or no cover).
bool apply(Document* doc, const NewNoteSpec& spec, QString* error = nullptr);
}

class NewNoteDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewNoteDialog(const NewNoteSpec& start, QWidget* parent = nullptr);

    NewNoteSpec spec() const;
    bool dontAskAgain() const;

    /// Shows the window, unless newNote/ask is off and !forceShow. True with
    /// *spec when a note must be made.
    static bool ask(QWidget* parent, bool edgeless, const QString& folderHint,
                    NewNoteSpec* spec, bool forceShow = false);

    /// A Paper id or a path in the user papers folder. Tests use it too.
    void selectPaper(const QString& idOrPath);
    void setEdgeless(bool edgeless);
    void setLandscape(bool landscape);
    /// Tests: lay the next windows out as on a phone screen.
    static void setNarrowForTests(bool narrow);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum TileKind { PaperTile, UserTile, AddTile };

    QWidget* buildOptions();
    QWidget* buildGallery();
    QListWidget* addGroup(QWidget* body, const QString& title);
    void addTile(QListWidget* list, TileKind kind, const QString& key, const QString& text);
    void choose(QListWidgetItem* item);
    void showChosen();               ///< scrolls the gallery to the chosen tile
    void addUserPaper();
    void sizeChanged();
    void refresh();
    void refreshTiles();
    void fitList(QListWidget* list) const;
    void validate();
    QSizeF chosenSizePx() const;     ///< as listed, before the orientation
    QSizeF pagePx() const;           ///< with the orientation
    QPixmap tilePixmap(TileKind kind, const QString& key) const;
    void paintPreview(QPainter& p, const QRectF& area) const;
    const Paper::Drawing& drawing(const QString& id, const QSizeF& size, bool endless) const;

    NewNoteSpec m_start;
    bool m_dark = false;
    bool m_narrow = false;           ///< a phone: the parts stack in one column
    QString m_paper = QStringLiteral("plain");   ///< a Paper id or a user paper path
    QString m_pagedPaper;                        ///< kept while Endless is chosen
    QColor m_paperColor;
    QColor m_storedLine;

    QLineEdit* m_title = nullptr;
    QLabel* m_titleError = nullptr;
    QWidget* m_coverSlot = nullptr;   ///< the cover (Step N3)
    QPushButton* m_coverButton = nullptr;
    QJsonObject m_cover;              ///< empty: no cover
    void refreshCover();
    QWidget* m_preview = nullptr;
    QLabel* m_captionName = nullptr;
    QLabel* m_captionSize = nullptr;
    QComboBox* m_size = nullptr;
    QWidget* m_customRow = nullptr;
    LengthSpinBox* m_customW = nullptr;
    LengthSpinBox* m_customH = nullptr;
    QPushButton* m_portrait = nullptr;
    QPushButton* m_landscape = nullptr;
    QPushButton* m_paged = nullptr;
    QPushButton* m_endless = nullptr;
    QButtonGroup* m_swatches = nullptr;
    QAbstractButton* m_customSwatch = nullptr;
    QVector<QListWidget*> m_lists;
    QListWidget* m_userList = nullptr;
    LibraryPlacePicker* m_place = nullptr;
    QCheckBox* m_dontAsk = nullptr;
    QPushButton* m_create = nullptr;

    QPoint m_pressPos;
    int m_pressScroll = 0;
    bool m_dragging = false;
    QScrollArea* m_galleryArea = nullptr;
    QSize m_minGrid;                 ///< smallest tile cell; fitList() spreads the cells
    mutable QHash<QString, Paper::Drawing> m_drawings;
};
