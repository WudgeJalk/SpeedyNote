// The "Drawing" tab of the settings: every drawing threshold the user can
// change. Each stage of the toolbar work adds its group here.

#include "ControlPanelDialog.h"
#include "MainWindow.h"
#include "core/DocumentViewport.h"
#include "core/DrawingSettings.h"
#include "core/PenHaptics.h"
#include "strokes/StrokeStyle.h"
#include "ui/StrokePresetDialog.h"
#include "ui/widgets/LengthSpinBox.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

namespace {

QLabel* hintLabel(const QString& text, QWidget* parent)
{
    QLabel* hint = new QLabel(text, parent);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    return hint;
}

} // namespace

void ControlPanelDialog::createDrawingTab()
{
    QWidget* drawingTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(drawingTab);

    // --- Lines: dash and dot spacing ---
    QGroupBox* linesGroup = new QGroupBox(tr("Dashed and dotted lines"), drawingTab);
    QFormLayout* linesLayout = new QFormLayout(linesGroup);
    const StrokeStyle::Look& look = StrokeStyle::look();
    const auto ratioSpin = [linesGroup](qreal value) {
        QDoubleSpinBox* spin = new QDoubleSpinBox(linesGroup);
        spin->setRange(0.5, 20.0);   // Same bounds as DrawingSettings::reload()
        spin->setSingleStep(0.5);
        spin->setDecimals(1);
        spin->setSuffix(tr(" × line width"));
        spin->setValue(value);
        return spin;
    };
    dashLengthSpin = ratioSpin(look.dashLength);
    dashGapSpin = ratioSpin(look.dashGap);
    dotGapSpin = ratioSpin(look.dotGap);
    linesLayout->addRow(tr("Dash length:"), dashLengthSpin);
    linesLayout->addRow(tr("Gap between dashes:"), dashGapSpin);
    linesLayout->addRow(tr("Gap between dots:"), dotGapSpin);
    linesLayout->addRow(hintLabel(
        tr("Measured in widths of the line itself, so a thicker line gets longer "
           "dashes and wider gaps. Changes every dashed and dotted line, old ones too."),
        linesGroup));
    layout->addWidget(linesGroup);

    // --- Eraser: filter and auto deselect (also in the eraser window) ---
    QGroupBox* eraserGroup = new QGroupBox(tr("Eraser"), drawingTab);
    QVBoxLayout* eraserLayout = new QVBoxLayout(eraserGroup);
    const DrawingSettings& drawing = DrawingSettings::get();
    eraserPenCheck = new QCheckBox(tr("Erase pen lines"), eraserGroup);
    eraserPenCheck->setChecked(drawing.eraserPen);
    eraserMarkerCheck = new QCheckBox(tr("Erase marker lines"), eraserGroup);
    eraserMarkerCheck->setChecked(drawing.eraserMarker);
    eraserAutoDeselectCheck = new QCheckBox(tr("Go back to the last tool after erasing"), eraserGroup);
    eraserAutoDeselectCheck->setChecked(drawing.eraserAutoDeselect);
    eraserLayout->addWidget(eraserPenCheck);
    eraserLayout->addWidget(eraserMarkerCheck);
    eraserLayout->addWidget(hintLabel(
        tr("The eraser leaves alone the lines of a tool that is off."), eraserGroup));
    eraserLayout->addWidget(eraserAutoDeselectCheck);
    eraserLayout->addWidget(hintLabel(
        tr("When you lift the eraser, the tool you used before it comes back."), eraserGroup));
    layout->addWidget(eraserGroup);

    // --- Pen: stabilisation and draw and hold (also in the pen window) ---
    QGroupBox* penGroup = new QGroupBox(tr("Pen"), drawingTab);
    QFormLayout* penLayout = new QFormLayout(penGroup);
    stabilisationSpin = new QSpinBox(penGroup);
    stabilisationSpin->setRange(0, 100);   // Same bounds as DrawingSettings::reload()
    stabilisationSpin->setSpecialValueText(tr("Off"));
    stabilisationSpin->setValue(drawing.stabilisation);
    penLayout->addRow(tr("Stroke stabilisation:"), stabilisationSpin);
    penLayout->addRow(hintLabel(tr("Smooths shaky lines. More smoothing makes the line "
                                   "follow the pen a little later."), penGroup));
    drawAndHoldCheck = new QCheckBox(tr("Draw and hold"), penGroup);
    drawAndHoldCheck->setChecked(drawing.drawAndHold);
    penLayout->addRow(drawAndHoldCheck);
    holdMsSpin = new QSpinBox(penGroup);
    holdMsSpin->setRange(50, 1500);
    holdMsSpin->setSingleStep(50);
    holdMsSpin->setSuffix(tr(" ms"));
    holdMsSpin->setValue(drawing.holdMs);
    penLayout->addRow(tr("Hold time:"), holdMsSpin);
    holdStillSpin = new LengthSpinBox(penGroup);
    holdStillSpin->setRange(1, 40);
    holdStillSpin->setValue(drawing.holdStillPx);
    penLayout->addRow(tr("The pen counts as still within:"), holdStillSpin);
    holdMinLengthSpin = new LengthSpinBox(penGroup);
    holdMinLengthSpin->setRange(0, 400);
    holdMinLengthSpin->setSingleStep(5);
    holdMinLengthSpin->setValue(drawing.holdMinLength);
    penLayout->addRow(tr("Shortest line to clean:"), holdMinLengthSpin);
    penLayout->addRow(hintLabel(
        tr("Keep the pen still at the end of a line for the hold time: it becomes a clean "
           "straight line or a smooth curve. Lengths are measured on the screen."), penGroup));
    for (QWidget* w : {static_cast<QWidget*>(holdMsSpin), static_cast<QWidget*>(holdStillSpin),
                       static_cast<QWidget*>(holdMinLengthSpin)}) {
        w->setEnabled(drawing.drawAndHold);
        connect(drawAndHoldCheck, &QCheckBox::toggled, w, &QWidget::setEnabled);
    }
    layout->addWidget(penGroup);

    // --- Shapes: how a held or free-shape line is cleaned ---
    QGroupBox* shapesGroup = new QGroupBox(tr("Shapes"), drawingTab);
    QFormLayout* shapesLayout = new QFormLayout(shapesGroup);
    cornerAngleSpin = new QSpinBox(shapesGroup);
    cornerAngleSpin->setRange(10, 90);   // Same bounds as DrawingSettings::reload()
    cornerAngleSpin->setSuffix(tr("°"));
    cornerAngleSpin->setValue(drawing.cornerAngle);
    shapesLayout->addRow(tr("A turn this sharp is a corner:"), cornerAngleSpin);
    closePercentSpin = new QSpinBox(shapesGroup);
    closePercentSpin->setRange(0, 50);
    closePercentSpin->setSuffix(tr(" % of its size"));
    closePercentSpin->setValue(drawing.closePercent);
    shapesLayout->addRow(tr("Close the shape when the ends are within:"), closePercentSpin);
    snapPxSpin = new LengthSpinBox(shapesGroup);
    snapPxSpin->setRange(0, 60);
    snapPxSpin->setValue(drawing.snapPx);
    shapesLayout->addRow(tr("Ends snap to a line within:"), snapPxSpin);
    // Same bounds as DrawingSettings::reload()
    const auto addSpin = [&](QSpinBox*& spin, int lo, int hi, int value, const QString& suffix,
                             const QString& label) {
        spin = new QSpinBox(shapesGroup);
        spin->setRange(lo, hi);
        spin->setSuffix(suffix);
        spin->setValue(value);
        shapesLayout->addRow(label, spin);
    };
    addSpin(roughPercentSpin, 0, 40, drawing.roughPercent, tr(" % of its size"),
            tr("Exact shape when the line is within:"));
    addSpin(equalPercentSpin, 0, 40, drawing.equalPercent, tr(" %"),
            tr("Circle or square when sides differ by under:"));
    addSpin(angleSnapSpin, 0, 30, drawing.angleSnap, tr("°"),
            tr("Parallel or square sides within:"));
    addSpin(levelAngleSpin, 0, 20, drawing.levelAngle, tr("°"),
            tr("Level a shape within:"));
    handArrowsCheck = new QCheckBox(tr("Hand-drawn arrows"), shapesGroup);
    handArrowsCheck->setChecked(drawing.handArrows);
    shapesLayout->addRow(handArrowsCheck);
    handArrowHeadCombo = new QComboBox(shapesGroup);
    handArrowHeadCombo->addItem(tr("Follow my scribble (V = open, closed triangle = filled)"));
    handArrowHeadCombo->addItem(tr("Always open"));
    handArrowHeadCombo->addItem(tr("Always filled"));
    handArrowHeadCombo->setCurrentIndex(drawing.handArrowHead);
    handArrowHeadCombo->setEnabled(drawing.handArrows);
    connect(handArrowsCheck, &QCheckBox::toggled, handArrowHeadCombo, &QWidget::setEnabled);
    shapesLayout->addRow(tr("Head of a hand-drawn arrow:"), handArrowHeadCombo);
    shapesLayout->addRow(hintLabel(StrokePresetDialog::handArrowHint(), shapesGroup));
    shapesLayout->addRow(hintLabel(
        tr("A closed line becomes an exact circle, ellipse, square, rectangle, rhombus, "
           "parallelogram, trapezoid or polygon; 0 % keeps the drawn shape, only smoothed. "
           "Draw a line and a quick V, hook or small triangle at its end, in the same stroke "
           "or right after: the line becomes an arrow and keeps its curve. The snap distance "
           "is measured on the screen; 0 turns snapping off."), shapesGroup));
    layout->addWidget(shapesGroup);

    // --- Laser pointer (Point or Line is chosen in the laser bar) ---
    QGroupBox* laserGroup = new QGroupBox(tr("Laser pointer"), drawingTab);
    QFormLayout* laserLayout = new QFormLayout(laserGroup);
    laserHoldSpin = new QSpinBox(laserGroup);
    laserHoldSpin->setRange(0, 10000);   // Same bounds as DrawingSettings::reload()
    laserHoldSpin->setSingleStep(100);
    laserHoldSpin->setSuffix(tr(" ms"));
    laserHoldSpin->setValue(drawing.laserHoldMs);
    laserLayout->addRow(tr("Wait before the lines fade:"), laserHoldSpin);
    laserFadeSpin = new QSpinBox(laserGroup);
    laserFadeSpin->setRange(0, 5000);
    laserFadeSpin->setSingleStep(100);
    laserFadeSpin->setSuffix(tr(" ms"));
    laserFadeSpin->setValue(drawing.laserFadeMs);
    laserLayout->addRow(tr("Fade time:"), laserFadeSpin);
    laserColorChoice = drawing.laserColor;
    laserColorButton = new QPushButton(laserGroup);
    laserColorButton->setFixedSize(100, 30);
    laserColorButton->setStyleSheet(QString("background-color: %1").arg(laserColorChoice.name()));
    connect(laserColorButton, &QPushButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(laserColorChoice, this, tr("Laser colour"));
        if (!chosen.isValid()) return;
        laserColorChoice = chosen;
        laserColorButton->setStyleSheet(QString("background-color: %1").arg(chosen.name()));
    });
    laserLayout->addRow(tr("Colour:"), laserColorButton);
    laserLayout->addRow(hintLabel(
        tr("The laser lines stay while you keep pointing. When the pen stops for the wait "
           "time, they all fade out together. The dot of Point mode fades as soon as the pen lifts. "
           "Nothing is saved."), laserGroup));
    layout->addWidget(laserGroup);

    // --- Ruler (shown and hidden with the toolbar's ruler button, or R) ---
    QGroupBox* rulerGroup = new QGroupBox(tr("Ruler"), drawingTab);
    QFormLayout* rulerLayout = new QFormLayout(rulerGroup);
    rulerSnapSpin = new LengthSpinBox(rulerGroup);
    rulerSnapSpin->setRange(0, 80);   // Same bounds as DrawingSettings::reload()
    rulerSnapSpin->setValue(drawing.rulerSnapPx);
    rulerLayout->addRow(tr("A line follows the edge when it starts within:"), rulerSnapSpin);
    rulerAngleUnitCombo = new QComboBox(rulerGroup);
    rulerAngleUnitCombo->addItem(tr("Degrees"));
    rulerAngleUnitCombo->addItem(tr("Radians"));
    rulerAngleUnitCombo->setCurrentIndex(drawing.rulerRadians ? 1 : 0);
    rulerLayout->addRow(tr("Show the angle in:"), rulerAngleUnitCombo);
    rulerLayout->addRow(hintLabel(
        tr("Start a line just outside a long edge of the ruler: it follows the edge. The distance "
           "is measured on the screen; 0 turns this off. Drag the ruler to move it, drag its centre "
           "to turn it, and tap its angle for more."), rulerGroup));
    layout->addWidget(rulerGroup);

    // --- Finger: a tap selects a line or a shape (only while fingers pan) ---
    QGroupBox* fingerGroup = new QGroupBox(tr("Finger"), drawingTab);
    QFormLayout* fingerLayout = new QFormLayout(fingerGroup);
    fingerTapCheck = new QCheckBox(tr("A finger tap selects lines and shapes"), fingerGroup);
    fingerTapCheck->setChecked(drawing.fingerTapSelects);
    fingerLayout->addRow(fingerTapCheck);
    tapMsSpin = new QSpinBox(fingerGroup);
    tapMsSpin->setRange(100, 1000);   // Same bounds as DrawingSettings::reload()
    tapMsSpin->setSingleStep(50);
    tapMsSpin->setSuffix(tr(" ms"));
    tapMsSpin->setValue(drawing.tapMs);
    fingerLayout->addRow(tr("A tap lasts at most:"), tapMsSpin);
    tapSlopSpin = new LengthSpinBox(fingerGroup);
    tapSlopSpin->setRange(2, 40);
    tapSlopSpin->setValue(drawing.tapSlopPx);
    fingerLayout->addRow(tr("A tap moves less than:"), tapSlopSpin);
    tapReachSpin = new LengthSpinBox(fingerGroup);
    tapReachSpin->setRange(4, 60);
    tapReachSpin->setValue(drawing.tapReachPx);
    fingerLayout->addRow(tr("A tap hits a line within:"), tapReachSpin);
    fingerLayout->addRow(hintLabel(
        tr("Tap a straight line, an arrow, a cleaned shape, a shape or a connector with one finger "
           "to select it, whatever the tool. Handwriting is never selected this way. A tap on empty "
           "paper, or the pen pressing elsewhere, lets go and gives the tool back. Works while "
           "touch gestures are on; a finger drag still moves the page. Turn it off if you rest "
           "fingers on the screen."), fingerGroup));
    for (QWidget* w : {static_cast<QWidget*>(tapMsSpin), static_cast<QWidget*>(tapSlopSpin),
                       static_cast<QWidget*>(tapReachSpin)}) {
        w->setEnabled(drawing.fingerTapSelects);
        connect(fingerTapCheck, &QCheckBox::toggled, w, &QWidget::setEnabled);
    }
    layout->addWidget(fingerGroup);

    layout->addStretch();
    // Each stage adds a group, so the tab scrolls instead of growing the dialog.
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(drawingTab);
    tabWidget->addTab(scroll, tr("Drawing"));
    drawingTabPage = scroll;
}

