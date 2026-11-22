#include "ActionBar.h"
#include "CornerRadiusSlider.h"
#include "../widgets/ActionBarButton.h"

#include <QFrame>
#include <QMenu>
#include <QSlider>
#include <QWidgetAction>
#include <QPalette>
#include <QApplication>
#include <QGraphicsDropShadowEffect>

ActionBar::ActionBar(QWidget* parent)
    : QWidget(parent)
    , m_darkMode(isDarkMode())
{
    // Set fixed width (same as SubToolbar)
    setFixedWidth(ACTIONBAR_WIDTH);
    // A plain QWidget subclass paints its style sheet background only with this.
    setAttribute(Qt::WA_StyledBackground, true);
    
    // Create main layout
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    
    // Apply styling
    setupStyle();
}

void ActionBar::addButton(QWidget* button)
{
    if (button) {
        m_layout->addWidget(button, 0, Qt::AlignHCenter);
    }
}

void ActionBar::addShapeEditButtons()
{
    m_editPointsButton = new ActionBarButton(this);
    m_editPointsButton->setIconName(QStringLiteral("editpoints"));
    m_editPointsButton->setToolTip(tr("Edit points: drag a corner to move it"));
    m_editPointsButton->setCheckable(true);
    m_editPointsButton->hide();
    addButton(m_editPointsButton);
    connect(m_editPointsButton, &ActionBarButton::clicked, this, [this]() {
        m_editPointsButton->setChecked(!m_editPointsButton->isChecked());
        emit editPointsToggled(m_editPointsButton->isChecked());
    });

    m_cornersButton = new ActionBarButton(this);
    m_cornersButton->setIconName(QStringLiteral("corners"));
    m_cornersButton->setToolTip(tr("Corners: from sharp to round"));
    m_cornersButton->hide();
    addButton(m_cornersButton);
    connect(m_cornersButton, &ActionBarButton::clicked, this, [this]() {
        // A popup, not exec(): no event loop inside the click.
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        auto* slider = new CornerRadiusSlider(menu);
        slider->setFraction(m_cornerFraction);
        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(slider);
        menu->addAction(action);
        connect(slider, &CornerRadiusSlider::begin, this, &ActionBar::cornerRadiusBegin);
        connect(slider, &CornerRadiusSlider::preview, this, &ActionBar::cornerRadiusPreview);
        connect(slider, &CornerRadiusSlider::commit, this, &ActionBar::cornerRadiusCommit);
        // A key change still waiting for its pause is kept when the menu closes.
        connect(menu, &QMenu::aboutToHide, slider, &CornerRadiusSlider::finish);
        menu->popup(m_cornersButton->mapToGlobal(QPoint(0, m_cornersButton->height())));
        slider->slider()->setFocus();   // Arrow keys move it at once
    });
}

void ActionBar::setShapeEdit(bool canEditPoints, bool editPointsOn, qreal fraction)
{
    if (!m_editPointsButton) {
        return;
    }
    m_cornerFraction = fraction;
    m_editPointsButton->setVisible(canEditPoints);
    m_editPointsButton->setChecked(canEditPoints && editPointsOn);
    m_cornersButton->setVisible(canEditPoints && fraction >= 0.0);
    adjustSize();
}

void ActionBar::addSeparator()
{
    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setFixedHeight(2);
    separator->setFixedWidth(ACTIONBAR_WIDTH - 2 * PADDING);
    
    separator->setStyleSheet(m_darkMode
        ? "background-color: #4d4d4d; border: none;"
        : "background-color: #D0D0D0; border: none;");
    
    m_layout->addWidget(separator, 0, Qt::AlignHCenter);
}

void ActionBar::addStretch()
{
    m_layout->addStretch();
}

void ActionBar::setOrientation(Qt::Orientation orientation)
{
    const bool horizontal = orientation == Qt::Horizontal;
    if (horizontal == (m_layout->direction() == QBoxLayout::LeftToRight)) {
        return;
    }
    m_layout->setDirection(horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    m_layout->setAlignment(horizontal ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignTop | Qt::AlignHCenter));
    for (int i = 0; i < m_layout->count(); ++i) {
        m_layout->itemAt(i)->setAlignment(horizontal ? Qt::AlignVCenter : Qt::AlignHCenter);
    }
    const int across = ACTIONBAR_WIDTH - 2 * PADDING;
    for (QFrame* frame : findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (frame->frameShape() == QFrame::HLine || frame->frameShape() == QFrame::VLine) {
            frame->setFrameShape(horizontal ? QFrame::VLine : QFrame::HLine);
            frame->setFixedSize(horizontal ? QSize(2, across) : QSize(across, 2));
        }
    }
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (horizontal) {
        setFixedHeight(ACTIONBAR_WIDTH);
    } else {
        setFixedWidth(ACTIONBAR_WIDTH);
    }
    m_layout->invalidate();
    setupStyle();   // A pill has round ends
    adjustSize();
}

void ActionBar::setupStyle()
{
    QString bgColor = m_darkMode ? "#2a2e32" : "#F5F5F5";
    QString borderColor = m_darkMode ? "#4d4d4d" : "#D0D0D0";
    const bool pill = m_layout && m_layout->direction() == QBoxLayout::LeftToRight;

    setStyleSheet(QString(
        "ActionBar {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}"
    ).arg(bgColor, borderColor).arg(pill ? ACTIONBAR_WIDTH / 2 : BORDER_RADIUS));
    
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(8);
    shadow->setOffset(2, 2);
    shadow->setColor(QColor(0, 0, 0, m_darkMode ? 100 : 50));
    setGraphicsEffect(shadow);
}

bool ActionBar::isDarkMode() const
{
    const QPalette& pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    
    // Calculate relative luminance (simplified)
    const qreal luminance = 0.299 * windowColor.redF()
                          + 0.587 * windowColor.greenF()
                          + 0.114 * windowColor.blueF();
    
    return luminance < 0.5;
}

void ActionBar::setDarkMode(bool darkMode)
{
    m_darkMode = darkMode;
    
    setupStyle();
    if (m_editPointsButton) {
        m_editPointsButton->setDarkMode(darkMode);
        m_cornersButton->setDarkMode(darkMode);
    }
    
    const auto frames = findChildren<QFrame*>();
    for (QFrame* frame : frames) {
        if (frame->frameShape() == QFrame::HLine || frame->frameShape() == QFrame::VLine) {
            frame->setStyleSheet(m_darkMode
                ? "background-color: #4d4d4d; border: none;"
                : "background-color: #D0D0D0; border: none;");
        }
    }
}

