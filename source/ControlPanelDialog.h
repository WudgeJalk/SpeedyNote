#ifndef CONTROLPANELDIALOG_H
#define CONTROLPANELDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QColor>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTreeWidget>

#include "ui/dialogs/KeyCaptureDialog.h"

// Forward declarations
class MainWindow;
class QVBoxLayout;

/**
 * @brief Control Panel dialog for application settings.
 * 
 * Phase CP.1: Cleaned up after migration. Contains only working tabs:
 * - Theme: Custom accent color settings
 * - Language: Language selection
 * - Cache: Cache management (TODO: integrate with NotebookLibrary)
 * - About: Application info
 */
class ControlPanelDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Construct the Control Panel dialog.
     * @param mainWindow Reference to MainWindow for settings access.
     * @param parent Parent widget.
     */
    explicit ControlPanelDialog(MainWindow *mainWindow, QWidget *parent = nullptr);
    
    /**
     * @brief Switch to the Keyboard Shortcuts tab.
     * Used by keyboard shortcut Ctrl+Alt+Shift+K.
     */
    void switchToKeyboardShortcutsTab();

    /// Switch to the Stylus tab (pen buttons). Used by the pen window.
    void switchToStylusTab() { tabWidget->setCurrentWidget(stylusTab); }
    /// Switch to the Drawing tab. Used by the ruler's "Options...".
    void switchToDrawingTab() { tabWidget->setCurrentWidget(drawingTabPage); }

protected:
    /**
     * @brief Override done() to fix Android keyboard crash (BUG-A001).
     * Hides the virtual keyboard before dialog destruction.
     */
    void done(int result) override;

private slots:
    void applyChanges();
    void chooseAccentColor();
    void chooseBackgroundColor();
    void chooseGridColor();
    void loadSettings();

private:
    MainWindow *mainWindowRef;

    // === Dialog widgets ===
    QTabWidget *tabWidget;
    QPushButton *applyButton;
    QPushButton *okButton;
    QPushButton *cancelButton;

    // === Background tab ===
    QWidget *backgroundTab;
    QComboBox *styleCombo;
    QComboBox *pageSizeCombo;
    QLabel *pageSizeDimLabel;
    QPushButton *bgColorButton;
    QPushButton *gridColorButton;
    QSpinBox *gridSpacingSpin;
    QSpinBox *lineSpacingSpin;
    QColor selectedBgColor;
    QColor selectedGridColor;
    void createBackgroundTab();
    /// The Background tab's values as one string, to see if the user changed any.
    QString backgroundSignature() const;
    QString m_loadedBackground;  ///< backgroundSignature() when the dialog opened / last applied
    void onPageSizePresetChanged(int index);

    // === Tools tab ===
    QWidget *toolsTab;
    QDoubleSpinBox *wheelScrollSpeedSpin;
    QCheckBox *panOutsidePagesCheck = nullptr;
    // Page tab: show the New note window (Step N1, newNote/ask)
    QCheckBox *newNoteAskCheck = nullptr;
    // Page tab, "Saving" group (Step R1)
    QCheckBox *autosaveCheck = nullptr;
    QSpinBox *autosaveIdleSpin = nullptr;
    QSpinBox *autosaveMaxSpin = nullptr;
    // Same group: earlier versions (Step R4)
    QCheckBox *backupCheck = nullptr;
    QSpinBox *backupMinutesSpin = nullptr;
    QSpinBox *backupCountSpin = nullptr;
    QSpinBox *backupDaysSpin = nullptr;
    QCheckBox *ocrCjkGridModeCheck = nullptr;
    QComboBox *eraserUndoGroupingCombo = nullptr;
    void createToolsTab();

    // === Drawing tab (ControlPanelDrawingTab.cpp) ===
    QDoubleSpinBox *dashLengthSpin = nullptr;
    QDoubleSpinBox *dashGapSpin = nullptr;
    QDoubleSpinBox *dotGapSpin = nullptr;
    QCheckBox *eraserPenCheck = nullptr;
    QCheckBox *eraserMarkerCheck = nullptr;
    QCheckBox *eraserAutoDeselectCheck = nullptr;
    QSpinBox *stabilisationSpin = nullptr;
    QCheckBox *drawAndHoldCheck = nullptr;
    QSpinBox *holdMsSpin = nullptr;
    QSpinBox *holdStillSpin = nullptr;      // LengthSpinBox
    QSpinBox *holdMinLengthSpin = nullptr;  // LengthSpinBox
    QSpinBox *cornerAngleSpin = nullptr;
    QSpinBox *closePercentSpin = nullptr;
    QSpinBox *snapPxSpin = nullptr;         // LengthSpinBox
    QSpinBox *roughPercentSpin = nullptr;
    QSpinBox *equalPercentSpin = nullptr;
    QSpinBox *angleSnapSpin = nullptr;
    QSpinBox *trapezoidAngleSpin = nullptr;
    QSpinBox *levelAngleSpin = nullptr;
    QCheckBox *handArrowsCheck = nullptr;
    QComboBox *handArrowHeadCombo = nullptr;
    QSpinBox *laserHoldSpin = nullptr;
    QSpinBox *laserFadeSpin = nullptr;
    QPushButton *laserColorButton = nullptr;
    QColor laserColorChoice;
    QSpinBox *rulerSnapSpin = nullptr;      // LengthSpinBox
    QComboBox *rulerAngleUnitCombo = nullptr;
    QCheckBox *fingerTapCheck = nullptr;
    QSpinBox *tapMsSpin = nullptr;
    QSpinBox *tapSlopSpin = nullptr;        // LengthSpinBox
    QSpinBox *tapReachSpin = nullptr;       // LengthSpinBox
    QWidget *drawingTabPage = nullptr;
    void createDrawingTab();
    /// Save the Drawing tab, reload DrawingSettings and repaint if needed.
    void applyDrawingTab();
    /// "Pen Vibration" of the Stylus tab (PenHaptics); applies at once, like that tab.
    void addPenVibrationGroup(QVBoxLayout *layout, QWidget *parent);

    // === Theme tab ===
    QWidget *themeTab;
    QCheckBox *useCustomAccentCheckbox;
    QPushButton *accentColorButton;
    QColor selectedAccentColor;
    QCheckBox *pdfDarkModeCheckbox;
    QCheckBox *skipImageMaskingCheckbox;
    void createThemeTab();

    // === Language tab ===
    QWidget *languageTab;
    QComboBox *languageCombo;
    QCheckBox *useSystemLanguageCheckbox;
    QComboBox *ocrLanguageCombo = nullptr;
    void createLanguageTab();

    // === Cache tab ===
    QWidget *cacheTab;
    void createCacheTab();

    // === About tab ===
    QWidget *aboutTab;
    void createAboutTab();

    // === Keyboard Shortcuts tab (Phase 5.1) ===
    QWidget *shortcutsTab;
    QTreeWidget *shortcutsTree;
    QPushButton *resetAllShortcutsButton;
    QPushButton *openConfigFolderButton;
    void createShortcutsTab();
    void populateShortcutsTree();
    void onShortcutItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onEditShortcut();
    void onResetShortcut();
    void onResetAllShortcuts();
    void onOpenConfigFolder();
    void updateShortcutDisplay(QTreeWidgetItem* item, const QString& actionId);

    // === Stylus tab ===
    QWidget *stylusTab;
    void createStylusTab();
#ifdef Q_OS_LINUX
    // Palm rejection (Linux only - Windows has it built in)
    QCheckBox *palmRejectionCheckbox;
    QSpinBox *palmRejectionDelaySpinBox;
#endif
};

#endif // CONTROLPANELDIALOG_H