void ControlPanelDialog::addPenVibrationGroup(QVBoxLayout* layout, QWidget* parent)
{
    const PenHaptics::Status status = PenHaptics::status();
    if (status == PenHaptics::Status::NotBuilt) {
        return;   // No pen vibration on this platform or build
    }
    QLabel* title = new QLabel(tr("Pen Vibration"), parent);
    title->setStyleSheet("font-weight: bold; font-size: 14px;");
    layout->addWidget(title);
    layout->addWidget(hintLabel(
        status == PenHaptics::Status::Vibration ? tr("A pen with vibration was found.")
        : status == PenHaptics::Status::NoVibration
            ? tr("No pen with vibration found. It needs Windows 11 and a pen with haptics, "
                 "such as the Surface Slim Pen 2.")
            : tr("No pen with vibration found yet: touch the screen with your pen, then open "
                 "this page again."), parent));
    // A pen without a strength setting plays its own fixed strength.
    const bool strengths = status != PenHaptics::Status::Vibration || PenHaptics::intensitySupported();

    QFormLayout* form = new QFormLayout();
    const DrawingSettings& drawing = DrawingSettings::get();
    // Applies at once, like the rest of this tab.
    const auto addRow = [&](const QString& text, const QString& help, const QString& key, bool on,
                            int strength) {
        QCheckBox* check = new QCheckBox(text, parent);
        check->setChecked(on);
        QSlider* slider = new QSlider(Qt::Horizontal, parent);
        slider->setRange(0, 100);   // Same bounds as DrawingSettings::reload()
        slider->setValue(strength);
        slider->setEnabled(on && strengths);
        QLabel* percent = new QLabel(QStringLiteral("%1 %").arg(strength), parent);
        percent->setMinimumWidth(48);
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(slider);
        row->addWidget(percent);
        form->addRow(check);
        form->addRow(tr("Strength:"), row);
        form->addRow(hintLabel(help, parent));
        connect(check, &QCheckBox::toggled, this, [key, slider, strengths](bool checked) {
            slider->setEnabled(checked && strengths);
            QSettings("SpeedyNote", "App").setValue(key, checked);
            DrawingSettings::reload();
        });
        const auto save = [key, slider]() {
            QSettings("SpeedyNote", "App").setValue(key + QStringLiteral("Strength"), slider->value());
            DrawingSettings::reload();
        };
        connect(slider, &QSlider::valueChanged, this, [slider, percent, save](int value) {
            percent->setText(QStringLiteral("%1 %").arg(value));
            if (!slider->isSliderDown()) save();   // A drag saves once, when it ends
        });
        connect(slider, &QSlider::sliderReleased, this, save);
    };
    addRow(tr("Click when something snaps into place"),
           tr("A held line or shape is cleaned, a hand-drawn arrow is made, a connector end "
              "attaches to a shape, or a line starts to follow the ruler."),
           QStringLiteral("haptics/snap"), drawing.hapticSnap, drawing.hapticSnapStrength);
    addRow(tr("Light click when the tool changes"),
           tr("From the toolbar, a pen button, the quick bar or a keyboard shortcut."),
           QStringLiteral("haptics/tool"), drawing.hapticTool, drawing.hapticToolStrength);
    addRow(tr("Paper feel while writing"),
           tr("A light texture while the pen touches the page: ink for the fountain and ball pens, "
              "brush, marker, and the eraser when the pen has it."),
           QStringLiteral("haptics/writing"), drawing.hapticWriting, drawing.hapticWritingStrength);
    layout->addLayout(form);
}

