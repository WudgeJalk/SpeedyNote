#include "NewNoteDialog.h"

#include "DialogSizing.h"
#include "../widgets/CoverPicker.h"
#include "../widgets/LengthSpinBox.h"
#include "../widgets/LibraryPlacePicker.h"
#include "../../compat/qt_compat.h"
#include "../../core/CoverPainter.h"
#include "../../core/Document.h"
#include "../../core/LibraryTree.h"
#include "../../core/Page.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QConicalGradient>
#include <QDate>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QtMath>
#include <functional>

#ifdef Q_OS_HARMONY
#include "../../harmony/HarmonyEnvironment.h"
#endif

namespace {

constexpr int kTileW = 72, kTileH = 96;   // the page inside a gallery tile
constexpr int kTileMargin = 6;            // room for its shadow
// Tiles show each paper on a page this tall (A5): on the real page size a
// 5 mm grid or a staff would be finer than a pixel at tile size.
constexpr qreal kTilePagePx = 560;
constexpr qreal kTileDetail = 0.6;        // keep finer lines than on screen (Paper::paintDrawing)
constexpr int kLeftWidth = 240;
constexpr int kKeyRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 1;

struct Theme {
    QColor bg, card, input, border, text, text2, disabled, accent, accentText, accentHover;
    QColor tint, hover, segBg, segOn, handle;
};

Theme themeFor(bool dark)
{
    if (dark) {
        return {QColor("#1e1e1e"), QColor("#252525"), QColor("#2d2d2d"), QColor("#3a3a3a"),
                QColor("#e0e0e0"), QColor("#9e9e9e"), QColor("#666666"),
                QColor("#8ab4f8"), QColor("#1e1e1e"), QColor("#a8c7fa"),
                QColor(138, 180, 248, 56), QColor(255, 255, 255, 16), QColor("#1e1e1e"),
                QColor("#3d3d3d"), QColor(255, 255, 255, 50)};
    }
    return {QColor("#f5f5f5"), QColor("#ffffff"), QColor("#ffffff"), QColor("#e0e0e0"),
            QColor("#333333"), QColor("#666666"), QColor("#aaaaaa"),
            QColor("#1a73e8"), QColor("#ffffff"), QColor("#1765cc"),
            QColor(66, 133, 244, 36), QColor(0, 0, 0, 13), QColor("#f0f0f0"),
            QColor("#ffffff"), QColor(0, 0, 0, 50)};
}

QString css(const QColor& c)
{
    return c.alpha() == 255 ? c.name()
                            : QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString iconPath(const char* name, bool dark)
{
    return QStringLiteral(":/resources/icons/%1%2.png")
        .arg(QLatin1String(name), dark ? QStringLiteral("_reversed") : QString());
}

// In the style of the launcher (resources/styles/launcher*.qss). Set on the
// root widget only, so boxes opened from here keep the app's own look.
QString dialogStyleSheet(bool dark)
{
    const Theme t = themeFor(dark);
    QString s = QStringLiteral(R"(
QWidget#NNRoot { background: @bg; }
QLabel { color: @text; }
QLabel#NNCaption { color: @text2; font-size: 12px; font-weight: 600; }
QLabel#NNPaperName { color: @text; font-size: 14px; font-weight: 600; }
QLabel#NNPaperSize { color: @text2; font-size: 12px; }
QLabel#NNError { color: #d93025; font-size: 12px; }
QFrame#NNCard { background: @card; border: 1px solid @border; border-radius: 12px; }
QLineEdit { background: @input; color: @text; border: 1px solid @border; border-radius: 8px;
            padding: 7px 10px; font-size: 14px; selection-background-color: @tint; selection-color: @text; }
QLineEdit:focus { border-color: @accent; }
QComboBox { background: @input; color: @text; border: 1px solid @border; border-radius: 8px;
            padding: 6px 10px; font-size: 13px; min-width: 170px; }
QComboBox:focus, QComboBox:on { border-color: @accent; }
QComboBox:disabled, QSpinBox:disabled { color: @disabled; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox::down-arrow { image: url(@arrow); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: @card; color: @text; border: 1px solid @border;
            selection-background-color: @tint; selection-color: @text; outline: 0; }
QSpinBox { background: @input; color: @text; border: 1px solid @border; border-radius: 8px;
           padding: 5px 8px; font-size: 13px; }
QSpinBox:focus { border-color: @accent; }
QFrame#NNSeg { background: @segBg; border: 1px solid @border; border-radius: 9px; }
QPushButton#NNSegButton { background: transparent; color: @text2; border: none; border-radius: 7px;
            padding: 6px 14px; font-size: 13px; }
QPushButton#NNSegButton:hover { color: @text; }
QPushButton#NNSegButton:checked { background: @segOn; color: @text; font-weight: 600; }
QPushButton#NNSegButton:disabled { color: @disabled; }
QToolButton#NNGroup { background: transparent; border: none; color: @text; font-size: 14px;
            font-weight: 600; padding: 8px 2px 4px 2px; }
QListWidget#NNTiles { background: transparent; border: none; outline: 0; color: @text2; font-size: 12px; }
QListWidget#NNTiles::item { border: 2px solid transparent; border-radius: 10px; padding: 2px; }
QListWidget#NNTiles::item:hover { background: @hover; }
QListWidget#NNTiles::item:selected { background: @tint; border-color: @accent; color: @text; }
QListWidget#NNTiles::item:disabled { color: @disabled; }
QPushButton#LibraryPlacePicker { background: @input; color: @text; border: 1px solid @border;
            border-radius: 8px; padding: 7px 34px 7px 10px; text-align: left; font-size: 13px; }
QPushButton#LibraryPlacePicker:hover { border-color: @accent; }
QPushButton#NNButton { background: @input; color: @text; border: 1px solid @border; border-radius: 8px;
            padding: 8px 18px; font-size: 13px; }
QPushButton#NNButton:hover { background: @hover; }
QPushButton#NNCoverButton { background: @input; color: @text; border: 1px solid @border; border-radius: 8px;
            padding: 5px 10px; text-align: left; font-size: 13px; }
QPushButton#NNCoverButton:hover { border-color: @accent; }
QPushButton#NNCreate { background: @accent; color: @accentText; border: none; border-radius: 8px;
            padding: 8px 24px; font-size: 13px; font-weight: 600; }
