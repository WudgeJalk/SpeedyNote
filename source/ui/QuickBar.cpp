#include "QuickBar.h"

#include "ToolbarButtons.h"
#include "../core/PenBindings.h"

#include <QApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QToolButton>

namespace {
const int kMaxColors = 10;       // The first pen colours; the bar stays small
const int kGapAbovePen = 28;     // The bar sits above the tip, so the hand does not hide it
}

QuickBar::QuickBar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("QuickBar"));
    setAttribute(Qt::WA_StyledBackground, true);  // Let the stylesheet paint the panel
    hide();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    auto addCommand = [&](const char* icon, const QString& tip, const QString& actionId) {
        auto* button = new ActionButton(this);
        button->setThemedIcon(QLatin1String(icon));
        button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this, [this, actionId]() {
            hide();
            emit commandPicked(actionId);
        });
        layout->addWidget(button);
        m_buttons << button;
    };
    addCommand("pen",    tr("Pen"),         QStringLiteral("tool.pen"));
    addCommand("marker", tr("Marker"),      QStringLiteral("tool.marker"));
    addCommand("text",   tr("Highlighter"), QStringLiteral("tool.highlighter"));
    addCommand("eraser", tr("Eraser"),      QStringLiteral("tool.eraser"));
    addCommand("rope",   tr("Lasso"),       QStringLiteral("tool.lasso"));

    auto* line = new ActionButton(this);
    line->setThemedIcon(QStringLiteral("straightLine"));
    line->setToolTip(tr("Straight line on / off"));
    connect(line, &QPushButton::clicked, this, [this]() {
        hide();
        emit straightLinePicked();
    });
    layout->addWidget(line);
    m_buttons << line;

    layout->addSpacing(8);

    for (int i = 0; i < kMaxColors; ++i) {
        auto* color = new QToolButton(this);
        color->setFixedSize(30, 30);
        color->setCursor(Qt::PointingHandCursor);
        connect(color, &QToolButton::clicked, this, [this, i]() {
            hide();
            emit colorPicked(i);
        });
        layout->addWidget(color);
        m_colorButtons << color;
    }

    // Pick on release: 30 ms is fast enough to feel instant, and the timer
    // only runs while the bar is open with the side button held.
    m_releaseTimer.setInterval(30);
    connect(&m_releaseTimer, &QTimer::timeout, this, [this]() {
        if (PenBindings::nativeSideButtonDown()) return;
        m_releaseTimer.stop();
        if (auto* button = qobject_cast<QAbstractButton*>(childAt(mapFromGlobal(QCursor::pos())))) {
            button->click();
        }
        // Released away from the bar: it stays open, a tap can still pick.
    });
}

void QuickBar::applyTheme(bool darkMode)
{
    if (m_themedDark == int(darkMode)) return;
    m_themedDark = int(darkMode);

    // A solid panel, so the icons are readable on any paper colour. Same
    // colours as the action bar, so the two floating bars match.
    // One stylesheet: ButtonStyles::applyToWidget() would replace the panel
    // rule with the button rules, and the bar then had no background at all.
    setStyleSheet(QStringLiteral(
        "#QuickBar { background-color: %1; border: 1px solid %2; border-radius: 10px; }")
        .arg(QString::fromLatin1(darkMode ? "#2a2e32" : "#F5F5F5"),
             QString::fromLatin1(darkMode ? "#4d4d4d" : "#D0D0D0"))
        + ButtonStyles::getStylesheet(darkMode));
    for (ToolbarButton* button : m_buttons) button->setDarkMode(darkMode);
}

void QuickBar::popupAt(const QPoint& tip, const QList<QColor>& colors, int selectedColor, bool darkMode)
{
    applyTheme(darkMode);

    for (int i = 0; i < m_colorButtons.size(); ++i) {
        const bool has = i < colors.size();
        m_colorButtons[i]->setVisible(has);
        if (!has) continue;
        // The ring marks the active colour. It contrasts with the panel, and a
        // thin one stays on the others so a colour close to the panel's own
        // (black on the dark panel) still shows as a dot.
        m_colorButtons[i]->setStyleSheet(QStringLiteral(
            "QToolButton { background-color: %1; border-radius: 15px; border: %2px solid %3; }"
            "QToolButton:hover { border: 3px solid %4; }")
            .arg(colors[i].name())
            .arg(i == selectedColor ? 3 : 1)
            .arg(QString::fromLatin1(i == selectedColor ? (darkMode ? "#ffffff" : "#000000")
                                                        : (darkMode ? "#8a8a8a" : "#707070")),
                 QString::fromLatin1(darkMode ? "#ffffff" : "#000000")));
    }

    adjustSize();
    QPoint topLeft(tip.x() - width() / 2, tip.y() - kGapAbovePen - height());
    if (QWidget* p = parentWidget()) {
        // No room above the pen: go below it instead.
        if (topLeft.y() < 0) topLeft.setY(tip.y() + kGapAbovePen);
        topLeft.setX(qBound(0, topLeft.x(), qMax(0, p->width() - width())));
        topLeft.setY(qBound(0, topLeft.y(), qMax(0, p->height() - height())));
    }
    move(topLeft);
    show();
    raise();

    qApp->installEventFilter(this);
    if (PenBindings::nativeSideButtonDown()) m_releaseTimer.start();
}

bool QuickBar::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::TabletPress:
    case QEvent::TouchBegin: {
        // A press anywhere else closes the bar. The press is NOT swallowed: if
        // the pen starts to write, that stroke must not be lost.
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget != this && !isAncestorOf(widget)) hide();
        break;
    }
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}

void QuickBar::hideEvent(QHideEvent* event)
{
    m_releaseTimer.stop();
    qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}
