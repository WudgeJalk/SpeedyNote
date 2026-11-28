#pragma once

#include <QString>
#include <QWidget>

// The row of buttons at the end of a side panel (Pages, Layers, Library):
// one background, one line above it, one margin, so the three look alike.
namespace PanelFooter {

constexpr int Margin = 8;
constexpr int Spacing = 4;

inline void apply(QWidget* footer, bool dark)
{
    footer->setObjectName(QStringLiteral("panelFooter"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    footer->setStyleSheet(QStringLiteral(
        "#panelFooter { background: %1; border: none; border-top: 1px solid %2; }")
        .arg(dark ? "#1f1f1f" : "#f2f2f2", dark ? "#3a3a3a" : "#d4d4d4"));
}

} // namespace PanelFooter