QPushButton#NNCreate:hover { background: @accentHover; }
QPushButton#NNCreate:disabled { background: @border; color: @text2; }
QCheckBox { color: @text; font-size: 13px; spacing: 8px; }
QScrollArea#NNGallery { background: transparent; border: none; }
QWidget#NNGalleryBody { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: @handle; border-radius: 3px; min-height: 40px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; height: 0px; }
)");
    const std::pair<const char*, QString> tokens[] = {
        {"@accentText", css(t.accentText)}, {"@accentHover", css(t.accentHover)},
        {"@accent", css(t.accent)}, {"@bg", css(t.bg)}, {"@card", css(t.card)},
        {"@input", css(t.input)}, {"@border", css(t.border)}, {"@text2", css(t.text2)},
        {"@text", css(t.text)}, {"@disabled", css(t.disabled)}, {"@tint", css(t.tint)},
        {"@hover", css(t.hover)}, {"@segBg", css(t.segBg)}, {"@segOn", css(t.segOn)},
        {"@handle", css(t.handle)}, {"@arrow", iconPath("down_arrow", dark)},
    };
    for (const auto& [token, value] : tokens) s.replace(QLatin1String(token), value);
    return s;
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("NNCaption"));
    return label;
}

// Two buttons in one rounded frame; the chosen one is lit.
QFrame* segmented(QPushButton* a, QPushButton* b, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("NNSeg"));
    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(2);
    for (QPushButton* button : {a, b}) {
        button->setParent(frame);
        button->setObjectName(QStringLiteral("NNSegButton"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        row->addWidget(button);
    }
    frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return frame;
}

QIcon pageIcon(bool landscape, const QColor& color)
{
    QPixmap pm(32, 32);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.4));
    const QSizeF s = landscape ? QSizeF(13, 9.5) : QSizeF(9.5, 13);
    p.drawRoundedRect(QRectF(QPointF(8 - s.width() / 2, 8 - s.height() / 2), s), 1.5, 1.5);
    return QIcon(pm);
}

// A round colour button. The custom one shows a colour wheel around its colour.
class Swatch : public QAbstractButton {
public:
    Swatch(const QColor& color, bool custom, const QColor& ring, const QColor& edge, QWidget* parent)
        : QAbstractButton(parent), m_color(color), m_custom(custom), m_ring(ring), m_edge(edge)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(sizeHint());
    }
    QSize sizeHint() const override { return {34, 34}; }
    QColor color() const { return m_color; }
    void setColor(const QColor& color) { m_color = color; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF disc = QRectF(rect()).adjusted(5, 5, -5, -5);
        if (isChecked() || hasFocus()) {
            p.setPen(QPen(isChecked() ? m_ring : m_edge, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
        }
        p.setPen(Qt::NoPen);
        if (m_custom) {
            QConicalGradient wheel(disc.center(), 90);
            for (int i = 0; i <= 6; ++i) wheel.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 190, 235));
            p.setBrush(wheel);
            p.drawEllipse(disc);
            p.setBrush(m_color.isValid() ? m_color : QColor(Qt::white));
            p.setPen(QPen(m_edge, 1));
            p.drawEllipse(disc.adjusted(5, 5, -5, -5));
            return;
        }
        p.setPen(QPen(m_edge, 1));
        p.setBrush(m_color);
        p.drawEllipse(disc);
    }

private:
    QColor m_color;
    bool m_custom;
    QColor m_ring, m_edge;
};

// The live picture of the page in the left column.
class Preview : public QWidget {
public:
    std::function<void(QPainter&, const QRectF&)> paintFn;
    explicit Preview(QWidget* parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    QSize sizeHint() const override { return {208, 318}; }   // an A4 page across the column
    QSize minimumSizeHint() const override { return {120, 150}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (paintFn) paintFn(p, QRectF(rect()));
    }
};

bool isUserPaper(const QString& key) { return key.contains(QLatin1Char('/')); }

// The paper an endless canvas can take for a page paper: the same kind of lines.
QString endlessPaper(const QString& id)
{
    if (NewNote::isClassic(id)) return id;   // the classic grid and lines go on forever
    const Paper::PaperDef* def = Paper::find(id);
    if (def && def->endless) return id;
    if (def && def->fallback == Page::BackgroundType::Lines) return QStringLiteral("lined-wide");
    if (def && def->fallback == Page::BackgroundType::Grid) return QStringLiteral("squared");
    return QStringLiteral("plain");
}

QString classicName(const QString& id, int spacing)
{
    return id == QLatin1String("grid")
        ? QCoreApplication::translate("NewNoteDialog", "Grid, %1 px").arg(spacing)
        : QCoreApplication::translate("NewNoteDialog", "Lines, %1 px").arg(spacing);
}

// The classic grid or lines in page pixels (the painter maps the page), one
// screen pixel wide, as the page draws them.
void paintClassic(QPainter& p, const QString& id, int spacing, const QSizeF& page, const QColor& line)
{
    const qreal step = qMax(4, spacing);
    p.setPen(QPen(line, 0));
    for (qreal y = step; y < page.height(); y += step) p.drawLine(QPointF(0, y), QPointF(page.width(), y));
    if (id != QLatin1String("grid")) return;
    for (qreal x = step; x < page.width(); x += step) p.drawLine(QPointF(x, 0), QPointF(x, page.height()));
}

QRectF fitted(const QSizeF& size, const QRectF& box)
{
    const QSizeF s = size.scaled(box.size(), Qt::KeepAspectRatio);
    return QRectF(box.center() - QPointF(s.width() / 2, s.height() / 2), s);
}

void paintShadow(QPainter& p, const QRectF& r, qreal radius, bool dark)
{
    p.setPen(Qt::NoPen);
    for (int i = 3; i >= 1; --i) {
        p.setBrush(QColor(0, 0, 0, dark ? 34 : 9 + 5 * (3 - i)));
        p.drawRoundedRect(r.adjusted(-i, -i + 1.5, i, i + 1.5), radius + i, radius + i);
    }
}

} // namespace

// ============================================================================
// NewNote: the choices, without the window
// ============================================================================

