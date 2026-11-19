#pragma once

// ============================================================================
// LengthUnit / LengthSpinBox - show lengths in millimetres or pixels
// ============================================================================
// Everything is STORED in page units (1/96 inch, the app calls them px).
// The user picks what is SHOWN: Settings > Page > Units ("ui/lengthUnit").
// The choice is read when a window opens, so it applies to the next window.
// ============================================================================

#include <QSpinBox>
#include <QSettings>
#include <QLocale>
#include <QCoreApplication>

namespace LengthUnit {

constexpr double MM_PER_UNIT = 25.4 / 96.0;

inline bool& mmFlag()
{
    // Millimetres by default: they are the real size on paper.
    static bool mm = QSettings("SpeedyNote", "App").value("ui/lengthUnit", "mm").toString() != QLatin1String("px");
    return mm;
}
inline bool useMm() { return mmFlag(); }
inline void setUseMm(bool mm)
{
    mmFlag() = mm;
    QSettings("SpeedyNote", "App").setValue("ui/lengthUnit", mm ? "mm" : "px");
}

/// Multiply page units by this to get the shown number.
inline double factor() { return useMm() ? MM_PER_UNIT : 1.0; }
inline QString suffix() { return useMm() ? QStringLiteral(" mm") : QStringLiteral(" px"); }
inline QString name() { return useMm() ? QStringLiteral("mm") : QStringLiteral("px"); }
inline int decimals() { return useMm() ? 2 : 0; }

/// "210 × 297 mm" or "794 × 1123 px".
inline QString sizeText(double widthUnits, double heightUnits)
{
    if (useMm()) {
        return QStringLiteral("%1 × %2 mm").arg(qRound(widthUnits * MM_PER_UNIT))
                                           .arg(qRound(heightUnits * MM_PER_UNIT));
    }
    return QStringLiteral("%1 × %2 px").arg(qRound(widthUnits)).arg(qRound(heightUnits));
}

} // namespace LengthUnit

/**
 * @brief A QSpinBox whose value() stays in whole page units, but that shows
 *        and accepts the user's unit.
 *
 * A drop-in for the spacing boxes: the code around them keeps reading ints.
 * shortcut: the value is a whole number of page units, so 5 mm is kept as
 * 19 units = 5.03 mm. An exact 5 mm grid needs fractional spacing in Page.
 */
class LengthSpinBox : public QSpinBox {
public:
    explicit LengthSpinBox(QWidget* parent = nullptr) : QSpinBox(parent)
    {
        setSuffix(LengthUnit::suffix());
    }

    void stepBy(int steps) override
    {
        // In millimetres the arrows move by about 1 mm (4 units), whatever
        // pixel step the caller set.
        if (LengthUnit::useMm()) setValue(value() + steps * 4);
        else QSpinBox::stepBy(steps);
    }

protected:
    QString textFromValue(int units) const override
    {
        if (!LengthUnit::useMm()) return QSpinBox::textFromValue(units);
        return locale().toString(units * LengthUnit::MM_PER_UNIT, 'f', 2);
    }

    int valueFromText(const QString& text) const override
    {
        QString t = text;
        t.remove(suffix());
        t = t.trimmed();
        bool ok = false;
        double shown = locale().toDouble(t, &ok);
        if (!ok) shown = t.replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        return qRound(shown / LengthUnit::factor());
    }

    QValidator::State validate(QString&, int&) const override
    {
        // Decimals are typed here, which the integer validator refuses. Let
        // the text through; valueFromText and the range tidy it on Enter.
        return QValidator::Acceptable;
    }
};
