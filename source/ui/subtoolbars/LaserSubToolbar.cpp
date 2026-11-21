#include "LaserSubToolbar.h"
#include "../widgets/IconPresetButton.h"
#include "../../core/DrawingSettings.h"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSettings>

QIcon LaserSubToolbar::modeIcon(int mode, bool dark)
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = dark ? QColor(235, 235, 235) : QColor(40, 40, 40);
    QColor glow = ink;
    glow.setAlpha(70);
    if (mode == 0) {   // A dot with its glow
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(QPointF(32, 32), 24, 24);
        p.setBrush(ink);
        p.drawEllipse(QPointF(32, 32), 12, 12);
        return QIcon(pixmap);
    }
    QPainterPath path;   // A trail with its glow
    path.moveTo(8, 44);
    path.cubicTo(20, 6, 34, 58, 44, 26);
    path.quadTo(48, 14, 56, 18);
    for (const QPen& pen : {QPen(glow, 16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
                            QPen(ink, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)}) {
        p.setPen(pen);
        p.drawPath(path);
    }
    return QIcon(pixmap);
}

LaserSubToolbar::LaserSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    const QString tips[2] = {tr("Point: a glowing dot follows the pen"),
                             tr("Line: a glowing trail that fades a moment after the pen stops")};
    for (int i = 0; i < 2; ++i) {
        m_modeButtons[i] = new IconPresetButton(this);
        m_modeButtons[i]->setToolTip(tips[i]);
        addWidget(m_modeButtons[i]);
        connect(m_modeButtons[i], &IconPresetButton::clicked, this, [this, i]() {
            select(i);
            QSettings("SpeedyNote", "App").setValue(QStringLiteral("laser/mode"), i);
            DrawingSettings::reload();
        });
    }
    setDarkMode(palette().color(QPalette::Window).lightness() < 128);
    refreshFromSettings();
}

void LaserSubToolbar::refreshFromSettings()
{
    select(DrawingSettings::get().laserMode);
}

void LaserSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);
    for (int i = 0; i < 2; ++i) {
        m_modeButtons[i]->setDarkMode(darkMode);
        m_modeButtons[i]->setIcon(modeIcon(i, darkMode));
    }
}

void LaserSubToolbar::select(int mode)
{
    for (int i = 0; i < 2; ++i) m_modeButtons[i]->setSelected(i == mode);
}