namespace NewNote {

QString defaultPlace()
{
    QSettings settings("SpeedyNote", "App");
    const QString last = settings.value("newNote/place").toString();
    if (!last.isEmpty() && QFileInfo(last).isDir()) return QDir::cleanPath(last);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/notebooks");
#else
    const QString saved = settings.value("FileDialogs/lastSaveDirectory").toString();
    if (!saved.isEmpty() && QFileInfo(saved).isDir()) return QDir::cleanPath(saved);
#ifdef Q_OS_HARMONY
    return HarmonyEnvironment::writableDocumentsRoot() + QStringLiteral("/SpeedyNote");
#else
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/SpeedyNote");
#endif
#endif
}

QColor lineColorFor(const QColor& paper, const QColor& stored)
{
    // Paper's own rule: a grid colour darker than 96 belongs to dark paper.
    const bool darkPaper = paper.lightness() < 128;
    if (stored.isValid() && (stored.lightness() < 96) == darkPaper) return stored;
    return darkPaper ? QColor("#404040") : QColor("#c8c8c8");
}

NewNoteSpec defaults(bool edgeless, const QString& folderHint)
{
    QSettings settings("SpeedyNote", "App");
    const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    NewNoteSpec spec;
    spec.edgeless = edgeless;
    // ISO date: the locale's short date may hold a '/', which a file name cannot.
    spec.name = QCoreApplication::translate("NewNoteDialog", "Note %1")
                    .arg(QDate::currentDate().toString(Qt::ISODate));
    spec.folder = folderHint.isEmpty() ? defaultPlace() : QDir::cleanPath(folderHint);
    spec.sizePx = QSizeF(settings.value("page/width", 816).toReal(),
                         settings.value("page/height", 1056).toReal());
    if (spec.sizePx.width() < 100 || spec.sizePx.height() < 100) spec.sizePx = QSizeF(816, 1056);
    spec.landscape = settings.value("newNote/landscape", false).toBool();
    spec.paperColor = QColor(settings.value("background/color", dark ? "#2b2b2b" : "#ffffff").toString());
    if (!spec.paperColor.isValid()) spec.paperColor = Qt::white;
    spec.lineColor = lineColorFor(spec.paperColor,
        QColor(settings.value("background/gridColor", dark ? "#404040" : "#c8c8c8").toString()));
    // The paper of new notes is shared with Settings > Page. Without one, its
    // classic background stays, at its own spacing (rule 3: never swapped silently).
    spec.paperId = settings.value("background/paper").toString();
    if (!Paper::find(spec.paperId)) {
        const int type = settings.value("background/type", static_cast<int>(Page::BackgroundType::Grid)).toInt();
        if (type == static_cast<int>(Page::BackgroundType::Grid)) {
            spec.paperId = QStringLiteral("grid");
            spec.spacing = settings.value("background/gridSpacing", 32).toInt();
        } else if (type == static_cast<int>(Page::BackgroundType::Lines)) {
            spec.paperId = QStringLiteral("lines");
            spec.spacing = settings.value("background/lineSpacing", 32).toInt();
        } else {
            spec.paperId = QStringLiteral("plain");
        }
    }
    const QString mine = settings.value("newNote/customImage").toString();
    if (!mine.isEmpty() && QFileInfo::exists(mine)) spec.customImage = mine;
    spec.cover = QJsonDocument::fromJson(settings.value("newNote/cover").toByteArray()).object();
    return spec;
}

void remember(const NewNoteSpec& spec)
{
    QSettings settings("SpeedyNote", "App");
    settings.setValue("newNote/place", spec.folder);
    settings.setValue("newNote/edgeless", spec.edgeless);
    settings.setValue("background/color", spec.paperColor.name());
    settings.setValue("background/gridColor", spec.lineColor.name());
    // The design and colour of the cover come back next time; a photo belongs to one note.
    if (spec.cover.isEmpty() || spec.cover.contains("image"))
        settings.remove("newNote/cover");
    else
        settings.setValue("newNote/cover", QJsonDocument(spec.cover).toJson(QJsonDocument::Compact));
    if (spec.edgeless) return;   // an endless note keeps the paper and size of pages as they were

    settings.setValue("page/width", spec.sizePx.width());
    settings.setValue("page/height", spec.sizePx.height());
    settings.setValue("newNote/landscape", spec.landscape);
    if (!spec.customImage.isEmpty()) {
        settings.setValue("newNote/customImage", spec.customImage);
        return;
    }
    settings.remove("newNote/customImage");
    Page::BackgroundType type = Page::BackgroundType::None;
    if (isClassic(spec.paperId)) {
        // Settings > Page's own background, kept as it is (its spacing too).
        settings.remove("background/paper");
        type = spec.paperId == QLatin1String("grid") ? Page::BackgroundType::Grid : Page::BackgroundType::Lines;
    } else if (spec.paperId != QLatin1String("plain") && Paper::fallbackFor(spec.paperId, &type, nullptr)) {
        settings.setValue("background/paper", spec.paperId);
    } else {
        settings.remove("background/paper");
    }
    settings.setValue("background/type", static_cast<int>(type));  // what code without papers reads
}

QString bundlePathFor(const NewNoteSpec& spec)
{
    return QDir(spec.folder).filePath(LibraryTree::uniqueName(spec.folder, spec.name.trimmed(), QStringLiteral(".snb")));
}

bool isClassic(const QString& paperId)
{
    return paperId == QLatin1String("grid") || paperId == QLatin1String("lines");
}

bool apply(Document* doc, const NewNoteSpec& spec, QString* error)
{
    if (!doc) return false;
    const bool edgeless = doc->isEdgeless();
    QString paper = edgeless ? endlessPaper(spec.paperId) : spec.paperId;
    // A paper keeps its fallback as the type: what an older version draws.
    Page::BackgroundType type = Page::BackgroundType::None;
    int spacing = 0;
    if (isClassic(paper)) {
        type = paper == QLatin1String("grid") ? Page::BackgroundType::Grid : Page::BackgroundType::Lines;
        spacing = qBound(4, spec.spacing, 400);
        paper.clear();
    } else if (!Paper::fallbackFor(paper, &type, &spacing) || paper == QLatin1String("plain")) {
        paper.clear();
    }
    bool ok = true;
    QString image;
    if (!edgeless && !spec.customImage.isEmpty()) {
        image = Paper::adoptUserPaper(spec.customImage, doc->bundlePath(), error);
        ok = !image.isEmpty();
        if (ok) {
            type = Page::BackgroundType::Custom;
            paper.clear();
        }
    }

    doc->defaultBackgroundType = type;
    doc->defaultBackgroundColor = spec.paperColor;
    doc->defaultGridColor = spec.lineColor;
    if (spacing > 0) doc->defaultGridSpacing = doc->defaultLineSpacing = spacing;
    doc->defaultPaperId = paper;
    doc->defaultCustomImage = image;
    if (!edgeless) {
        doc->defaultPageSize = spec.pageSize();
        if (doc->pageCount() > 0) {
            // setPageSize() moves both the page and the layout the viewport reads.
            doc->setPageSize(0, spec.pageSize());
            if (Page* first = doc->page(0)) {
                first->backgroundType = type;
                first->backgroundColor = spec.paperColor;
                first->gridColor = spec.lineColor;
                first->gridSpacing = doc->defaultGridSpacing;
                first->lineSpacing = doc->defaultLineSpacing;
                first->paperId = paper;
                first->customImage = image;
                first->customBackground = QPixmap();
                first->loadCustomBackground(doc->bundlePath());
            }
            doc->markPageDirty(0);   // its file holds its own paper
        }
    }

    // The cover; its picture is copied into the note (assets/images).
    QJsonObject cover = spec.cover;
    const QString picture = cover.value("image").toString();
    if (!picture.isEmpty() && QFileInfo(picture).isAbsolute()) {
        QString why = QCoreApplication::translate("NewNoteDialog", "The note has no folder yet.");
        const QString name = doc->bundlePath().isEmpty() ? QString()
            : CoverPainter::adoptPicture(picture, doc->bundlePath() + QStringLiteral("/assets/images"),
                                         QStringLiteral("cover"), &why);
        if (name.isEmpty()) {
            cover = {};
            ok = false;
            if (error) *error = why;
        } else {
            cover["image"] = name;
        }
    }
    doc->cover = cover;

    // The defaults are not an edit: an untouched new note closes without a
    // question and is not offered back after a crash (tempNoteHasWork).
    doc->clearModified();
    doc->lastModified = doc->created;
    return ok;
}

} // namespace NewNote

