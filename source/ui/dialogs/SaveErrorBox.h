#pragma once

// ============================================================================
// SaveErrorBox - the box shown when a note could not be saved (Step R2)
// ============================================================================
// A failed save never touches the last saved version on disk, and the note
// stays open with its changes, so every answer here is safe.
// ============================================================================

#include <QCoreApplication>
#include <QString>

class QWidget;

class SaveErrorBox
{
    Q_DECLARE_TR_FUNCTIONS(SaveErrorBox)

public:
    enum Choice { Retry, SaveCopy, Cancel, Discard };

    /**
     * @param name  The note's name, shown in quotes.
     * @param error The plain reason (Document/DocumentManager error text).
     * @param offerDiscard Adds "Close without saving" (when a note closes).
     * @return Retry (try the same place again), SaveCopy (choose another
     *         place), Cancel, or Discard (only with offerDiscard).
     */
    static Choice ask(QWidget* parent, const QString& name, const QString& error,
                      bool offerDiscard = false);
};
