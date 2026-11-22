#include "CoverPicker.h"

#include "../../core/CoverPainter.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace {

const QSize kTile(42, 56);
const char* const kColours[] = {"#4a6fa5", "#2c3e63", "#3f7d5a", "#2f7f86", "#b0413e", "#d9822b", "#e3b23c",
                                "#6c4f9c", "#c45b84", "#7a5a3a", "#5f6368", "#2b2b2b", "#efe6d2"};

class CoverPreview : public QWidget {
public:
    std::function<void(QPainter&, const QRectF&)> paintFn;
    using QWidget::QWidget;
    QSize sizeHint() const override { return {170, 226}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (paintFn) paintFn(p, QRectF(rect()));
    }
};

// A round colour button; a ring marks the colour in use.
QIcon colourIcon(const QColor& color, bool current, const QColor& ring)
{
    QPixmap pm(48, 48);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (current) {
        p.setPen(QPen(ring, 2));
        p.drawEllipse(QRectF(1.5, 1.5, 21, 21));
    }
    p.setPen(QPen(color.darker(130), 1));
    p.setBrush(color);
    p.drawEllipse(QRectF(5, 5, 14, 14));
    return QIcon(pm);
}

QRectF bookIn(const QRectF& area)
{
    const QRectF box = area.adjusted(8, 8, -8, -8);
    const qreal w = qMin(box.width(), box.height() * 0.75);
    return QRectF(box.center().x() - w / 2, box.center().y() - w / 1.5, w, w / 0.75);
}

void paintNoCover(QPainter& p, const QRectF& r, const QColor& ink)
{
    p.setPen(QPen(ink, 1.2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
    p.drawLine(r.topLeft() + QPointF(6, 6), r.bottomRight() - QPointF(6, 6));
}

} // namespace

CoverPicker::CoverPicker(const QJsonObject& cover, const QString& title, const QString& baseDir, QWidget* parent)
    : QDialog(parent), m_cover(cover), m_none(cover.isEmpty()), m_title(title), m_baseDir(baseDir)
{
    setWindowTitle(tr("Cover"));
    if (!m_cover.contains("design")) m_cover["design"] = QStringLiteral("spine");   // what a colour turns "No cover" into

    auto* preview = new CoverPreview(this);
    preview->paintFn = [this](QPainter& p, const QRectF& area) {
        const QRectF book = bookIn(area);
        if (m_none) paintNoCover(p, book, palette().color(QPalette::Mid));
        else CoverPainter::paint(p, book, m_cover, m_title, m_baseDir);
    };
    m_preview = preview;

    m_designs = new QListWidget(this);
    m_designs->setViewMode(QListView::IconMode);
    m_designs->setMovement(QListView::Static);
    m_designs->setResizeMode(QListView::Adjust);
    m_designs->setWrapping(true);
    m_designs->setIconSize(kTile);
    m_designs->setGridSize(QSize(86, 90));
    m_designs->setUniformItemSizes(true);
    m_designs->setSelectionMode(QAbstractItemView::SingleSelection);
    m_designs->setMinimumSize(5 * 86 + 12, 2 * 90 + 8);
    auto* none = new QListWidgetItem(tr("None"), m_designs);
    none->setToolTip(tr("No cover: the first page is shown"));
    none->setData(Qt::UserRole, QString());
    for (const QString& id : CoverPainter::designs()) {
        auto* item = new QListWidgetItem(CoverPainter::displayName(id), m_designs);
        item->setData(Qt::UserRole, id);
    }
    connect(m_designs, &QListWidget::itemClicked, this, &CoverPicker::choose);
    connect(m_designs, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item && !QApplication::mouseButtons()) choose(item);   // the arrow keys
    });

    auto* colourRow = new QHBoxLayout;
    colourRow->setSpacing(0);
    m_colours = new QButtonGroup(this);
    int id = 0;
    for (const char* name : kColours) {
        auto* button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setIconSize(QSize(24, 24));
        button->setToolTip(QString::fromLatin1(name));
        m_colours->addButton(button, id++);
        colourRow->addWidget(button);
    }
    auto* custom = new QToolButton(this);
    custom->setText(QStringLiteral("..."));
    custom->setToolTip(tr("A colour of your own..."));
    colourRow->addWidget(custom);
    colourRow->addStretch(1);
    connect(m_colours, &QButtonGroup::idClicked, this, [this](int i) {
        m_cover["color"] = QString::fromLatin1(kColours[i]);
        m_none = false;   // a colour means a cover
        refresh();
    });
    connect(custom, &QToolButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(CoverPainter::color(m_cover), this, tr("Cover Colour"));
        if (!chosen.isValid()) return;
        m_cover["color"] = chosen.name();
        m_none = false;
        refresh();
    });

    m_showTitle = new QCheckBox(tr("Show the title on the cover"), this);
    m_showTitle->setChecked(m_cover.value("showTitle").toBool(true));
    connect(m_showTitle, &QCheckBox::toggled, this, [this](bool on) {
        m_cover["showTitle"] = on;
        refresh();
    });

    auto* pictureButton = new QPushButton(tr("Picture..."), this);
    pictureButton->setToolTip(tr("A photo or a picture of your own fills the cover."));
    connect(pictureButton, &QPushButton::clicked, this, &CoverPicker::pickPicture);
    m_pictureName = new QLabel(this);
    m_pictureName->setTextInteractionFlags(Qt::NoTextInteraction);
    auto* pictureRow = new QHBoxLayout;
    pictureRow->addWidget(pictureButton);
    pictureRow->addWidget(m_pictureName, 1);

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Design"), this));
    right->addWidget(m_designs, 1);
    right->addWidget(new QLabel(tr("Colour"), this));
    right->addLayout(colourRow);
    right->addWidget(m_showTitle);
    right->addLayout(pictureRow);

    auto* body = new QHBoxLayout;
    body->addWidget(preview);
    body->addLayout(right, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);
    refresh();
}