// ============================================================================
// The window
// ============================================================================

static bool s_narrowForTests = false;
void NewNoteDialog::setNarrowForTests(bool narrow) { s_narrowForTests = narrow; }

NewNoteDialog::NewNoteDialog(const NewNoteSpec& start, QWidget* parent)
    : QDialog(parent), m_start(start)
{
    setWindowTitle(tr("New Note"));
    m_dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    m_paperColor = start.paperColor.isValid() ? start.paperColor : QColor(m_dark ? "#2b2b2b" : "#ffffff");
    m_storedLine = start.lineColor;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("NNRoot"));
    root->setAttribute(Qt::WA_StyledBackground);
    root->setStyleSheet(dialogStyleSheet(m_dark));
    outer->addWidget(root);

    const QScreen* screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    m_narrow = s_narrowForTests || (screen && screen->availableGeometry().width() < 700);
    const bool narrow = m_narrow;

    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(14);

    // ----- Left column: title, cover (Step N3), live preview -----
    auto* left = new QFrame(root);
    left->setObjectName(QStringLiteral("NNCard"));
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(16, 14, 16, 14);
    leftLayout->setSpacing(6);
    leftLayout->addWidget(caption(tr("Title"), left));
    m_title = new QLineEdit(start.name, left);
    m_title->setClearButtonEnabled(true);
    leftLayout->addWidget(m_title);
    m_titleError = new QLabel(left);
    m_titleError->setObjectName(QStringLiteral("NNError"));
    m_titleError->setWordWrap(true);
    m_titleError->hide();
    leftLayout->addWidget(m_titleError);
    m_coverSlot = new QWidget(left);
    auto* coverLayout = new QVBoxLayout(m_coverSlot);
    coverLayout->setContentsMargins(0, 8, 0, 0);
    coverLayout->setSpacing(6);
    coverLayout->addWidget(caption(tr("Cover"), m_coverSlot));
    m_cover = start.cover;
    m_coverButton = new QPushButton(m_coverSlot);
    m_coverButton->setObjectName(QStringLiteral("NNCoverButton"));
    m_coverButton->setIconSize(QSize(30, 40));
    m_coverButton->setCursor(Qt::PointingHandCursor);
    m_coverButton->setToolTip(tr("The cover the start screen and the Library show for this note."));
    connect(m_coverButton, &QPushButton::clicked, this, [this]() {
        if (CoverPicker::edit(this, &m_cover, m_title->text().trimmed(), QString())) refreshCover();
    });
    coverLayout->addWidget(m_coverButton);
    leftLayout->addWidget(m_coverSlot);
    leftLayout->addSpacing(8);
    auto* preview = new Preview(left);
    preview->paintFn = [this](QPainter& p, const QRectF& area) { paintPreview(p, area); };
    m_preview = preview;
    leftLayout->addWidget(preview);
    m_captionName = new QLabel(left);
    m_captionName->setObjectName(QStringLiteral("NNPaperName"));
    m_captionName->setAlignment(Qt::AlignHCenter);
    m_captionSize = new QLabel(left);
    m_captionSize->setObjectName(QStringLiteral("NNPaperSize"));
    m_captionSize->setAlignment(Qt::AlignHCenter);
    m_captionSize->setWordWrap(true);
    leftLayout->addWidget(m_captionName);
    leftLayout->addWidget(m_captionSize);
    leftLayout->addStretch(1);
    if (narrow) {
        // A phone: the gallery tiles are the preview; the room goes to them.
        preview->hide();
        m_captionName->hide();
        m_captionSize->hide();
    } else {
        left->setFixedWidth(kLeftWidth);
    }

    // ----- Right: options and the gallery -----
    auto* right = new QVBoxLayout;
    right->setSpacing(14);
    right->addWidget(buildOptions());
    right->addWidget(buildGallery(), 1);

    auto* body = new QBoxLayout(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    body->setSpacing(14);
    body->addWidget(left);
    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    // ----- Bottom: the place, the switch, Cancel / Create -----
    m_place = new LibraryPlacePicker(root);
    m_place->setPlace(start.folder);
    m_place->setToolTip(tr("Where the note is saved: a notebook and a section of the Library, or another folder."));
    m_dontAsk = new QCheckBox(tr("Do not ask again, use these settings"), root);
    m_dontAsk->setToolTip(tr("A new note is then made at once with these choices. Turn this window back on "
                             "in Settings > Page, or use + > New Note with Options..."));
    auto* cancel = new QPushButton(tr("Cancel"), root);
    cancel->setObjectName(QStringLiteral("NNButton"));
    m_create = new QPushButton(tr("Create"), root);
    m_create->setObjectName(QStringLiteral("NNCreate"));
    m_create->setDefault(true);
    for (QPushButton* b : {cancel, m_create}) b->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, [this]() {
        validate();
        if (m_create->isEnabled()) accept();
    });

    auto* where = new QHBoxLayout;
    where->setSpacing(10);
    where->addWidget(caption(tr("Where"), root));
    where->addWidget(m_place, 1);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    if (narrow) {
        layout->addLayout(where);
        layout->addWidget(m_dontAsk);
        buttons->addStretch(1);
    } else {
        where->setStretch(1, 0);
        m_place->setMinimumWidth(260);
        m_place->setMaximumWidth(460);
        buttons->addLayout(where, 1);
        buttons->addSpacing(16);
        buttons->addWidget(m_dontAsk);
        buttons->addStretch(1);
    }
    buttons->addWidget(cancel);
    buttons->addWidget(m_create);
    layout->addLayout(buttons);

    // ----- Start state -----
    m_landscape->setChecked(start.landscape);
    m_portrait->setChecked(!start.landscape);
    m_endless->setChecked(start.edgeless);
    m_paged->setChecked(!start.edgeless);
    QAbstractButton* colour = m_customSwatch;
    for (QAbstractButton* b : m_swatches->buttons()) {
        if (b != m_customSwatch && static_cast<Swatch*>(b)->color() == m_paperColor) colour = b;
    }
    if (colour == m_customSwatch) static_cast<Swatch*>(m_customSwatch)->setColor(m_paperColor);
    colour->setChecked(true);
    m_paper = start.customImage.isEmpty() ? start.paperId : start.customImage;
    if (start.edgeless && endlessPaper(m_paper) != m_paper) {
        m_pagedPaper = m_paper;   // back when Paged is chosen again
        m_paper = endlessPaper(m_paper);
    }

    connect(m_title, &QLineEdit::textChanged, this, &NewNoteDialog::validate);
    connect(m_place, &LibraryPlacePicker::placeChanged, this, &NewNoteDialog::validate);
    connect(m_portrait, &QPushButton::toggled, this, [this]() { refreshTiles(); refresh(); });
    connect(m_endless, &QPushButton::toggled, this, &NewNoteDialog::setEdgeless);
    connect(m_swatches, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton* b) {
        auto* swatch = static_cast<Swatch*>(b);
        if (b == m_customSwatch) {
            const QColor chosen = QColorDialog::getColor(m_paperColor, this, tr("Paper Colour"));
            if (!chosen.isValid()) {   // cancelled: back to the colour in use
                for (QAbstractButton* other : m_swatches->buttons()) {
                    if (static_cast<Swatch*>(other)->color() == m_paperColor) other->setChecked(true);
                }
                return;
            }
            swatch->setColor(chosen);
        }
        m_paperColor = swatch->color();
        refreshTiles();
        refresh();
    });

    refreshTiles();
    selectPaper(m_paper);
    refreshCover();
    validate();
    m_title->setFocus();
    m_title->selectAll();
    DialogSizing::openAtSize(this, narrow ? QSize(420, 760) : QSize(980, 690));
    QTimer::singleShot(0, this, &NewNoteDialog::showChosen);   // once the gallery has its size
}

