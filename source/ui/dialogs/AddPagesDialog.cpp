#include "AddPagesDialog.h"

#include "PageRangeSelectDialog.h"
#include "../PageThumbnailDelegate.h"
#include "../PageThumbnailModel.h"
#include "../widgets/PaperCombo.h"
#include "DialogSizing.h"
#include "../../core/Document.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScroller>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int kThumbWidth = 120;

QLabel* hint(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet("color: gray; font-size: 11px;");
    return label;
}
} // namespace

AddPagesDialog::AddPagesDialog(Document* doc, int currentPage, QWidget* parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Add pages"));
    const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    QSettings settings("SpeedyNote", "App");
    settings.beginGroup("addPages");

    // --- Left: every page of the note; a tap chooses the place ---
    auto* model = new PageThumbnailModel(this);
    model->setThumbnailWidth(kThumbWidth);
    model->setDevicePixelRatio(devicePixelRatioF());
    model->setDocument(doc);
    auto* delegate = new PageThumbnailDelegate(this);
    delegate->setThumbnailWidth(kThumbWidth);
    delegate->setDarkMode(dark);
    m_pages = new QListView(this);
    m_pages->setModel(model);
    m_pages->setItemDelegate(delegate);
    m_pages->setFlow(QListView::LeftToRight);
    m_pages->setWrapping(true);
    m_pages->setResizeMode(QListView::Adjust);
    m_pages->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pages->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pages->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages->setMinimumWidth(3 * (kThumbWidth + 40));
    QScroller::grabGesture(m_pages->viewport(), QScroller::TouchGesture);   // a finger scrolls
    const int pageCount = doc ? doc->pageCount() : 0;
    const QModelIndex start = model->index(qBound(0, currentPage, qMax(0, pageCount - 1)), 0);
    m_pages->setCurrentIndex(start);
    m_pages->scrollTo(start, QAbstractItemView::PositionAtCenter);

    // --- Right: where, and what ---
    QGroupBox* whereBox = new QGroupBox(tr("Where"), this);
    QVBoxLayout* whereLayout = new QVBoxLayout(whereBox);
    m_below = new QRadioButton(tr("Under the chosen page"), whereBox);
    m_above = new QRadioButton(tr("Above the chosen page"), whereBox);
    (settings.value("below", true).toBool() ? m_below : m_above)->setChecked(true);
    whereLayout->addWidget(m_below);
    whereLayout->addWidget(m_above);
    whereLayout->addWidget(hint(tr("Tap a page on the left to choose it."), whereBox));

    QGroupBox* whatBox = new QGroupBox(tr("What"), this);
    QVBoxLayout* whatLayout = new QVBoxLayout(whatBox);
    m_blank = new QRadioButton(tr("Empty pages"), whatBox);
    m_pdf = new QRadioButton(tr("Pages of a PDF"), whatBox);
    m_blank->setChecked(true);   // a PDF is chosen each time
    whatLayout->addWidget(m_blank);

    m_blankBox = new QWidget(whatBox);
    QFormLayout* blankLayout = new QFormLayout(m_blankBox);
    blankLayout->setContentsMargins(24, 0, 0, 8);
    m_count = new QSpinBox(m_blankBox);
    m_count->setRange(1, 200);
    m_count->setValue(qBound(1, settings.value("count", 1).toInt(), 200));
    blankLayout->addRow(tr("How many:"), m_count);
    whatLayout->addWidget(m_blankBox);

    whatLayout->addWidget(m_pdf);
    m_pdfBox = new QWidget(whatBox);
    QFormLayout* pdfLayout = new QFormLayout(m_pdfBox);
    pdfLayout->setContentsMargins(24, 0, 0, 0);
    QPushButton* pdfButton = new QPushButton(tr("Choose a PDF..."), m_pdfBox);
    m_pdfName = new QLabel(tr("No PDF chosen"), m_pdfBox);
    m_pdfName->setWordWrap(true);
    pdfLayout->addRow(pdfButton, m_pdfName);
    m_pdfRange = new QLineEdit(QStringLiteral("all"), m_pdfBox);
    pdfLayout->addRow(tr("Its pages:"), m_pdfRange);
    pdfLayout->addRow(hint(tr("\"all\", or some pages: 1-5, 8, 12-20."), m_pdfBox));
    QHBoxLayout* ratio = new QHBoxLayout();
    m_pdfGroup = new QSpinBox(m_pdfBox);
    m_pdfGroup->setRange(1, 50);
    m_pdfGroup->setValue(qBound(1, settings.value("pdfGroup", 1).toInt(), 50));
    m_notesBetween = new QSpinBox(m_pdfBox);
    m_notesBetween->setRange(0, 20);
    m_notesBetween->setValue(qBound(0, settings.value("notesBetween", 0).toInt(), 20));
    ratio->addWidget(new QLabel(tr("After every"), m_pdfBox));
    ratio->addWidget(m_pdfGroup);
    ratio->addWidget(new QLabel(tr("PDF pages, add"), m_pdfBox));
    ratio->addWidget(m_notesBetween);
    ratio->addWidget(new QLabel(tr("note pages"), m_pdfBox));
    ratio->addStretch();
    pdfLayout->addRow(ratio);
    whatLayout->addWidget(m_pdfBox);

    // --- The new note pages (the empty ones, and those between PDF pages) ---
    QGroupBox* noteBox = new QGroupBox(tr("The new note pages"), this);
    QFormLayout* noteLayout = new QFormLayout(noteBox);
    m_paper = new QComboBox(noteBox);
    m_paper->addItem(tr("The paper of this note"), QString());
    m_paper->addItem(tr("Plain"), QStringLiteral("plain"));
    PaperCombo::addPapers(m_paper, false);
    const int savedPaper = m_paper->findData(settings.value("paper").toString());
    m_paper->setCurrentIndex(qMax(0, savedPaper));
    noteLayout->addRow(tr("Paper:"), m_paper);
    m_sizeLikeNeighbour = new QCheckBox(tr("Same size as the page before"), noteBox);
    m_sizeLikeNeighbour->setChecked(settings.value("sizeLikeNeighbour", true).toBool());
    noteLayout->addRow(m_sizeLikeNeighbour);
    noteLayout->addRow(hint(tr("A note page next to a PDF page gets the size of that PDF page. "
                               "Off: the page size of this note."), noteBox));
    m_pageNumber = new QCheckBox(tr("Write the page number on each page"), noteBox);
    m_pageNumber->setChecked(settings.value("pageNumber", false).toBool());
    noteLayout->addRow(m_pageNumber);
    m_title = new QCheckBox(tr("Write a title on each page:"), noteBox);
    m_title->setChecked(settings.value("title", false).toBool());
    m_titleText = new QLineEdit(doc ? doc->name : QString(), noteBox);
    noteLayout->addRow(m_title, m_titleText);
    noteLayout->addRow(hint(tr("The number and the title are small text boxes: you can move, change or "
                               "delete them. The number does not change when you move the page later."), noteBox));

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet("font-weight: bold;");
    m_create = new QPushButton(tr("Add"), this);
    m_create->setDefault(true);
    QPushButton* cancel = new QPushButton(tr("Cancel"), this);
    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(m_create);
    buttons->addWidget(cancel);

    // The choices scroll on a small screen; the summary and the buttons stay in view.
    QWidget* choices = new QWidget(this);
    QVBoxLayout* choicesLayout = new QVBoxLayout(choices);
    choicesLayout->setContentsMargins(0, 0, 8, 0);
    choicesLayout->addWidget(whereBox);
    choicesLayout->addWidget(whatBox);
    choicesLayout->addWidget(noteBox);
    choicesLayout->addStretch();
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidget(choices);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(460);
    QVBoxLayout* right = new QVBoxLayout();
    right->addWidget(scroll, 1);
    right->addWidget(m_summary);
    right->addLayout(buttons);
    QHBoxLayout* body = new QHBoxLayout(this);
    body->addWidget(m_pages, 1);
    body->addLayout(right, 1);

    setStyleSheet("QCheckBox, QRadioButton, QPushButton, QComboBox, QAbstractSpinBox, QLineEdit { min-height: 32px; }");

    connect(pdfButton, &QPushButton::clicked, this, &AddPagesDialog::choosePdf);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, [this]() {
        const AddPagesChoice c = choice();
        QSettings s("SpeedyNote", "App");   // remember the last choices
        s.beginGroup("addPages");
        s.setValue("below", c.below);
        s.setValue("count", c.count);
        s.setValue("paper", c.paperLikeNote ? QString() : c.paperId);
        s.setValue("sizeLikeNeighbour", c.sizeLikeNeighbour);
        s.setValue("pageNumber", c.pageNumber);
        s.setValue("title", c.title);
        s.setValue("pdfGroup", c.pdfGroup);
        s.setValue("notesBetween", c.notesBetween);
        accept();
    });
    for (QRadioButton* radio : {m_above, m_below, m_blank, m_pdf}) {
        connect(radio, &QRadioButton::toggled, this, &AddPagesDialog::refresh);
    }
    for (QSpinBox* spin : {m_count, m_pdfGroup, m_notesBetween}) {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AddPagesDialog::refresh);
    }
    connect(m_pdfRange, &QLineEdit::textChanged, this, &AddPagesDialog::refresh);
    connect(m_title, &QCheckBox::toggled, this, &AddPagesDialog::refresh);
    connect(m_pages->selectionModel(), &QItemSelectionModel::currentChanged, this, &AddPagesDialog::refresh);
    // The blue frame of the pictures marks the chosen page.
    model->setCurrentPageIndex(start.row());
    connect(m_pages->selectionModel(), &QItemSelectionModel::currentChanged, model,
            [model](const QModelIndex& current) { model->setCurrentPageIndex(current.row()); });
    refresh();
    DialogSizing::openAtSize(this, QSize(1040, 860));   // capped by the screen
}

