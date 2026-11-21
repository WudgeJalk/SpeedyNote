#include "StrokePresetDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

StrokePresetDialog::StrokePresetDialog(const StrokePreset& current, const StrokePreset& defaults,
                                       qreal minWidthLimit, qreal maxWidthLimit, QWidget* parent)
    : ThicknessEditDialog(current.width, minWidthLimit, maxWidthLimit, current.minWidth, parent)
    , m_defaults(defaults)
{
    setWindowTitle(tr("Stroke settings"));
    setFixedWidth(340);

    auto* form = new QFormLayout;

    auto* styles = new QHBoxLayout;
    m_lineStyle = new QButtonGroup(this);
    const QString names[] = {tr("Solid"), tr("Dashed"), tr("Dotted")};
    for (int i = 0; i < 3; ++i) {
        auto* choice = new QRadioButton(names[i], this);
        m_lineStyle->addButton(choice, i);
        styles->addWidget(choice);
    }
    form->addRow(tr("Line:"), styles);

    m_rounded = new QCheckBox(tr("Rounded corners"), this);
    form->addRow(QString(), m_rounded);

    auto arrowBox = [this]() {
        auto* box = new QComboBox(this);
        box->addItems({tr("None"), tr("Open arrow"), tr("Filled arrow")});
        return box;
    };
    m_arrowStart = arrowBox();
    m_arrowEnd = arrowBox();
    m_arrowStart->setToolTip(handArrowHint());
    m_arrowEnd->setToolTip(handArrowHint());
    form->addRow(tr("Start arrow:"), m_arrowStart);
    form->addRow(tr("End arrow:"), m_arrowEnd);

    // Above the OK / Cancel row the base dialog ends with.
    auto* box = qobject_cast<QVBoxLayout*>(layout());
    box->insertLayout(box->count() - 1, form);

    if (auto* buttons = findChild<QDialogButtonBox*>()) {
        QPushButton* reset = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
        connect(reset, &QPushButton::clicked, this, [this]() {
            setValues(m_defaults.width, m_defaults.minWidth);
            showPreset(m_defaults);
        });
    }

    showPreset(current);
}

void StrokePresetDialog::showPreset(const StrokePreset& preset)
{
    if (QAbstractButton* choice = m_lineStyle->button(qBound(0, int(preset.lineStyle), 2)))
        choice->setChecked(true);
    m_rounded->setChecked(preset.rounded);
    m_arrowStart->setCurrentIndex(qBound(0, int(preset.arrowStart), 2));
    m_arrowEnd->setCurrentIndex(qBound(0, int(preset.arrowEnd), 2));
}

StrokePreset StrokePresetDialog::preset() const
{
    StrokePreset p;
    p.width = thickness();
    p.minWidth = minWidth();
    p.lineStyle = static_cast<quint8>(qMax(0, m_lineStyle->checkedId()));
    p.arrowStart = static_cast<quint8>(m_arrowStart->currentIndex());
    p.arrowEnd = static_cast<quint8>(m_arrowEnd->currentIndex());
    p.rounded = m_rounded->isChecked();
    return p;
}
