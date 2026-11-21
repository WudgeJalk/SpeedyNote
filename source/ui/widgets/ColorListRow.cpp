#include "ColorListRow.h"
#include "ColorPresetButton.h"

#include <QApplication>
#include <QBoxLayout>
#include <QColorDialog>
#include <QMenu>
#include <QPalette>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QSettings>
#include <QToolButton>
#include <QWheelEvent>

namespace {
constexpr int kSwatch = ColorPresetButton::kDefaultButtonSize;
constexpr int kSpacing = 4;
}

ColorListRow::ColorListRow(const QString& group, const QList<QColor>& defaults, QWidget* parent)
    : QWidget(parent)
    , m_group(group)
    , m_defaults(defaults)
{
    m_visible = readVisibleCount();

    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(kSpacing);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The bar paints the background; the scroll area must not paint its own.
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea, #qt_scrollarea_viewport, #ColorListInner { background: transparent; border: none; }"));
    m_scroll->viewport()->setAutoFillBackground(false);
    m_inner = new QWidget;
    m_inner->setObjectName(QStringLiteral("ColorListInner"));
    m_innerLayout = new QBoxLayout(QBoxLayout::LeftToRight, m_inner);
    m_innerLayout->setContentsMargins(0, 0, 0, 0);
    m_innerLayout->setSpacing(kSpacing);
    m_scroll->setWidget(m_inner);
    m_inner->setAutoFillBackground(false);  // After setWidget(), which turns it on

    // Drag to scroll, with a finger, a pen or the mouse; no bounce on a short list.
    QScroller::grabGesture(m_scroll->viewport(), QScroller::LeftMouseButtonGesture);
    QScrollerProperties props = QScroller::scroller(m_scroll->viewport())->scrollerProperties();
    props.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                          QVariant::fromValue(QScrollerProperties::OvershootAlwaysOff));
    props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                          QVariant::fromValue(QScrollerProperties::OvershootAlwaysOff));
    QScroller::scroller(m_scroll->viewport())->setScrollerProperties(props);
    m_scroll->viewport()->installEventFilter(this);  // The wheel scrolls along the row
    m_layout->addWidget(m_scroll);

    m_add = new QToolButton(this);
    m_add->setFixedSize(kSwatch, kSwatch);
    m_add->setIconSize(QSize(18, 18));
    m_add->setAutoRaise(true);
    m_add->setCursor(Qt::PointingHandCursor);
    m_add->setToolTip(tr("Add a color"));
    connect(m_add, &QToolButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(currentColor(), this, tr("Add a color"));
        if (color.isValid()) addColor(color);
    });
    m_layout->addWidget(m_add);

    setDarkMode(QApplication::palette().color(QPalette::Window).lightness() < 128);
    m_colors = load();
    sync();
}

QList<QColor> ColorListRow::load() const
{
    QSettings settings;
    settings.beginGroup(m_group);
    QList<QColor> list;
    if (settings.contains(QStringLiteral("colors"))) {
        const QStringList names = settings.value(QStringLiteral("colors")).toStringList();
        for (const QString& name : names) {
            const QColor color(name);
            if (color.isValid()) list << color;
        }
    } else {
        // Before the first edit: the old three presets, then the other defaults.
        for (int i = 0; i < m_defaults.size(); ++i) {
            const QString key = QStringLiteral("color%1").arg(i + 1);
            const QColor old = (i < 3 && settings.contains(key)) ? settings.value(key).value<QColor>() : QColor();
            list << (old.isValid() ? old : m_defaults[i]);
        }
    }
    return list.isEmpty() ? m_defaults : list;
}

void ColorListRow::save() const
{
    QStringList names;
    for (const QColor& color : m_colors) names << color.name(QColor::HexArgb);
    QSettings settings;
    settings.setValue(m_group + QStringLiteral("/colors"), names);
}

void ColorListRow::sync()
{
    while (m_swatches.size() > m_colors.size()) {
        ColorPresetButton* swatch = m_swatches.takeLast();
        m_innerLayout->removeWidget(swatch);
        swatch->hide();
        swatch->deleteLater();  // It can be the swatch whose menu asked for the removal
    }
    while (m_swatches.size() < m_colors.size()) {
        auto* swatch = new ColorPresetButton(m_inner);
        swatch->setToolTip(tr("Click to select, click again to edit"));
        connect(swatch, &ColorPresetButton::clicked, this, [this, swatch]() {
            setSelectedIndex(m_swatches.indexOf(swatch), true);
        });
        connect(swatch, &ColorPresetButton::editRequested, this, [this, swatch]() {
            showMenu(m_swatches.indexOf(swatch));
        });
        m_innerLayout->addWidget(swatch);
        m_swatches << swatch;
    }
    for (int i = 0; i < m_swatches.size(); ++i) {
        m_swatches[i]->setColor(m_colors[i]);
        m_swatches[i]->setSelected(i == m_selected);
    }

    const int n = m_colors.size();
    const int fit = (qMin(m_room, INT_MAX - kSpacing) + kSpacing) / (kSwatch + kSpacing);
    const int shown = qMax(qMin(1, n), qMin(qMin(n, m_visible), fit));
    const int length = n * kSwatch + qMax(0, n - 1) * kSpacing;
    const int view = shown * kSwatch + qMax(0, shown - 1) * kSpacing;
    m_inner->setFixedSize(m_vertical ? QSize(kSwatch, length) : QSize(length, kSwatch));
    m_scroll->setFixedSize(m_vertical ? QSize(kSwatch, view) : QSize(view, kSwatch));
}

