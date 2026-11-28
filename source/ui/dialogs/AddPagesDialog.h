#pragma once

#include <QDialog>
#include <QPair>
#include <QString>
#include <QVector>

class Document;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QRadioButton;
class QSpinBox;

/// What the "Add pages" window was asked to do. MainWindow::showAddPagesDialog() does it.
struct AddPagesChoice {
    int anchor = 0;              ///< The page chosen in the pictures
    bool below = true;           ///< New pages go under the chosen page (else above it)
    bool fromPdf = false;        ///< Pages of a PDF (else empty note pages)

    int count = 1;               ///< Empty pages to add
    bool paperLikeNote = true;   ///< The note's own paper (else paperId)
    QString paperId;             ///< A Paper id; "plain" is no paper
    bool sizeLikeNeighbour = true;   ///< A note page takes the size of the page before it
    bool pageNumber = false;     ///< A small text box with the page number on each new note page
    bool title = false;          ///< A small text box with titleText on each new note page
    QString titleText;

    QString pdfPath;
    QString pdfRange;            ///< "all", or "1-5, 8" (PageRangeSelectDialog::parseRange)
    int pdfGroup = 1;            ///< After every pdfGroup PDF pages...
    int notesBetween = 0;        ///< ...this many note pages (0: none)
};

/**
 * @brief The one window to add pages: the "+" of the Pages panel.
 *
 * Left: the pictures of every page of the note; a tap chooses where the new
 * pages go. Right: empty pages (how many, which paper, number and title) or
 * the pages of a PDF (which ones, and note pages between them at a ratio).
 * The last choices are kept in the settings ("addPages/...").
 */
class AddPagesDialog : public QDialog {
    Q_OBJECT
    friend class ToolbarButtonTests;

public:
    AddPagesDialog(Document* doc, int currentPage, QWidget* parent = nullptr);
    AddPagesChoice choice() const;
    /**
     * @brief Where the note pages go between PDF pages put in at @p at.
     * @return For each note page, in the order to insert them: the index to
     *         insert at, and the page number (from 1) it has at the end.
     */
    static QVector<QPair<int, int>> notePlaces(int at, int pdfPages, int group, int between);

private:
    void choosePdf();
    void refresh();   ///< Enable what applies, and say in one line what Create will do

    Document* m_doc = nullptr;
    QListView* m_pages = nullptr;
    QRadioButton* m_above = nullptr;
    QRadioButton* m_below = nullptr;
    QRadioButton* m_blank = nullptr;
    QRadioButton* m_pdf = nullptr;
    QWidget* m_blankBox = nullptr;
    QWidget* m_pdfBox = nullptr;
    QSpinBox* m_count = nullptr;
    QComboBox* m_paper = nullptr;
    QCheckBox* m_sizeLikeNeighbour = nullptr;
    QCheckBox* m_pageNumber = nullptr;
    QCheckBox* m_title = nullptr;
    QLineEdit* m_titleText = nullptr;
    QLabel* m_pdfName = nullptr;
    QLineEdit* m_pdfRange = nullptr;
    QSpinBox* m_pdfGroup = nullptr;
    QSpinBox* m_notesBetween = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_create = nullptr;
    QString m_pdfPath;
    int m_pdfPageCount = 0;
};