void ControlPanelDialog::applyDrawingTab()
{
    QSettings settings("SpeedyNote", "App");
    settings.setValue("drawing/dashLength", dashLengthSpin->value());
    settings.setValue("drawing/dashGap", dashGapSpin->value());
    settings.setValue("drawing/dotGap", dotGapSpin->value());
    settings.setValue("drawing/eraserPen", eraserPenCheck->isChecked());
    settings.setValue("drawing/eraserMarker", eraserMarkerCheck->isChecked());
    settings.setValue("drawing/eraserAutoDeselect", eraserAutoDeselectCheck->isChecked());
    settings.setValue("pen/stabilisation", stabilisationSpin->value());
    settings.setValue("pen/drawAndHold", drawAndHoldCheck->isChecked());
    settings.setValue("pen/holdMs", holdMsSpin->value());
    settings.setValue("pen/holdStillPx", holdStillSpin->value());
    settings.setValue("pen/holdMinLength", holdMinLengthSpin->value());
    settings.setValue("pen/handArrows", handArrowsCheck->isChecked());
    settings.setValue("pen/handArrowHead", handArrowHeadCombo->currentIndex());
    settings.setValue("shapes/cornerAngle", cornerAngleSpin->value());
    settings.setValue("shapes/closePercent", closePercentSpin->value());
    settings.setValue("shapes/snapPx", snapPxSpin->value());
    settings.setValue("shapes/roughPercent", roughPercentSpin->value());
    settings.setValue("shapes/equalPercent", equalPercentSpin->value());
    settings.setValue("shapes/angleSnap", angleSnapSpin->value());
    settings.setValue("shapes/levelAngle", levelAngleSpin->value());
    settings.setValue("laser/holdMs", laserHoldSpin->value());
    settings.setValue("laser/fadeMs", laserFadeSpin->value());
    settings.setValue("laser/color", laserColorChoice);
    settings.setValue("ruler/snapPx", rulerSnapSpin->value());
    settings.setValue("ruler/radians", rulerAngleUnitCombo->currentIndex() == 1);
    settings.setValue("touch/tapSelects", fingerTapCheck->isChecked());
    settings.setValue("touch/tapMs", tapMsSpin->value());
    settings.setValue("touch/tapSlopPx", tapSlopSpin->value());
    settings.setValue("touch/tapReachPx", tapReachSpin->value());

    // The stroke caches rebuild on their next paint, and the ruler shows its
    // unit; ask every view for one.
    DrawingSettings::reload();
    if (mainWindowRef) {
        for (DocumentViewport* viewport : mainWindowRef->findChildren<DocumentViewport*>()) {
            viewport->update();
        }
    }
}