QJsonObject CoverPicker::cover() const
{
    if (m_none) return {};
    QJsonObject c = m_cover;
    if (c.value("design").toString() != QLatin1String("picture")) c.remove("image");
    if (c.value("showTitle").toBool(true)) c.remove("showTitle");   // the default is not written
    c["color"] = CoverPainter::color(c).name();
    return c;
}

bool CoverPicker::edit(QWidget* parent, QJsonObject* cover, const QString& title, const QString& baseDir)
{
    CoverPicker picker(*cover, title, baseDir, parent);
    if (picker.exec() != QDialog::Accepted) return false;
    *cover = picker.cover();
    return true;
}

void CoverPicker::setDesign(const QString& design)
{
    m_none = design.isEmpty();
    if (!m_none) m_cover["design"] = design;
    refresh();
}

void CoverPicker::setPicture(const QString& path)
{
    m_cover["image"] = path;
    m_cover["design"] = QStringLiteral("picture");
    m_none = false;
    refresh();
}

void CoverPicker::choose(QListWidgetItem* item)
{
    const QString design = item ? item->data(Qt::UserRole).toString() : QString();
    // The picture design needs a picture: ask for one, or stay where we were.
    if (design == QLatin1String("picture") && CoverPainter::picturePath(m_cover, m_baseDir).isEmpty()) {
        if (!pickPicture()) refresh();
        return;
    }
    setDesign(design);
}

bool CoverPicker::pickPicture()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Cover Picture"), QString(),
                                                      tr("Pictures (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (file.isEmpty()) return false;
    setPicture(file);
    return true;
}

void CoverPicker::refresh()
{
    const QString design = m_none ? QString() : m_cover.value("design").toString();
    const qreal dpr = devicePixelRatioF();
    const QColor mid = palette().color(QPalette::Mid);
    {
        const QSignalBlocker block(m_designs);
        for (int i = 0; i < m_designs->count(); ++i) {
            QListWidgetItem* item = m_designs->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            QPixmap pm(kTile * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r(QPointF(0.5, 0.5), QSizeF(kTile) - QSizeF(1, 1));
            if (id.isEmpty()) {
                paintNoCover(p, r, mid);
            } else {
                QJsonObject sample = m_cover;
                sample["design"] = id;
                CoverPainter::paint(p, r, sample, QString(), m_baseDir);
            }
            p.end();
            item->setIcon(QIcon(pm));
            if (id == design) m_designs->setCurrentItem(item);
        }
    }
    const QColor current = CoverPainter::color(m_cover);
    for (QAbstractButton* button : m_colours->buttons()) {
        const QColor c(QString::fromLatin1(kColours[m_colours->id(button)]));
        button->setIcon(colourIcon(c, !m_none && c == current, palette().color(QPalette::Highlight)));
    }
    m_showTitle->setEnabled(!m_none);
    const QString picture = CoverPainter::picturePath(m_cover, m_baseDir);
    m_pictureName->setText(design == QLatin1String("picture") ? QFileInfo(picture).fileName() : QString());
    m_preview->update();
}
