#include "PenSettingsPopup.h"
#include "../core/DrawingSettings.h"
#include "../layers/VectorLayer.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace {

QLabel* hintLabel(const QString& text, QWidget* parent)
{
    QLabel* hint = new QLabel(text, parent);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    return hint;
}

QLabel* titleLabel(const QString& text, QWidget* parent)
{
    QLabel* title = new QLabel(text, parent);
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);
    return title;
}

void saveSetting(const char* key, const QVariant& value)
{
    QSettings("SpeedyNote", "App").setValue(QLatin1String(key), value);
    DrawingSettings::reload();
}

constexpr QSize kPreviewSize(96, 40);

} // namespace

QPixmap PenSettingsPopup::penPreview(quint8 penType, const QColor& color, qreal dpr)
{
    QPixmap pixmap(kPreviewSize * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    VectorStroke stroke;
    stroke.color = color;
    stroke.baseThickness = 6.0;
    stroke.penType = penType;
    constexpr int steps = 32;
    for (int i = 0; i <= steps; ++i) {
        const qreal t = qreal(i) / steps;
        StrokePoint pt;
        pt.pos = QPointF(8 + 80 * t, 20 - 10 * std::sin(t * 6.2831853));
        pt.pressure = 0.15 + 0.85 * t;
        stroke.points.append(pt);
    }
    stroke.updateBoundingBox();

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    VectorLayer::renderStroke(painter, stroke);
    return pixmap;
}

PenSettingsPopup::PenSettingsPopup(quint8 penType, QWidget* parent)
    : QFrame(parent, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFrameShape(QFrame::StyledPanel);
    setMaximumWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    // --- Pen type ---
    layout->addWidget(titleLabel(tr("Pen type"), this));
    auto* types = new QHBoxLayout;
    const QString names[] = {tr("Fountain pen"), tr("Ball pen"), tr("Brush pen")};
    const qreal dpr = devicePixelRatioF();
    // The text colour, not the pen's: a black pen would vanish on a dark tile.
    const QColor color = palette().color(QPalette::ButtonText);
    // The chosen tile is filled with the accent colour: draw on it in black or white.
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    const QColor accent = palette().color(QPalette::Accent);
#else
    const QColor accent = palette().color(QPalette::Highlight);
#endif
    const QColor onAccent = accent.lightness() > 140 ? Qt::black : Qt::white;
    for (int type = StrokeStyle::Fountain; type <= StrokeStyle::Brush; ++type) {
        auto* tile = new QToolButton(this);
        tile->setCheckable(true);
        tile->setAutoExclusive(true);
        tile->setChecked(type == penType);
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setIconSize(kPreviewSize);
        QIcon icon(penPreview(quint8(type), color, dpr));
        icon.addPixmap(penPreview(quint8(type), onAccent, dpr), QIcon::Normal, QIcon::On);
        tile->setIcon(icon);
        tile->setText(names[type]);
        connect(tile, &QToolButton::clicked, this, [this, type]() { emit penTypeChosen(quint8(type)); });
        types->addWidget(tile);
    }
    layout->addLayout(types);
    layout->addWidget(hintLabel(tr("Fountain: the line follows the pressure. Ball: an even line. "
                                   "Brush: thin when light, wide when pressed."), this));
    layout->addSpacing(6);

    const DrawingSettings& drawing = DrawingSettings::get();

    // A slider with its value written beside it, saved at each change.
    const auto addSlider = [this, layout](const char* key, int value, int lo, int hi, int step,
                                          std::function<QString(int)> text) {
        auto* row = new QHBoxLayout;
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(lo, hi);
        slider->setSingleStep(step);
        slider->setPageStep(step * 5);
        slider->setValue(value);
        auto* label = new QLabel(text(value), this);
        label->setMinimumWidth(label->fontMetrics().horizontalAdvance(text(hi)) + 4);
        connect(slider, &QSlider::valueChanged, this, [key, label, text](int v) {
            label->setText(text(v));
            saveSetting(key, v);
        });
        row->addWidget(slider, 1);
        row->addWidget(label);
        layout->addLayout(row);
        return slider;
    };

    // --- Stroke stabilisation ---
    layout->addWidget(titleLabel(tr("Stroke stabilisation"), this));
    addSlider("pen/stabilisation", drawing.stabilisation, 0, 100, 5, [](int v) {
        return v == 0 ? tr("Off") : QString::number(v);
    });
    layout->addWidget(hintLabel(tr("Smooths shaky lines. More smoothing makes the line follow "
                                   "the pen a little later."), this));
    layout->addSpacing(6);

    // --- Draw and hold ---
    auto* hold = new QCheckBox(tr("Draw and hold"), this);
    hold->setChecked(drawing.drawAndHold);
    QFont holdFont = hold->font();
    holdFont.setBold(true);
    hold->setFont(holdFont);
    layout->addWidget(hold);
    layout->addWidget(hintLabel(tr("Keep the pen still at the end of a line: it becomes a clean "
                                   "straight line or a smooth curve."), this));
    auto* holdTimeLabel = new QLabel(tr("Hold time"), this);
    layout->addWidget(holdTimeLabel);
    QSlider* holdTime = addSlider("pen/holdMs", drawing.holdMs, 50, 1500, 50, [](int v) {
        return tr("%1 ms").arg(v);
    });
    const auto enableHoldTime = [holdTimeLabel, holdTime](bool on) {
        holdTimeLabel->setEnabled(on);
        holdTime->setEnabled(on);
    };
    enableHoldTime(drawing.drawAndHold);
    connect(hold, &QCheckBox::toggled, this, [enableHoldTime](bool on) {
        enableHoldTime(on);
        saveSetting("pen/drawAndHold", on);
    });
    layout->addSpacing(6);

    // --- Pen buttons ---
    auto* penButtons = new QPushButton(tr("Pen button settings..."), this);
    connect(penButtons, &QPushButton::clicked, this, [this]() {
        close();
        emit penButtonSettingsRequested();
    });
    layout->addWidget(penButtons);
}
