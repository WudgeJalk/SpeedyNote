#pragma once

// The paper list of the two settings dialogs. The caller adds the old None /
// Grid / Lines first (int data: a Page::BackgroundType, they use the spacing
// boxes); addPapers() adds every paper drawn by code (QString data: its id),
// one separator per group.

#include "../../core/Paper.h"

#include <QComboBox>

namespace PaperCombo {

inline void addPapers(QComboBox* combo, bool endlessOnly)
{
    int group = -1;
    for (const Paper::PaperDef& def : Paper::all()) {
        if (qstrcmp(def.id, "plain") == 0 || (endlessOnly && !def.endless)) continue;  // plain = None
        if (def.group != group) {
            combo->insertSeparator(combo->count());
            group = def.group;
        }
        combo->addItem(Paper::displayName(def), QString::fromLatin1(def.id));
        combo->setItemData(combo->count() - 1, Paper::groupName(def.group), Qt::ToolTipRole);
    }
}

/// The paper id of the current item, empty for None / Grid / Lines.
inline QString paperId(const QComboBox* combo)
{
    const QVariant data = combo->currentData();
    return data.userType() == QMetaType::QString ? data.toString() : QString();
}

/// Selects a paper by id, else the old type. False when neither is listed.
inline bool select(QComboBox* combo, const QString& paper, int type)
{
    int index = paper.isEmpty() ? -1 : combo->findData(paper);
    if (index < 0) index = combo->findData(type);
    if (index >= 0) combo->setCurrentIndex(index);
    return index >= 0;
}

} // namespace PaperCombo