void NewNoteDialog::showChosen()
{
    for (QListWidget* list : m_lists) {
        QListWidgetItem* item = list->currentItem();
        if (!item || !list->isVisible()) continue;
        const QRect r = list->visualItemRect(item);
        const QPoint c = list->viewport()->mapTo(m_galleryArea->widget(), r.center());
        m_galleryArea->ensureVisible(c.x(), c.y(), 0, r.height() / 2 + 44);   // with its group's title
    }
}

QWidget* NewNoteDialog::buildOptions()
{
    const Theme theme = themeFor(m_dark);
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("NNCard"));
    auto* grid = new QGridLayout(card);
    grid->setContentsMargins(16, 12, 16, 14);
    grid->setHorizontalSpacing(28);
    grid->setVerticalSpacing(6);

    // Page size: the presets of Settings > Page, and a size of your own.
    m_size = new QComboBox(card);
    m_size->addItem(tr("A3 (297 × 420 mm)"), QSizeF(1123, 1587));
    m_size->addItem(tr("B4 (250 × 353 mm)"), QSizeF(945, 1334));
    m_size->addItem(tr("A4 (210 × 297 mm)"), QSizeF(794, 1123));
    m_size->addItem(tr("B5 (176 × 250 mm)"), QSizeF(665, 945));
    m_size->addItem(tr("A5 (148 × 210 mm)"), QSizeF(559, 794));
    m_size->addItem(tr("US Letter (8.5 × 11 in)"), QSizeF(816, 1056));
    m_size->addItem(tr("US Legal (8.5 × 14 in)"), QSizeF(816, 1344));
    m_size->addItem(tr("US Tabloid (11 × 17 in)"), QSizeF(1056, 1632));
    m_size->addItem(tr("Custom size..."));
    m_customRow = new QWidget(card);
    auto* customLayout = new QHBoxLayout(m_customRow);
    customLayout->setContentsMargins(0, 2, 0, 0);
    customLayout->setSpacing(6);
    m_customW = new LengthSpinBox(m_customRow);
    m_customH = new LengthSpinBox(m_customRow);
    for (LengthSpinBox* spin : {m_customW, m_customH}) {
        spin->setRange(200, 4000);   // 53 mm to a little over a metre
        spin->setValue(qRound(m_start.sizePx.width()));
    }
    m_customH->setValue(qRound(m_start.sizePx.height()));
    m_customW->setToolTip(tr("Width"));
    m_customH->setToolTip(tr("Height"));
    customLayout->addWidget(m_customW);
    customLayout->addWidget(new QLabel(QStringLiteral("×"), m_customRow));
    customLayout->addWidget(m_customH);
    customLayout->addStretch(1);
    int index = m_size->count() - 1;
    for (int i = 0; i < m_size->count() - 1; ++i) {
        const QSizeF s = m_size->itemData(i).toSizeF();
        if (qAbs(s.width() - m_start.sizePx.width()) < 0.5 && qAbs(s.height() - m_start.sizePx.height()) < 0.5)
            index = i;
    }
    m_size->setCurrentIndex(index);
    m_customRow->setVisible(index == m_size->count() - 1);
    connect(m_size, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NewNoteDialog::sizeChanged);
    for (LengthSpinBox* spin : {m_customW, m_customH})
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshTiles(); refresh(); });

    m_portrait = new QPushButton(pageIcon(false, theme.text), tr("Portrait"));
    m_landscape = new QPushButton(pageIcon(true, theme.text), tr("Landscape"));
    m_paged = new QPushButton(tr("Pages"));
    m_endless = new QPushButton(tr("Endless canvas"));
    m_paged->setToolTip(tr("Pages of the size above, one after the other."));
    m_endless->setToolTip(tr("One canvas without edges. Only papers that can go on forever are offered."));

    // Paper colour: white, cream, yellow, dark, your own.
    auto* swatchRow = new QWidget(card);
    auto* swatchLayout = new QHBoxLayout(swatchRow);
    swatchLayout->setContentsMargins(0, 0, 0, 0);
    swatchLayout->setSpacing(4);
    m_swatches = new QButtonGroup(this);
    const QColor edge = m_dark ? QColor("#5a5a5a") : QColor("#c4c4c4");
    const std::pair<QColor, QString> colours[] = {
        {QColor("#ffffff"), tr("White")}, {QColor("#fdf6e3"), tr("Cream")},
        {QColor("#fff4b8"), tr("Yellow")}, {QColor("#2b2b2b"), tr("Dark")},
    };
    for (const auto& [color, name] : colours) {
        auto* swatch = new Swatch(color, false, theme.accent, edge, swatchRow);
        swatch->setToolTip(name);
        m_swatches->addButton(swatch);
        swatchLayout->addWidget(swatch);
    }
    auto* custom = new Swatch(QColor(), true, theme.accent, edge, swatchRow);
    custom->setToolTip(tr("A colour of your own..."));
    m_swatches->addButton(custom);
    swatchLayout->addWidget(custom);
    m_customSwatch = custom;
    swatchLayout->addStretch(1);

    QLabel* colourCaption = caption(tr("Paper colour"), card);
    QLabel* orientationCaption = caption(tr("Orientation"), card);
    QLabel* kindCaption = caption(tr("Kind"), card);
    for (QLabel* label : {colourCaption, orientationCaption, kindCaption}) label->setContentsMargins(0, 8, 0, 0);
    orientationCaption->setContentsMargins(0, m_narrow ? 8 : 0, 0, 0);
    // Two columns, or one on a narrow screen.
    const int col = m_narrow ? 0 : 1;
    const int row = m_narrow ? 5 : 0;
    grid->addWidget(caption(tr("Page size"), card), 0, 0);
    grid->addWidget(m_size, 1, 0);
    grid->addWidget(m_customRow, 2, 0);
    grid->addWidget(colourCaption, 3, 0);
    grid->addWidget(swatchRow, 4, 0);
    grid->addWidget(orientationCaption, row, col);
    grid->addWidget(segmented(m_portrait, m_landscape, card), row + 1, col, Qt::AlignLeft);
    grid->addWidget(kindCaption, row + 3, col);
    grid->addWidget(segmented(m_paged, m_endless, card), row + 4, col, Qt::AlignLeft);
    grid->setColumnStretch(2, 1);
    return card;
}