void AddPagesDialog::choosePdf()
{
    QSettings settings("SpeedyNote", "App");
    QString dir = settings.value("FileDialogs/lastOpenDirectory").toString();
    if (dir.isEmpty() || !QDir(dir).exists()) dir = QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Add Pages from PDF"), dir,
                                                      tr("PDF Files (*.pdf);;All Files (*)"));
    if (path.isEmpty()) return;
    settings.setValue("FileDialogs/lastOpenDirectory", QFileInfo(path).absolutePath());
    // Opened once here, only to count its pages.
    const std::unique_ptr<Document> pdf = Document::createForPdf(QFileInfo(path).completeBaseName(), path);
    m_pdfPageCount = pdf && pdf->isPdfLoaded() ? pdf->pdfPageCount() : 0;
    m_pdfPath = m_pdfPageCount > 0 ? path : QString();
    m_pdfName->setText(m_pdfPageCount > 0
        ? tr("%1 (%n pages)", "", m_pdfPageCount).arg(QFileInfo(path).fileName())
        : tr("This PDF could not be opened."));
    m_pdf->setChecked(true);
    refresh();
}

QVector<QPair<int, int>> AddPagesDialog::notePlaces(int at, int pdfPages, int group, int between)
{
    // From the last group to the first, so the places of the earlier groups stay.
    QVector<QPair<int, int>> places;
    group = qMax(1, group);
    for (int g = (pdfPages + group - 1) / group; g >= 1 && between > 0; --g) {
        const int index = at + qMin(g * group, pdfPages);
        for (int i = 0; i < between; ++i) {
            places.append({index + i, index + i + (g - 1) * between + 1});
        }
    }
    return places;
}

