#include "SubToolbar.h"
#include "../widgets/ColorListRow.h"

#include <QFrame>
#include <QPalette>
#include <QApplication>

SubToolbar::SubToolbar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(36);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(PADDING_LEFT, 0, PADDING_RIGHT, 0);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Transparent background -- the parent ExpandableToolButton handles painting
    // Scoped by object name so child dialogs don't inherit transparent bg
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName("SubToolbarWidget");
    setStyleSheet("#SubToolbarWidget { background: transparent; }");
}

QFrame* SubToolbar::addSeparator()
{
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setFixedWidth(2);
    separator->setFixedHeight(SEPARATOR_HEIGHT);

    if (isDarkMode()) {
        separator->setStyleSheet("background-color: #4d4d4d; border: none;");
    } else {
        separator->setStyleSheet("background-color: #D0D0D0; border: none;");
    }

    m_layout->addWidget(separator, 0, Qt::AlignVCenter);
    return separator;
}

void SubToolbar::addWidget(QWidget* widget)
{
    if (widget) {
        m_layout->addWidget(widget, 0, Qt::AlignVCenter);
    }
}

void SubToolbar::addStretch()
{
    m_layout->addStretch();
}

void SubToolbar::setOrientation(Qt::Orientation orientation)
{
    const bool row = orientation == Qt::Horizontal;
    m_layout->setDirection(row ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    m_layout->setContentsMargins(row ? PADDING_LEFT : 0, row ? 0 : PADDING_LEFT,
                                 row ? PADDING_RIGHT : 0, row ? 0 : PADDING_RIGHT);
    m_layout->setAlignment(row ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignTop | Qt::AlignHCenter));
    for (int i = 0; i < m_layout->count(); ++i)
        m_layout->itemAt(i)->setAlignment(row ? Qt::AlignVCenter : Qt::AlignHCenter);

    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (row) setFixedHeight(36); else setFixedWidth(36);

    for (QFrame* frame : findChildren<QFrame*>()) {
        if (frame->frameShape() == QFrame::VLine)
            frame->setFixedSize(row ? QSize(2, SEPARATOR_HEIGHT) : QSize(SEPARATOR_HEIGHT, 2));
    }
    for (ColorListRow* colors : findChildren<ColorListRow*>())
        colors->setOrientation(orientation);
    m_layout->invalidate();
}

bool SubToolbar::isDarkMode() const
{
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);

    const qreal luminance = 0.299 * windowColor.redF()
                          + 0.587 * windowColor.greenF()
                          + 0.114 * windowColor.blueF();

    return luminance < 0.5;
}

void SubToolbar::setDarkMode(bool darkMode)
{
    const auto frames = findChildren<QFrame*>();
    for (QFrame* frame : frames) {
        if (frame->frameShape() == QFrame::VLine) {
            if (darkMode) {
                frame->setStyleSheet("background-color: #4d4d4d; border: none;");
            } else {
                frame->setStyleSheet("background-color: #D0D0D0; border: none;");
            }
        }
    }
    for (ColorListRow* colors : findChildren<ColorListRow*>())
        colors->setDarkMode(darkMode);
}