QWidget* NewNoteDialog::buildGallery()
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("NNCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(2, 4, 2, 4);
    auto* body = new QWidget;
    body->setObjectName(QStringLiteral("NNGalleryBody"));
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 2, 12, 10);
    bodyLayout->setSpacing(0);

    int group = -1;
    QListWidget* list = nullptr;
    for (const Paper::PaperDef& def : Paper::all()) {
        if (def.group != group) {
            group = def.group;
            list = addGroup(body, Paper::groupName(def.group));
            // Settings > Page's classic background, first, when it is the default.
            if (m_lists.size() == 1 && NewNote::isClassic(m_start.paperId))
                addTile(list, PaperTile, m_start.paperId, classicName(m_start.paperId, m_start.spacing));
        }
        addTile(list, PaperTile, QString::fromLatin1(def.id), Paper::displayName(def));
    }
    m_userList = addGroup(body, tr("My papers"));
    const QFileInfoList mine = QDir(Paper::userPapersDir())
        .entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const QFileInfo& file : mine)
        addTile(m_userList, UserTile, QDir::cleanPath(file.absoluteFilePath()), file.completeBaseName());
    addTile(m_userList, AddTile, QString(), tr("Add from a PDF or picture..."));
    bodyLayout->addStretch(1);

    auto* area = DialogSizing::inScrollArea(body, card);
    area->setObjectName(QStringLiteral("NNGallery"));
    area->viewport()->setAutoFillBackground(false);
    body->setAutoFillBackground(false);
    m_galleryArea = area;
    cardLayout->addWidget(area);
    return card;
}

QListWidget* NewNoteDialog::addGroup(QWidget* body, const QString& title)
{
    auto* header = new QToolButton(body);
    header->setObjectName(QStringLiteral("NNGroup"));
    header->setText(title);
    header->setCheckable(true);
    header->setChecked(true);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setIconSize(QSize(12, 12));
    header->setCursor(Qt::PointingHandCursor);
    header->setIcon(QIcon(iconPath("down_arrow", m_dark)));

    auto* list = new QListWidget(body);
    list->setObjectName(QStringLiteral("NNTiles"));
    list->setViewMode(QListView::IconMode);
    list->setMovement(QListView::Static);
    list->setResizeMode(QListView::Adjust);
    list->setWrapping(true);
    list->setFlow(QListView::LeftToRight);
    list->setWordWrap(true);
    list->setTextElideMode(Qt::ElideRight);
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setFrameShape(QFrame::NoFrame);
    list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    list->setMouseTracking(true);
    list->installEventFilter(this);
    list->viewport()->installEventFilter(this);
    // Keyboard: the arrows choose, Enter makes the note.
    connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item && item->data(kKindRole).toInt() != AddTile) choose(item);
    });
    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item->data(kKindRole).toInt() == AddTile) {
            addUserPaper();
        } else {
            validate();
            if (m_create->isEnabled()) accept();
        }
    });
    connect(header, &QToolButton::toggled, this, [this, header, list](bool open) {
        list->setVisible(open);
        header->setIcon(QIcon(iconPath(open ? "down_arrow" : "right_arrow", m_dark)));
    });
    header->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto* bodyLayout = static_cast<QVBoxLayout*>(body->layout());
    bodyLayout->addWidget(header, 0, Qt::AlignLeft);
    bodyLayout->addWidget(list);
    m_lists << list;
    return list;
}

void NewNoteDialog::addTile(QListWidget* list, TileKind kind, const QString& key, const QString& text)
{
    auto* item = new QListWidgetItem(text, list);
    item->setData(kKeyRole, key);
    item->setData(kKindRole, kind);
    item->setToolTip(text);
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
}

bool NewNoteDialog::dontAskAgain() const
{
    return m_dontAsk->isChecked();
}

NewNoteSpec NewNoteDialog::spec() const
{
    NewNoteSpec s = m_start;   // keeps what this window does not show (the cover)
    s.name = m_title->text().trimmed();
    s.folder = m_place->place();
    s.sizePx = chosenSizePx();
    s.landscape = m_landscape->isChecked();
    s.edgeless = m_endless->isChecked();
    s.paperColor = m_paperColor;
    s.lineColor = NewNote::lineColorFor(m_paperColor, m_storedLine);
    s.cover = m_cover;
    const bool user = isUserPaper(m_paper);
    s.customImage = user ? m_paper : QString();
    s.paperId = user ? QStringLiteral("plain") : m_paper;
    return s;
}

bool NewNoteDialog::ask(QWidget* parent, bool edgeless, const QString& folderHint,
                        NewNoteSpec* spec, bool forceShow)
{
    const NewNoteSpec start = NewNote::defaults(edgeless, folderHint);
    QSettings settings("SpeedyNote", "App");
    if (!forceShow && !settings.value("newNote/ask", true).toBool()) {
        *spec = start;
        return true;
    }
    NewNoteDialog dialog(start, parent);
    if (dialog.exec() != QDialog::Accepted) return false;
    *spec = dialog.spec();
    NewNote::remember(*spec);
    if (dialog.dontAskAgain()) settings.setValue("newNote/ask", false);
    return true;
}

void NewNoteDialog::selectPaper(const QString& idOrPath)
{
    QListWidgetItem* found = nullptr;
    QListWidgetItem* plain = nullptr;
    for (QListWidget* list : m_lists) {
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            const QString key = item->data(kKeyRole).toString();
            if (key == QLatin1String("plain")) plain = item;
            if (!key.isEmpty() && key == QDir::cleanPath(idOrPath) && (item->flags() & Qt::ItemIsEnabled))
                found = item;
        }
    }
    choose(found ? found : plain);   // a paper gone (or unknown) falls back to plain
    if (isVisible()) showChosen();
}

void NewNoteDialog::setEdgeless(bool edgeless)
{
    if (m_endless->isChecked() != edgeless) {
        (edgeless ? m_endless : m_paged)->setChecked(true);   // comes back here through toggled
        return;
    }
    QString paper = m_paper;
    if (edgeless && endlessPaper(m_paper) != m_paper) {
        m_pagedPaper = m_paper;
        paper = endlessPaper(m_paper);
    } else if (!edgeless && !m_pagedPaper.isEmpty()) {
        paper = m_pagedPaper;
        m_pagedPaper.clear();
    }
    refreshTiles();
    selectPaper(paper);
}

void NewNoteDialog::setLandscape(bool landscape)
{
    (landscape ? m_landscape : m_portrait)->setChecked(true);
}

