#include "ObjectSubToolbar.h"
#include "../widgets/IconPresetButton.h"

#include <QPalette>

ObjectSubToolbar::ObjectSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    auto add = [this](const char* icon, const QString& tip) {
        auto* button = new IconPresetButton(this);
        button->setIconName(QLatin1String(icon));
        button->setToolTip(tip);
        addWidget(button);
        return button;
    };
    m_selectButton = add("select", tr("Select: tap a picture to choose it, then drag it (Ctrl+7)"));
    m_addButton = add("image_plus", tr("Add: tap the page to put a picture there (Ctrl+6)"));
    addSeparator();
    m_pasteButton = add("paste", tr("Paste a picture or an object (Ctrl+V)"));

    connect(m_selectButton, &IconPresetButton::clicked, this, [this]() {
        setCreateMode(false);
        emit createModeRequested(false);
    });
    connect(m_addButton, &IconPresetButton::clicked, this, [this]() {
        setCreateMode(true);
        emit createModeRequested(true);
    });
    connect(m_pasteButton, &IconPresetButton::clicked, this, &ObjectSubToolbar::pasteRequested);

    setDarkMode(palette().color(QPalette::Window).lightness() < 128);
    setCreateMode(false);
}

void ObjectSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);
    for (IconPresetButton* button : {m_selectButton, m_addButton, m_pasteButton}) {
        button->setDarkMode(darkMode);
    }
}

void ObjectSubToolbar::setCreateMode(bool create)
{
    m_selectButton->setSelected(!create);
    m_addButton->setSelected(create);
}