AddPagesChoice AddPagesDialog::choice() const
{
    AddPagesChoice c;
    c.anchor = qMax(0, m_pages->currentIndex().row());
    c.below = m_below->isChecked();
    c.fromPdf = m_pdf->isChecked();
    c.count = m_count->value();
    c.paperId = m_paper->currentData().toString();
    c.paperLikeNote = c.paperId.isEmpty();
    c.sizeLikeNeighbour = m_sizeLikeNeighbour->isChecked();
    c.pageNumber = m_pageNumber->isChecked();
    c.title = m_title->isChecked() && !m_titleText->text().trimmed().isEmpty();
    c.titleText = m_titleText->text().trimmed();
    c.pdfPath = m_pdfPath;
    c.pdfRange = m_pdfRange->text();
    c.pdfGroup = m_pdfGroup->value();
    c.notesBetween = m_notesBetween->value();
    return c;
}

void AddPagesDialog::refresh()
{
    const AddPagesChoice c = choice();
    m_blankBox->setEnabled(!c.fromPdf);
    m_pdfBox->setEnabled(c.fromPdf);
    m_titleText->setEnabled(m_title->isChecked());
    const QString place = c.below ? tr("under page %1").arg(c.anchor + 1) : tr("above page %1").arg(c.anchor + 1);
    bool ok = true;
    QString text;
    if (!c.fromPdf) {
        text = tr("%n empty page(s) %1.", "", c.count).arg(place);
    } else {
        const int pdfPages = static_cast<int>(PageRangeSelectDialog::parseRange(c.pdfRange, m_pdfPageCount).size());
        ok = pdfPages > 0;
        if (m_pdfPath.isEmpty()) {
            text = tr("Choose a PDF first.");
        } else if (!ok) {
            text = tr("No page of the PDF is in \"%1\".").arg(c.pdfRange);
        } else {
            const int notes = c.notesBetween > 0 ? ((pdfPages + c.pdfGroup - 1) / c.pdfGroup) * c.notesBetween : 0;
            text = notes > 0 ? tr("%1 PDF pages and %2 note pages, %3.").arg(pdfPages).arg(notes).arg(place)
                             : tr("%1 PDF pages %2.").arg(pdfPages).arg(place);
        }
    }
    m_summary->setText(text);
    m_create->setEnabled(ok);
}