void NewNoteDialog::choose(QListWidgetItem* item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
    if (item->data(kKindRole).toInt() == AddTile) {
        addUserPaper();
        return;
    }
    m_paper = item->data(kKeyRole).toString();
    for (QListWidget* list : m_lists) {
        const QSignalBlocker block(list);
        if (list == item->listWidget()) {
            list->setCurrentItem(item);
        } else {
            list->clearSelection();
            list->setCurrentItem(nullptr);
        }
    }
    refresh();
}

void NewNoteDialog::addUserPaper()
{
    const QString source = QFileDialog::getOpenFileName(
        this, tr("Add a Paper"), QString(), tr("PDF or picture (*.pdf *.png *.jpg *.jpeg *.bmp *.webp)"));
    if (source.isEmpty()) return;
    QString error;
    const QString png = Paper::importUserPaper(source, &error);
    if (png.isEmpty()) {
        QMessageBox::warning(this, tr("Add a Paper"), error);
        return;
    }
    const QString key = QDir::cleanPath(png);
    auto* item = new QListWidgetItem(QFileInfo(png).completeBaseName());
    item->setData(kKeyRole, key);
    item->setData(kKindRole, UserTile);
    item->setToolTip(item->text());
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_userList->insertItem(m_userList->count() - 1, item);   // before "Add..."
    refreshTiles();
    fitList(m_userList);
    selectPaper(key);
}

void NewNoteDialog::refreshCover()
{
    const qreal dpr = devicePixelRatioF();
    const QSize size = m_coverButton->iconSize();
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(QPointF(0.5, 0.5), QSizeF(size) - QSizeF(1, 1));
    if (m_cover.isEmpty()) {
        p.setPen(QPen(themeFor(m_dark).text2, 1, Qt::DashLine));
        p.drawRoundedRect(r, 3, 3);
    } else {
        CoverPainter::paint(p, r, m_cover, QString(), QString());
    }
    p.end();
    m_coverButton->setIcon(QIcon(pm));
    m_coverButton->setText(m_cover.isEmpty() ? tr("No cover")
                           : CoverPainter::displayName(m_cover.value("design").toString(QStringLiteral("plain"))));
}

void NewNoteDialog::sizeChanged()
{
    const bool custom = m_size->currentIndex() == m_size->count() - 1;
    m_customRow->setVisible(custom);
    refreshTiles();
    refresh();
}

QSizeF NewNoteDialog::chosenSizePx() const
{
    if (m_size->currentIndex() == m_size->count() - 1)
        return QSizeF(m_customW->value(), m_customH->value());
    return m_size->currentData().toSizeF();
}

QSizeF NewNoteDialog::pagePx() const
{
    const QSizeF s = chosenSizePx();
    return m_landscape->isChecked() ? s.transposed() : s;
}

void NewNoteDialog::validate()
{
    const QString name = m_title->text().trimmed();
    QString error = name.isEmpty() ? tr("Give the note a title.") : LibraryTree::checkName(name);
    if (error.isEmpty() && m_place->place().isEmpty()) error = tr("Choose where the note goes.");
    m_titleError->setText(error);
    m_titleError->setVisible(!error.isEmpty());
    m_create->setEnabled(error.isEmpty());
}

void NewNoteDialog::refresh()
{
    const bool endless = m_endless->isChecked();
    m_size->setEnabled(!endless);
    m_customRow->setEnabled(!endless);
    m_portrait->setEnabled(!endless);
    m_landscape->setEnabled(!endless);

    if (isUserPaper(m_paper)) {
        m_captionName->setText(tr("My paper: %1").arg(QFileInfo(m_paper).completeBaseName()));
    } else if (const Paper::PaperDef* def = Paper::find(m_paper)) {
        m_captionName->setText(Paper::displayName(*def));
    } else if (NewNote::isClassic(m_paper)) {
        m_captionName->setText(classicName(m_paper, m_start.spacing));
    }
    if (endless) {
        m_captionSize->setText(tr("Endless canvas"));
    } else {
        const QSizeF page = pagePx();
        const bool custom = m_size->currentIndex() == m_size->count() - 1;
        const QString sizeName = custom ? tr("Custom") : m_size->currentText().section(QLatin1String(" ("), 0, 0);
        m_captionSize->setText(QStringLiteral("%1 · %2 · %3").arg(
            sizeName, m_landscape->isChecked() ? tr("Landscape") : tr("Portrait"),
            LengthUnit::sizeText(page.width(), page.height())));
    }
    m_preview->update();
}

const Paper::Drawing& NewNoteDialog::drawing(const QString& id, const QSizeF& size, bool endless) const
{
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(id).arg(size.width()).arg(size.height()).arg(int(endless));
    auto it = m_drawings.find(key);
    if (it == m_drawings.end()) it = m_drawings.insert(key, Paper::build(id, QRectF(QPointF(0, 0), size), endless));
    return it.value();
}

