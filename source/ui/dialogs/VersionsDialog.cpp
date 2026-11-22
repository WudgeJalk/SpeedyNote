#include "VersionsDialog.h"

#include "../../core/Document.h"
#include "../../core/DocumentManager.h"
#include "../../core/NoteVersions.h"
#include "../../core/Page.h"

#include <QApplication>
#include <QBoxLayout>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QtMath>

namespace {
constexpr int kPreviewWidth = 240;
constexpr int kPreviewHeight = 320;
// A listed time is a minute; everything kept during that minute belongs to it.
constexpr qint64 kMinuteEnd = 59;
}

VersionsDialog::VersionsDialog(Document* doc, int pageIndex, QWidget* parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Restore an Earlier Version"));
    m_bundle = doc ? doc->bundlePath() : QString();
    if (!m_bundle.isEmpty()) {
        m_times = NoteVersions::versionTimes(m_bundle);
    }
    if (doc && !doc->isEdgeless()) {
        const QString uuid = doc->pageUuidAt(pageIndex);
        if (!uuid.isEmpty()) m_pageFile = QStringLiteral("pages/") + uuid + QStringLiteral(".json");
    }

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Before a page is saved over, its earlier copy is kept. Pick a time: "
                                "the note opens as it was then, as a new note. Nothing here is "
                                "overwritten."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Side by side, or stacked on a narrow screen.
    const QScreen* screen = QGuiApplication::primaryScreen();
    const bool narrow = screen && screen->availableGeometry().width() < 600;
    auto* row = new QBoxLayout(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(220);
    const QLocale locale;
    for (qint64 t : m_times) {
        m_list->addItem(locale.toString(QDateTime::fromSecsSinceEpoch(t),
                                        QStringLiteral("dddd d MMMM yyyy, HH:mm")));
    }
    if (m_times.isEmpty()) {
        auto* none = new QListWidgetItem(tr("No earlier version yet. Versions are kept as you "
                                            "change pages."), m_list);
        none->setFlags(Qt::NoItemFlags);
    }
    row->addWidget(m_list, 1);

    m_preview = new QLabel(this);
    m_preview->setFixedSize(kPreviewWidth, kPreviewHeight);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setWordWrap(true);
    m_preview->setFrameShape(QFrame::StyledPanel);
    row->addWidget(m_preview, 0, Qt::AlignCenter);
    layout->addLayout(row, 1);

    m_warning = new QLabel(tr("Some pages are newer than this time: no earlier copy of them was "
                              "kept, so the copy has them as they are now."), this);
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet(QStringLiteral("color: #b45309;"));
    m_warning->hide();
    layout->addWidget(m_warning);

    auto* buttons = new QDialogButtonBox(this);
    m_open = buttons->addButton(tr("Open as a Copy"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    m_open->setEnabled(false);
    connect(m_open, &QPushButton::clicked, this, &VersionsDialog::openCopy);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, &VersionsDialog::showTime);
    if (!m_times.isEmpty()) {
        selectRow(0);
    } else {
        m_preview->setText(tr("No preview"));
    }
}

void VersionsDialog::selectRow(int row)
{
    m_list->setCurrentRow(row);
    showTime(row);
}

void VersionsDialog::showTime(int row)
{
    if (row < 0 || row >= m_times.size()) {
        m_open->setEnabled(false);
        m_warning->hide();
        return;
    }
    const qint64 at = m_times[row] + kMinuteEnd;
    const NoteVersions::Plan plan = NoteVersions::planAt(m_bundle, at);
    m_open->setEnabled(!plan.manifest.isEmpty());
    m_warning->setVisible(plan.someNewer);

    if (m_pageFile.isEmpty()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("No preview for an endless note."));
        return;
    }
    bool newer = false;
    const QString file = NoteVersions::fileAt(m_bundle, m_pageFile, at, &newer);
    QFile in(file);
    std::unique_ptr<Page> page;
    if (!file.isEmpty() && !newer && in.open(QIODevice::ReadOnly)) {
        page = Page::fromJson(QJsonDocument::fromJson(in.readAll()).object());
    }
    if (!page) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("This page has no copy from that time."));
        return;
    }
    page->loadImages(m_bundle);

    const QSizeF size = page->size.isEmpty() ? QSizeF(816, 1056) : page->size;
    const qreal scale = qMin((kPreviewWidth - 8) / size.width(), (kPreviewHeight - 8) / size.height());
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(qCeil(size.width() * scale * dpr), qCeil(size.height() * scale * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(scale, scale);
    QPixmap pdf;
    if (page->backgroundType == Page::BackgroundType::PDF && page->pdfPageNumber >= 0
        && m_doc->providerForSource(page->pdfSourceId)) {
        const QImage image = m_doc->renderPdfPageToImage(page->pdfSourceId, page->pdfPageNumber,
                                                         qMin(150.0, 72.0 * scale * dpr));
        if (!image.isNull()) pdf = QPixmap::fromImage(image);
    }
    page->render(painter, pdf.isNull() ? nullptr : &pdf, 1.0);
    painter.end();
    m_preview->setPixmap(pixmap);
}

void VersionsDialog::openCopy()
{
    const int row = m_list->currentRow();
    if (!m_doc || row < 0 || row >= m_times.size()) return;

    const QString when = QLocale().toString(QDateTime::fromSecsSinceEpoch(m_times[row]),
                                            QStringLiteral("d MMM yyyy HH:mm"));
    const QString dest = DocumentManager::newTempBundlePath(m_doc->isEdgeless());
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = NoteVersions::buildCopy(m_bundle, m_times[row] + kMinuteEnd, dest,
                                            tr("%1 (%2)").arg(m_doc->displayName(), when), &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, windowTitle(), tr("The copy could not be made. %1").arg(error));
        return;
    }
    m_copyPath = dest;
    accept();
}