void ColorListRow::setSelectedIndex(int index, bool emitSignal)
{
    if (m_colors.isEmpty() || index < 0) return;
    m_selected = qMin(index, m_colors.size() - 1);
    for (int i = 0; i < m_swatches.size(); ++i) m_swatches[i]->setSelected(i == m_selected);
    m_scroll->ensureWidgetVisible(m_swatches[m_selected], 0, 0);
    if (emitSignal) emit colorSelected(m_colors[m_selected]);
}

void ColorListRow::cycle()
{
    if (!m_colors.isEmpty()) setSelectedIndex((m_selected + 1) % m_colors.size(), true);
}

void ColorListRow::reload()
{
    const int visible = readVisibleCount();
    const QList<QColor> list = load();
    if (list == m_colors && visible == m_visible) return;
    const QColor before = currentColor();
    m_visible = visible;
    m_colors = list;
    m_selected = qBound(0, m_selected, m_colors.size() - 1);
    sync();
    if (currentColor() != before) emit colorSelected(currentColor());
}

void ColorListRow::fitTo(int px)
{
    // The "add" button and its gap take their part of the room.
    const int room = px >= INT_MAX - kSwatch ? INT_MAX : px - kSpacing - kSwatch;
    if (room != m_room) {
        m_room = room;
        sync();   // Same sizes: Qt skips them, so this asks for no new layout
    }
}

int ColorListRow::readVisibleCount()
{
    return qMax(1, QSettings().value(QStringLiteral("toolbar/visibleColors"), 8).toInt());
}

void ColorListRow::addColor(const QColor& color)
{
    m_colors << color;
    sync();
    save();
    setSelectedIndex(m_colors.size() - 1, true);
    emit listChanged();
}

void ColorListRow::setColorAt(int index, const QColor& color)
{
    if (index < 0 || index >= m_colors.size()) return;
    m_colors[index] = color;
    sync();
    save();
    if (index == m_selected) emit colorSelected(color);
    emit listChanged();
}

void ColorListRow::moveColor(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_colors.size() || to >= m_colors.size() || from == to) return;
    m_colors.move(from, to);
    if (from == m_selected) m_selected = to;
    else if (from < m_selected && to >= m_selected) --m_selected;
    else if (from > m_selected && to <= m_selected) ++m_selected;
    sync();
    save();
    emit listChanged();
}

void ColorListRow::removeColor(int index)
{
    if (m_colors.size() <= 1 || index < 0 || index >= m_colors.size()) return;
    const bool removedPicked = (index == m_selected);
    m_colors.removeAt(index);
    if (index < m_selected) --m_selected;
    m_selected = qMin(m_selected, m_colors.size() - 1);
    sync();
    save();
    if (removedPicked) emit colorSelected(currentColor());
    emit listChanged();
}

void ColorListRow::showMenu(int index)
{
    if (index < 0 || index >= m_colors.size()) return;
    QMenu menu(this);
    QAction* change = menu.addAction(tr("Change..."));
    QAction* earlier = menu.addAction(tr("Move earlier"));
    QAction* later = menu.addAction(tr("Move later"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Remove"));
    earlier->setEnabled(index > 0);
    later->setEnabled(index < m_colors.size() - 1);
    remove->setEnabled(m_colors.size() > 1);

    QWidget* swatch = m_swatches.value(index);
    const QPoint at = swatch->mapToGlobal(m_vertical ? QPoint(swatch->width(), 0) : QPoint(0, swatch->height()));
    QAction* picked = menu.exec(at);
    if (index >= m_colors.size()) return;
    if (picked == change) {
        const QColor color = QColorDialog::getColor(m_colors[index], this, tr("Change color"));
        if (color.isValid()) setColorAt(index, color);
    } else if (picked == earlier) {
        moveColor(index, index - 1);
    } else if (picked == later) {
        moveColor(index, index + 1);
    } else if (picked == remove) {
        removeColor(index);
    }
}

void ColorListRow::setOrientation(Qt::Orientation orientation)
{
    m_vertical = (orientation == Qt::Vertical);
    const QBoxLayout::Direction along = m_vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    m_layout->setDirection(along);
    m_innerLayout->setDirection(along);
    sync();
}

void ColorListRow::setDarkMode(bool dark)
{
    m_add->setIcon(QIcon(dark ? QStringLiteral(":/resources/icons/addtab_reversed.png")
                              : QStringLiteral(":/resources/icons/addtab.png")));
}

bool ColorListRow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_scroll->viewport() && event->type() == QEvent::Wheel) {
        const QPoint delta = static_cast<QWheelEvent*>(event)->angleDelta();
        const int step = qAbs(delta.y()) >= qAbs(delta.x()) ? delta.y() : delta.x();
        QScrollBar* bar = m_vertical ? m_scroll->verticalScrollBar() : m_scroll->horizontalScrollBar();
        bar->setValue(bar->value() - step * (kSwatch + kSpacing) / 120);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