QPixmap NewNoteDialog::tilePixmap(TileKind kind, const QString& key) const
{
    const Theme theme = themeFor(m_dark);
    const QSize icon = m_lists.first()->iconSize();
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(qCeil(icon.width() * dpr), qCeil(icon.height() * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(QPointF(0, 0), QSizeF(icon)).adjusted(kTileMargin, kTileMargin - 2, -kTileMargin, -kTileMargin);
    const QSizeF page = pagePx();
    QRectF r = fitted(page, box);
    r = QRectF(qRound(r.left()) + 0.5, qRound(r.top()) + 0.5, qRound(r.width()) - 1.0, qRound(r.height()) - 1.0);

    if (kind == AddTile) {
        p.setPen(QPen(theme.text2, 1.2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 4, 4);
        p.setPen(QPen(theme.accent, 2.2, Qt::SolidLine, Qt::RoundCap));
        const QPointF c = r.center();
        p.drawLine(c - QPointF(9, 0), c + QPointF(9, 0));
        p.drawLine(c - QPointF(0, 9), c + QPointF(0, 9));
        return pm;
    }

    paintShadow(p, r, 3, m_dark);
    p.setPen(QPen(theme.border.darker(m_dark ? 70 : 108), 1));
    p.setBrush(m_paperColor);
    p.drawRoundedRect(r, 3, 3);
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(r, 3, 3);
    p.setClipPath(clip);
    if (kind == UserTile) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPixmap picture = Paper::userPaperPixmap(key);
        if (!picture.isNull()) p.drawPixmap(r, picture, QRectF(picture.rect()));
    } else {
        const QSizeF v = page.scaled(kTilePagePx, kTilePagePx, Qt::KeepAspectRatio);
        p.translate(r.topLeft());
        p.scale(r.width() / v.width(), r.height() / v.height());
        const QColor line = NewNote::lineColorFor(m_paperColor, m_storedLine);
        if (NewNote::isClassic(key)) {
            paintClassic(p, key, m_start.spacing, v, line);
        } else {
            Paper::paintDrawing(p, drawing(key, v, false), line, kTileDetail / dpr);
        }
    }
    p.restore();
    return pm;
}

void NewNoteDialog::refreshTiles()
{
    const bool landscape = pagePx().width() > pagePx().height();
    const int extra = 2 * kTileMargin;
    const QSize icon = landscape ? QSize(kTileH + extra, kTileW + extra) : QSize(kTileW + extra, kTileH + extra);
    m_minGrid = QSize(qMax(112, icon.width() + 16), icon.height() + 46);
    const bool endless = m_endless->isChecked();
    for (QListWidget* list : m_lists) {
        list->setIconSize(icon);
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            const auto kind = static_cast<TileKind>(item->data(kKindRole).toInt());
            const QString key = item->data(kKeyRole).toString();
            const Paper::PaperDef* def = Paper::find(key);
            const bool enabled = !endless
                || (kind == PaperTile && ((def && def->endless) || NewNote::isClassic(key)));
            item->setFlags(enabled ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags);
            // Own pictures for every state: the style would tint or grey them.
            const QPixmap pm = tilePixmap(kind, key);
            QPixmap faded(pm.size());
            faded.setDevicePixelRatio(pm.devicePixelRatio());
            faded.fill(Qt::transparent);
            {
                QPainter p(&faded);
                p.setOpacity(0.3);
                p.drawPixmap(0, 0, pm);
            }
            QIcon tile(pm);
            tile.addPixmap(pm, QIcon::Selected);
            tile.addPixmap(faded, QIcon::Disabled);
            item->setIcon(tile);
            item->setSizeHint(m_minGrid - QSize(8, 6));   // a small gap between lit tiles
        }
        list->setGridSize(m_minGrid);
        fitList(list);
    }
}

void NewNoteDialog::fitList(QListWidget* list) const
{
    // The page scrolls, not the lists: each list is as tall as its rows, and
    // its cells share the width evenly. Not the viewport's width: on the list's
    // Resize event the viewport is not resized yet.
    if (m_minGrid.isEmpty()) return;
    const int width = list->contentsRect().width() - 8;   // slack: a full row must not wrap
    const int perRow = qMax(1, width / m_minGrid.width());
    const QSize grid(qMax(m_minGrid.width(), width / perRow), m_minGrid.height());
    if (list->gridSize() != grid) list->setGridSize(grid);
    const int rows = (list->count() + perRow - 1) / perRow;
    const int height = rows * grid.height() + 2 * list->frameWidth() + 6;
    if (list->height() != height) list->setFixedHeight(height);
}

void NewNoteDialog::paintPreview(QPainter& p, const QRectF& area) const
{
    const Theme theme = themeFor(m_dark);
    const qreal dpr = devicePixelRatioF();
    const QColor line = NewNote::lineColorFor(m_paperColor, m_storedLine);
    const QRectF box = area.adjusted(8, 8, -8, -10);

    if (m_endless->isChecked()) {
        // A square window onto the canvas: the paper from the world origin, a
        // dashed edge for "it goes on".
        const qreal side = qMin(box.width(), box.height());
        const QRectF r(box.center().x() - side / 2, box.top(), side, side);
        paintShadow(p, r, 10, m_dark);
        p.setPen(Qt::NoPen);
        p.setBrush(m_paperColor);
        p.drawRoundedRect(r, 10, 10);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(r, 10, 10);
        p.setClipPath(clip);
        const qreal zoom = 0.45;
        p.translate(r.topLeft());
        p.scale(zoom, zoom);
        const QSizeF window(r.width() / zoom, r.height() / zoom);
        if (NewNote::isClassic(m_paper)) {
            paintClassic(p, m_paper, m_start.spacing, window, line);
        } else {
            Paper::paintDrawing(p, drawing(m_paper, window, true), line, kTileDetail / dpr);
        }
        p.restore();
        p.setPen(QPen(theme.text2, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 10, 10);
        return;
    }

    const QSizeF page = pagePx();
    QRectF r = fitted(page, box);
    paintShadow(p, r, 4, m_dark);
    p.setPen(QPen(theme.border.darker(m_dark ? 70 : 108), 1));
    p.setBrush(m_paperColor);
    p.drawRoundedRect(r, 4, 4);
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(r, 4, 4);
    p.setClipPath(clip);
    if (isUserPaper(m_paper)) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPixmap picture = Paper::userPaperPixmap(m_paper);
        if (!picture.isNull()) p.drawPixmap(r, picture, QRectF(picture.rect()));
    } else {
        p.translate(r.topLeft());
        p.scale(r.width() / page.width(), r.height() / page.height());
        if (NewNote::isClassic(m_paper)) {
            paintClassic(p, m_paper, m_start.spacing, page, line);
        } else {
            Paper::paintDrawing(p, drawing(m_paper, page, false), line, kTileDetail / dpr);   // small labels too
        }
    }
    p.restore();
}

bool NewNoteDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (auto* list = qobject_cast<QListWidget*>(watched)) {
        if (event->type() == QEvent::Resize) fitList(list);
        return QDialog::eventFilter(watched, event);
    }
    auto* viewport = qobject_cast<QWidget*>(watched);
    auto* list = viewport ? qobject_cast<QListWidget*>(viewport->parentWidget()) : nullptr;
    if (!list || list->viewport() != viewport || !m_lists.contains(list))
        return QDialog::eventFilter(watched, event);

    // A tap chooses on release; a drag (finger, pen or mouse) scrolls the gallery.
    if (!m_galleryArea) return QDialog::eventFilter(watched, event);   // still being built
    QScrollBar* bar = m_galleryArea->verticalScrollBar();
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == Qt::LeftButton) {
            m_pressPos = SN_MOUSE_GLOBAL_POS(e);
            m_pressScroll = bar->value();
            m_dragging = false;
            list->setFocus(Qt::MouseFocusReason);
        }
        return true;
    }
    case QEvent::MouseMove: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (!(e->buttons() & Qt::LeftButton)) return false;   // hover: the list lights the tile
        const int dy = SN_MOUSE_GLOBAL_POS(e).y() - m_pressPos.y();
        if (!m_dragging && qAbs(dy) >= QApplication::startDragDistance()) m_dragging = true;
        if (m_dragging) bar->setValue(m_pressScroll - dy);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == Qt::LeftButton && !m_dragging) choose(list->itemAt(SN_MOUSE_POS(e).toPoint()));
        m_dragging = false;
        return true;
    }
    case QEvent::MouseButtonDblClick: {
        auto* e = static_cast<QMouseEvent*>(event);
        QListWidgetItem* item = list->itemAt(SN_MOUSE_POS(e).toPoint());
        if (e->button() == Qt::LeftButton && item && (item->flags() & Qt::ItemIsEnabled)
            && item->data(kKindRole).toInt() != AddTile) {
            choose(item);
            validate();
            if (m_create->isEnabled()) accept();   // a double tap on a paper makes the note
        }
        return true;
    }
    default:
        return QDialog::eventFilter(watched, event);
    }
}
