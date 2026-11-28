#ifndef TOOLBARBUTTONTESTS_H
#define TOOLBARBUTTONTESTS_H

#include <QObject>
#include <QCoreApplication>
#include <QResizeEvent>
#include <QTest>
#include <QSignalSpy>
#include <QButtonGroup>
#include "ToolbarButtons.h"
#include "Toolbar.h"
#include "widgets/ExpandableToolButton.h"
#include "widgets/IconPresetButton.h"
#include "subtoolbars/EraserSubToolbar.h"
#include "subtoolbars/HighlighterSubToolbar.h"
#include "subtoolbars/MarkerSubToolbar.h"
#include "subtoolbars/PenSubToolbar.h"
#include "widgets/ThicknessPresetButton.h"
#include "widgets/ColorListRow.h"
#include "NavigationBar.h"
#include <QBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QSettings>
#include <QToolButton>
#include <QWidgetAction>

/**
 * Unit tests for ToolbarButton classes.
 * Run with: speedynote --test-buttons
 */
class ToolbarButtonTests : public QObject {
    Q_OBJECT

private slots:
    // Test ActionButton
    void testActionButton() {
        ActionButton btn;
        
        // Should not be checkable
        QVERIFY(!btn.isCheckable());
        
        // Should have correct objectName for QSS
        QCOMPARE(btn.objectName(), QString("ActionButton"));
        
        // Should be 36x36
        QCOMPARE(btn.size(), QSize(36, 36));
        
        // Click should emit clicked signal
        QSignalSpy spy(&btn, &QPushButton::clicked);
        btn.click();
        QCOMPARE(spy.count(), 1);
    }
    
    // Test ToggleButton
    void testToggleButton() {
        ToggleButton btn;
        
        // Should be checkable
        QVERIFY(btn.isCheckable());
        
        // Should have correct objectName
        QCOMPARE(btn.objectName(), QString("ToggleButton"));
        
        // Should toggle on click
        QVERIFY(!btn.isChecked());
        btn.click();
        QVERIFY(btn.isChecked());
        btn.click();
        QVERIFY(!btn.isChecked());
    }
    
    // Test ThreeStateButton
    void testThreeStateButton() {
        ThreeStateButton btn;
        
        // Should have correct objectName
        QCOMPARE(btn.objectName(), QString("ThreeStateButton"));
        
        // Initial state should be 0
        QCOMPARE(btn.state(), 0);
        
        // Should cycle through states on click
        QSignalSpy spy(&btn, &ThreeStateButton::stateChanged);
        
        btn.click();
        QCOMPARE(btn.state(), 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
        
        btn.click();
        QCOMPARE(btn.state(), 2);
        
        btn.click();
        QCOMPARE(btn.state(), 0); // Wraps around
        
        // setState should clamp to valid range
        btn.setState(5);
        QCOMPARE(btn.state(), 2); // Clamped to max
        
        btn.setState(-1);
        QCOMPARE(btn.state(), 0); // Clamped to min
    }
    
    // Test ToolButton
    void testToolButton() {
        ToolButton btn;
        
        // Should be checkable (like ToggleButton)
        QVERIFY(btn.isCheckable());
        
        // Should have correct objectName
        QCOMPARE(btn.objectName(), QString("ToolButton"));
    }
    
    // Test ToolButton exclusive selection with QButtonGroup
    void testToolButtonGroup() {
        ToolButton btn1, btn2, btn3;
        QButtonGroup group;
        
        group.addButton(&btn1);
        group.addButton(&btn2);
        group.addButton(&btn3);
        group.setExclusive(true);
        
        // Initially none checked
        QVERIFY(!btn1.isChecked());
        QVERIFY(!btn2.isChecked());
        QVERIFY(!btn3.isChecked());
        
        // Click btn1 - only btn1 should be checked
        btn1.click();
        QVERIFY(btn1.isChecked());
        QVERIFY(!btn2.isChecked());
        QVERIFY(!btn3.isChecked());
        
        // Click btn2 - only btn2 should be checked
        btn2.click();
        QVERIFY(!btn1.isChecked());
        QVERIFY(btn2.isChecked());
        QVERIFY(!btn3.isChecked());
    }
    
    // Test icon loading
    void testIconLoading() {
        ActionButton btn;
        
        // Set a themed icon that exists in resources
        btn.setThemedIcon("save");
        
        // Icon should not be null
        QVERIFY(!btn.icon().isNull());
        
        // Test dark mode switching
        btn.setDarkMode(false);
        QVERIFY(!btn.isDarkMode());
        
        btn.setDarkMode(true);
        QVERIFY(btn.isDarkMode());
        QVERIFY(!btn.icon().isNull()); // Should still have icon
    }
    
    // Test ButtonStyles loading
    void testButtonStyles() {
        // Light mode stylesheet should not be empty
        QString lightStyle = ButtonStyles::getStylesheet(false);
        QVERIFY(!lightStyle.isEmpty());
        QVERIFY(lightStyle.contains("ActionButton"));
        QVERIFY(lightStyle.contains("ToggleButton"));
        
        // Dark mode stylesheet should not be empty
        QString darkStyle = ButtonStyles::getStylesheet(true);
        QVERIFY(!darkStyle.isEmpty());
        QVERIFY(darkStyle.contains("ActionButton"));
        
        // They should be different (different colors)
        QVERIFY(lightStyle != darkStyle);
    }

    // Overflow paging: the trailing button group moves to a second page when
    // the row no longer fits, and comes back when it does.
    void testToolbarPagination() {
        // Parented, as MainWindow hosts it. A parentless Toolbar would be a
        // window, and QLayout pins a window's minimum size to the full row
        // width, so it could never be resized narrow enough to page.
        QWidget host;
        Toolbar toolbar(&host);
        toolbar.setPlacement(Toolbar::Edge::Top, false);  // Docked, whatever the saved setting
        // An order saved before shapes, laser and ruler existed: they join the
        // tools on page 1, and undo, redo and touch stay together on page 2.
        const QStringList oldOrder = {
            "pen", "marker", "eraser", "straight_line", "lasso", "image", "link", "text",
            "highlighter", "ocr", "pan", "undo", "redo", "touch" };
        toolbar.applyButtonLayout(oldOrder, QStringList());
        QVERIFY(!toolbar.m_page1Widgets.isEmpty());
        QVERIFY(!toolbar.m_page2Widgets.isEmpty());
        for (QWidget* tool : {static_cast<QWidget*>(toolbar.m_shapeButton),
                              static_cast<QWidget*>(toolbar.m_laserButton),
                              static_cast<QWidget*>(toolbar.m_rulerButton)})
            QVERIFY(toolbar.m_page1Widgets.contains(tool));
        for (QWidget* action : {static_cast<QWidget*>(toolbar.m_panButton),
                                static_cast<QWidget*>(toolbar.m_undoButton),
                                static_cast<QWidget*>(toolbar.m_redoButton),
                                static_cast<QWidget*>(toolbar.m_touchGestureButton)})
            QVERIFY(toolbar.m_page2Widgets.contains(action));
        const QStringList full = toolbar.completeOrder(oldOrder);
        QCOMPARE(full.indexOf("shapes"), full.indexOf("straight_line") + 1);

        const int natural = toolbar.naturalLength();
        QVERIFY(natural > 0);

        // A widget that has never been shown is not created yet, so resize()
        // only assigns the geometry and never generates an event. Deliver one
        // so the real resizeEvent path still gets exercised.
        auto resizeTo = [&toolbar](int width) {
            const QSize before = toolbar.size();
            toolbar.resize(width, 44);
            QResizeEvent event(toolbar.size(), before);
            QCoreApplication::sendEvent(&toolbar, &event);
        };

        // The toolbar must not report the whole row as its floor, or the
        // window could never shrink far enough to page in the first place.
        QVERIFY(toolbar.minimumSizeHint().width() < natural);

        resizeTo(natural + 200);
        QVERIFY(!toolbar.m_paged);
        QVERIFY(!toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));

        resizeTo(natural / 2);
        QVERIFY(toolbar.m_paged);
        QCOMPARE(toolbar.m_currentPage, 0);
        QVERIFY(toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page1Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(!widget->isVisibleTo(&toolbar));

        // Forward on the right, back on the left.
        toolbar.m_pagerNextButton->click();
        QCOMPARE(toolbar.m_currentPage, 1);
        QVERIFY(toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page1Widgets)
            QVERIFY(!widget->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));

        // Pan lives on page 2, so selecting it from page 1 has to follow it
        // over rather than check a button nobody can see.
        toolbar.m_pagerBackButton->click();
        QCOMPARE(toolbar.m_currentPage, 0);
        toolbar.setCurrentTool(ToolType::Pan);
        QCOMPARE(toolbar.m_currentPage, 1);
        QVERIFY(toolbar.m_panButton->isVisibleTo(&toolbar));

        // Widening restores the single row and drops both pagers.
        resizeTo(natural + 200);
        QVERIFY(!toolbar.m_paged);
        QCOMPARE(toolbar.m_currentPage, 0);
        QVERIFY(!toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page1Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));
    }

    // The option bar shows the options of the tool in use, and only those; a
    // tool with no options hides it. In both looks.
    void testOptionBarFollowsTool() {
        for (bool floating : {false, true}) {
            QWidget host;
            host.resize(1200, 800);
            Toolbar toolbar(&host);
            toolbar.setPlacement(Toolbar::Edge::Top, floating);
            if (floating) toolbar.placeFloating(host.rect());
            else toolbar.resize(host.width(), 44);

            auto shows = [&toolbar](SubToolbar* expected) {
                if (!toolbar.m_optionBar->isVisibleTo(&toolbar)) return expected == nullptr;
                const SubToolbar* const all[] = {
                    toolbar.m_penSubToolbar, toolbar.m_markerSubToolbar,
                    toolbar.m_eraserSubToolbar, toolbar.m_highlighterSubToolbar };
                for (const SubToolbar* sub : all) {
                    if (sub->isVisibleTo(toolbar.m_optionBar) != (sub == expected)) return false;
                }
                return expected != nullptr;
            };

            QVERIFY(shows(toolbar.m_penSubToolbar));   // The pen is the first tool
            toolbar.setCurrentTool(ToolType::Eraser);
            QVERIFY(shows(toolbar.m_eraserSubToolbar));
            QCOMPARE(toolbar.optionsForTool(ToolType::Eraser),
                     static_cast<SubToolbar*>(toolbar.m_eraserSubToolbar));
            toolbar.setObjectInsertMode(DocumentViewport::ObjectInsertMode::Image);
            toolbar.setCurrentTool(ToolType::ObjectSelect);
            QVERIFY(shows(nullptr));
            QVERIFY(toolbar.m_objectImageButton->isChecked());
            toolbar.setCurrentTool(ToolType::Marker);
            QVERIFY(shows(toolbar.m_markerSubToolbar));
            toolbar.setCurrentTool(ToolType::Pen);
            QVERIFY(shows(toolbar.m_penSubToolbar));
            QVERIFY(toolbar.buttonForTool(ToolType::Pen)->isChecked());

            // A second tap on the tool in use hides its options, a third shows
            // them again; hidden, they stay hidden when the tool changes.
            QVector<ToolType> asked;
            connect(&toolbar, &Toolbar::toolWindowRequested,
                    [&asked](ToolType tool) { asked << tool; });
            toolbar.setTapHidesOptions(true);   // Applies only: the setting is not written
            toolbar.m_eraserButton->click();
            QVERIFY(shows(toolbar.m_eraserSubToolbar));
            toolbar.m_eraserButton->click();
            QVERIFY(shows(nullptr));
            toolbar.m_markerButton->click();
            QVERIFY(shows(nullptr));
            toolbar.m_lassoButton->click();
            toolbar.m_lassoButton->click();   // No options: the tap changes nothing
            toolbar.m_markerButton->click();
            QVERIFY(shows(nullptr));
            toolbar.m_markerButton->click();
            QVERIFY(shows(toolbar.m_markerSubToolbar));
            QVERIFY(asked.isEmpty());
            // Turned off while hidden: the options come back, and the tap asks
            // for the tool's window instead.
            toolbar.m_markerButton->click();
            QVERIFY(shows(nullptr));
            toolbar.setTapHidesOptions(false);
            QVERIFY(shows(toolbar.m_markerSubToolbar));
            toolbar.m_eraserButton->click();
            QVERIFY(asked.isEmpty());
            toolbar.m_eraserButton->click();
            QVERIFY(asked == QVector<ToolType>{ ToolType::Eraser });
            QVERIFY(shows(toolbar.m_eraserSubToolbar));
        }
    }

    // Three pen icons (the default): each picks its pen type, a second tap opens
    // no window, and an order saved before them gets them next to the pen. One
    // icon: the pen window comes back, behind a button of the pen bar.
    void testThreePenIcons() {
        QWidget host;
        Toolbar toolbar(&host);
        toolbar.setThreePenIcons(true);   // Applies only: the setting is not written
        const QStringList full = toolbar.completeOrder({ "marker", "pen", "eraser" });
        QCOMPARE(full.indexOf("pen_fountain"), full.indexOf("pen") + 1);
        QCOMPARE(full.indexOf("pen_brush"), full.indexOf("pen") + 3);
        QVERIFY(!toolbar.m_page1Widgets.contains(toolbar.m_penButton));
        bool listed = false;
        for (const Toolbar::ButtonChoice& choice : toolbar.buttonChoices()) {
            QVERIFY(choice.id != QLatin1String("pen"));
            listed |= choice.id == QLatin1String("pen_brush");
        }
        QVERIFY(listed);
        QVERIFY(!toolbar.m_penSubToolbar->m_windowButton->isVisibleTo(toolbar.m_penSubToolbar));

        QVector<ToolType> asked;
        connect(&toolbar, &Toolbar::toolWindowRequested, [&asked](ToolType tool) { asked << tool; });
        toolbar.setTapHidesOptions(true);
        const quint8 before = toolbar.m_penSubToolbar->currentStyle().penType;
        toolbar.m_penSubToolbar->setPenType(StrokeStyle::Fountain);   // So Ball is a new pick
        toolbar.m_penTypeButtons[StrokeStyle::Ball]->click();
        QVERIFY(toolbar.m_optionWanted);
        toolbar.m_penTypeButtons[StrokeStyle::Ball]->click();   // Same type again: hides the pen bar
        QVERIFY(!toolbar.m_optionWanted);
        toolbar.m_penTypeButtons[StrokeStyle::Fountain]->click();   // Another type: no toggle
        QVERIFY(!toolbar.m_optionWanted);
        toolbar.m_penTypeButtons[StrokeStyle::Ball]->click();
        toolbar.m_penTypeButtons[StrokeStyle::Ball]->click();
        QVERIFY(toolbar.m_optionWanted);
        QVERIFY(asked.isEmpty());
        QCOMPARE(int(toolbar.m_penSubToolbar->currentStyle().penType), int(StrokeStyle::Ball));

        // The settings list shows the selected tool's icon in the theme's own
        // colour, not the inverted one of the bar's selected pill.
        QVERIFY(toolbar.m_penTypeButtons[StrokeStyle::Ball]->isChecked());
        for (const Toolbar::ButtonChoice& choice : toolbar.buttonChoices()) {
            if (choice.id != QLatin1String("pen_ball")) continue;
            const QIcon plain = toolbar.m_penTypeButtons[StrokeStyle::Ball]->themedIcon();
            QCOMPARE(choice.icon.pixmap(24, 24).toImage(), plain.pixmap(24, 24).toImage());
            QVERIFY(choice.icon.pixmap(24, 24).toImage()
                    != toolbar.m_penTypeButtons[StrokeStyle::Ball]->icon().pixmap(24, 24).toImage());
        }
        // A shortcut or pen button that picks "pen" gets the type used last.
        toolbar.setCurrentTool(ToolType::Eraser);
        toolbar.setCurrentTool(ToolType::Pen);
        QVERIFY(toolbar.m_penTypeButtons[StrokeStyle::Ball]->isChecked());

        toolbar.setThreePenIcons(false);
        QVERIFY(toolbar.m_penButton->isChecked());
        QVERIFY(!toolbar.m_page1Widgets.contains(toolbar.m_penTypeButtons[0]));
        // One pen icon: the pen bar has the pen window's button, and the second
        // tap on the pen hides the bar (or, that setting off, opens the window).
        QVERIFY(toolbar.m_penSubToolbar->m_windowButton->isVisibleTo(toolbar.m_penSubToolbar));
        QSignalSpy window(toolbar.m_penSubToolbar, &PenSubToolbar::windowRequested);
        emit toolbar.m_penSubToolbar->m_windowButton->clicked();
        QCOMPARE(window.count(), 1);
        toolbar.m_penButton->click();
        QVERIFY(asked.isEmpty());
        QVERIFY(!toolbar.m_optionWanted);
        toolbar.setTapHidesOptions(false);
        toolbar.m_penButton->click();
        QVERIFY(asked == QVector<ToolType>{ ToolType::Pen });
        toolbar.m_penSubToolbar->setPenType(before);   // setPenType saves: put the user's back
    }

    // Floating on the left: a vertical bar against the left edge, centred,
    // inside the area. On the top: a row against the top edge.
    void testFloatingPlacement() {
        QWidget host;
        const QRect area(0, 0, 1200, 800);
        host.resize(area.size());
        Toolbar toolbar(&host);
        toolbar.setPlacement(Toolbar::Edge::Left, true);
        const int margin = toolbar.m_floatMargin;   // "toolbar/edgeGap": the user's
        const QRect inner = area.adjusted(margin, margin, -margin, -margin);

        toolbar.placeFloating(area);
        QVERIFY(toolbar.m_rowLayout->direction() == QBoxLayout::TopToBottom);
        QCOMPARE(toolbar.x(), margin);
        QVERIFY(inner.contains(toolbar.geometry()));
        QVERIFY(toolbar.height() > toolbar.width());
        QVERIFY(qAbs(toolbar.geometry().center().y() - inner.center().y()) <= 1);
        QVERIFY(toolbar.m_grip->isVisibleTo(&toolbar));

        toolbar.setPlacement(Toolbar::Edge::Top, true);
        toolbar.placeFloating(area);
        QVERIFY(toolbar.m_rowLayout->direction() == QBoxLayout::LeftToRight);
        QCOMPARE(toolbar.y(), margin);
        QVERIFY(inner.contains(toolbar.geometry()));
        QVERIFY(toolbar.width() > toolbar.height());
        QVERIFY(toolbar.width() < area.width() / 2 + 300);  // Only as long as its buttons
        // The same gap on the other two edges.
        toolbar.setPlacement(Toolbar::Edge::Bottom, true);
        toolbar.placeFloating(area);
        QCOMPARE(toolbar.geometry().bottom(), area.bottom() - margin);
        toolbar.setPlacement(Toolbar::Edge::Right, true);
        toolbar.placeFloating(area);
        QCOMPARE(toolbar.geometry().right(), area.right() - margin);
        toolbar.setPlacement(Toolbar::Edge::Top, true);

        // Too little room along the row: it pages, and stays inside the area.
        const QRect narrow(0, 0, 460, 800);
        toolbar.placeFloating(narrow);
        QVERIFY(toolbar.m_paged);
        QVERIFY(toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        QVERIFY(toolbar.width() <= narrow.width() - 2 * margin);
        toolbar.setPlacement(Toolbar::Edge::Right, true);
        const QRect low(0, 0, 1200, 500);
        toolbar.placeFloating(low);
        QVERIFY(toolbar.m_paged);
        QVERIFY(toolbar.height() <= low.height() - 2 * margin);
        QCOMPARE(toolbar.geometry().right(), low.right() - margin);
        toolbar.placeFloating(area);
        QVERIFY(!toolbar.m_paged);

        // The option bar does not page: in a narrow bar its colours give up
        // swatches (they scroll) so it stays inside, and get them back after.
        toolbar.setPlacement(Toolbar::Edge::Top, true);
        toolbar.setCurrentTool(ToolType::Pen);
        toolbar.placeFloating(area);
        auto* colors = toolbar.m_penSubToolbar->findChild<ColorListRow*>();
        QVERIFY(colors);
        const int fullColors = colors->sizeHint().width();
        const QRect phone(0, 0, 240, 800);
        toolbar.placeFloating(phone);
        if (colors->colors().size() > 1)   // The user's list (the tests read the real settings)
            QVERIFY(colors->sizeHint().width() < fullColors);
        QVERIFY(toolbar.m_optionBar->sizeHint().width() <= phone.width() - 2 * margin);
        toolbar.placeFloating(area);
        QCOMPARE(colors->sizeHint().width(), fullColors);

        // Docked again: a full-width row of fixed height, no grip.
        toolbar.setPlacement(Toolbar::Edge::Left, false);
        QVERIFY(toolbar.edge() == Toolbar::Edge::Top);  // Docked has no left or right
        QCOMPARE(toolbar.height(), 44);
        QVERIFY(!toolbar.m_grip->isVisibleTo(&toolbar));
    }

    // A stroke preset is a short line in its style; a size (marker, eraser)
    // stays a dot.
    void testPresetLineSample() {
        ThicknessPresetButton button;
        button.setLineColor(QColor(255, 0, 0));
        button.setThickness(5.0);
        const auto redAt = [&button](int x) {
            const QImage image = button.grab().toImage();
            const qreal dpr = image.devicePixelRatio();
            const QColor c = image.pixelColor(int(x * dpr), image.height() / 2);
            return c.red() > 150 && c.green() < 120;
        };
        QVERIFY(redAt(12));     // The dot
        QVERIFY(!redAt(6));
        button.setLineStyle(0); // Solid: a line from end to end
        QVERIFY(redAt(6));
        QVERIFY(redAt(12));
        QVERIFY(redAt(18));
    }

    // The title row: a point on one of our buttons is ours (not "title bar"
    // for Windows); the window buttons and empty space are not.
    void testTitleRowButtons() {
        NavigationBar bar;
        bar.setTitleBarMode(QIcon(), 0);
        bar.resize(1200, 44);
        bar.layout()->activate();
        const QPoint menu = bar.mapFromGlobal(bar.menuAnchor()) - QPoint(6, 6);
        QVERIFY(bar.hasOwnButtonAt(menu));
        QVERIFY(bar.hasOwnButtonAt(QPoint(bar.width() - 46 * 3 + 10, 22)));   // Minimise
        QVERIFY(bar.hasOwnButtonAt(QPoint(bar.width() - 46 - 1, 22)));        // Side of maximise
        QVERIFY(bar.hasOwnButtonAt(QPoint(bar.width() - 10, 22)));            // Close
        QVERIFY(!bar.hasOwnButtonAt(QPoint(bar.width() / 4, 22)));            // Between buttons
        QVERIFY(!bar.hasOwnButtonAt(QPoint(5, 60)));                          // Below the bar
    }

    // A dropped bar snaps to the nearest edge, never the middle.
    void testNearestEdge() {
        const QSize area(1000, 700);
        using Edge = Toolbar::Edge;
        QVERIFY(Toolbar::nearestEdge(QPoint(40, 350), area) == Edge::Left);
        QVERIFY(Toolbar::nearestEdge(QPoint(960, 300), area) == Edge::Right);
        QVERIFY(Toolbar::nearestEdge(QPoint(500, 30), area) == Edge::Top);
        QVERIFY(Toolbar::nearestEdge(QPoint(400, 680), area) == Edge::Bottom);
        QVERIFY(Toolbar::nearestEdge(QPoint(200, 600), area) == Edge::Bottom);
        QVERIFY(Toolbar::edgeFromName(Toolbar::edgeName(Edge::Right)) == Edge::Right);
        QVERIFY(Toolbar::edgeFromName(QStringLiteral("hidden")) == Edge::Top);
    }

    // A colour list: migrated from the old three keys, then add, move, remove
    // and change, with the pick following its colour. Saved only on an edit.
    void testColorListRow() {
        const QString group = QStringLiteral("colorListRowTest");
        auto clear = [&group]() { QSettings().remove(group); };
        clear();
        QSettings().setValue(group + QStringLiteral("/color1"), QColor(Qt::green));

        const QList<QColor> defaults = {Qt::red, Qt::blue, Qt::black, Qt::gray};
        QList<QColor> picked;
        {
            ColorListRow row(group, defaults);
            connect(&row, &ColorListRow::colorSelected, [&picked](const QColor& c) { picked << c; });
            QCOMPARE(row.colors(), QList<QColor>({Qt::green, Qt::blue, Qt::black, Qt::gray}));
            QVERIFY(!QSettings().contains(group + QStringLiteral("/colors")));  // Nothing written yet

            row.addColor(Qt::yellow);
            QCOMPARE(row.colors().size(), 5);
            QCOMPARE(row.selectedIndex(), 4);
            QCOMPARE(picked, QList<QColor>{QColor(Qt::yellow)});

            row.moveColor(4, 3);  // The picked colour moves and stays picked
            QCOMPARE(row.selectedIndex(), 3);
            QCOMPARE(row.currentColor(), QColor(Qt::yellow));

            row.setSelectedIndex(1, false);  // Blue
            row.moveColor(0, 1);             // Green moves past it
            QCOMPARE(row.currentColor(), QColor(Qt::blue));
            QCOMPARE(row.selectedIndex(), 0);

            picked.clear();
            row.removeColor(0);  // The picked one: the next takes its place
            QCOMPARE(row.colors().size(), 4);
            QCOMPARE(picked, QList<QColor>{QColor(Qt::green)});

            row.setColorAt(1, Qt::magenta);
            while (row.colors().size() > 1) row.removeColor(0);
            row.removeColor(0);  // Never the last one
            QCOMPARE(row.colors().size(), 1);
            row.addColor(Qt::cyan);
        }
        ColorListRow again(group, defaults);  // Read back what was saved
        QCOMPARE(again.colors().size(), 2);
        QCOMPARE(again.colors().last(), QColor(Qt::cyan));
        clear();
    }

    // Colour, width, pen type and line style are four independent choices; a
    // preset carries a width AND a line style.
    void testPenPresetsAreIndependent() {
        QMap<QString, QVariant> saved;  // The bar writes the real settings: put them back
        {
            QSettings settings;
            for (const QString& key : settings.allKeys())
                if (key.startsWith(QStringLiteral("pen/"))) saved.insert(key, settings.value(key));
        }

        {
            PenSubToolbar bar;
            int styleSignals = 0;
            StrokeStyle::Style last;
            connect(&bar, &PenSubToolbar::penStyleChanged, [&](const StrokeStyle::Style& s) {
                ++styleSignals;
                last = s;
            });

            StrokePreset dashed = bar.m_presets[1];
            dashed.lineStyle = StrokeStyle::Dashed;
            dashed.arrowEnd = StrokeStyle::Filled;
            bar.applyPreset(1, dashed);
            bar.onPresetClicked(1);
            bar.pickColorPreset(1);
            const QColor color = bar.currentColor();
            QCOMPARE(color, bar.colors().at(1));
            bar.setPenType(StrokeStyle::Ball);

            QCOMPARE(int(last.penType), int(StrokeStyle::Ball));
            QCOMPARE(int(last.lineStyle), int(StrokeStyle::Dashed));
            QCOMPARE(int(last.arrowEnd), int(StrokeStyle::Filled));
            QCOMPARE(bar.currentThickness(), dashed.width);
            QCOMPARE(bar.currentColor(), color);

            bar.onPresetClicked(0);  // Another preset: solid again, same pen and colour
            QCOMPARE(int(bar.currentStyle().lineStyle), int(StrokeStyle::Solid));
            QCOMPARE(int(bar.currentStyle().penType), int(StrokeStyle::Ball));
            QCOMPARE(bar.currentColor(), color);

            bar.cycleColor();  // Another colour: the style stays
            QVERIFY(bar.currentColor() != color);
            QCOMPARE(int(bar.currentStyle().penType), int(StrokeStyle::Ball));
            QVERIFY(styleSignals >= 2);
        }

        QSettings settings;
        settings.remove(QStringLiteral("pen"));
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
            settings.setValue(it.key(), it.value());
    }

    // Multi-pen: two named pens of one pen type keep their own sizes; the loop
    // button goes round them; another pen type keeps the usual sizes.
    void testMultiPen() {
        QMap<QString, QVariant> saved;  // The bar writes the real settings: put them back
        QSettings settings;
        for (const QString& key : settings.allKeys())
            if (key.startsWith(QStringLiteral("pen/"))) saved.insert(key, settings.value(key));
        settings.remove(QStringLiteral("pen"));
        {
            settings.setValue(QStringLiteral("pen/penType"), int(StrokeStyle::Ball));
            PenSubToolbar bar;
            QVERIFY(bar.m_multiPenButton->isHidden());   // off: as before
            const qreal usual = bar.currentThickness();

            settings.setValue(QStringLiteral("pen/multiPen"), true);
            const QString a4 = PenSubToolbar::addMultiPen(StrokeStyle::Ball, QStringLiteral("A4"));
            const QString a5 = PenSubToolbar::addMultiPen(StrokeStyle::Ball, QStringLiteral("A5"));
            QCOMPARE(PenSubToolbar::multiPenIds(StrokeStyle::Ball), QStringList({a4, a5}));
            bar.refreshFromSettings();
            QVERIFY(!bar.m_multiPenButton->isHidden());
            QCOMPARE(bar.m_multiPenId, a4);
            QCOMPARE(bar.currentThickness(), usual);     // a new pen starts from the usual sizes

            StrokePreset wide = bar.m_presets[bar.m_selectedPreset];
            wide.width = usual + 3.0;
            bar.applyPreset(bar.m_selectedPreset, wide);  // the size of A4 only
            qreal emitted = 0;
            connect(&bar, &PenSubToolbar::penThicknessChanged, [&](qreal w) { emitted = w; });
            bar.cycleMultiPen();
            QCOMPARE(bar.m_multiPenId, a5);
            QCOMPARE(emitted, usual);
            QVERIFY(bar.m_multiPenLabel && !bar.m_multiPenLabel->isHidden());
            QCOMPARE(bar.m_multiPenLabel->text(), QStringLiteral("A5"));
            bar.cycleMultiPen();                          // wraps
            QCOMPARE(bar.m_multiPenId, a4);
            QCOMPARE(emitted, usual + 3.0);

            bar.setPenType(StrokeStyle::Fountain);        // no pens of this type
            QVERIFY(bar.m_multiPenButton->isHidden());
            QCOMPARE(bar.currentThickness(), usual);
            bar.setPenType(StrokeStyle::Ball);
            QCOMPARE(bar.currentThickness(), usual + 3.0);

            settings.remove(PenSubToolbar::multiPenKey(StrokeStyle::Ball, a4));   // deleted while in use
            bar.refreshFromSettings();
            QCOMPARE(bar.m_multiPenId, a5);
            QCOMPARE(bar.currentThickness(), usual);
        }
        settings.remove(QStringLiteral("pen"));
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it)
            settings.setValue(it.key(), it.value());
    }

    // Size presets belong to a mode: switching modes must swap the three
    // values (and the selection), and lasso - which has no size - must hide the
    // row instead of showing dead controls.
    void testEraserSizePresetsArePerMode() {
        // Start from the built-in defaults; the developer's presets come back after.
        const SettingsGroupGuard guard(QStringLiteral("eraser"));

        EraserSubToolbar bar;
        const QList<ThicknessPresetButton*> sizes =
            bar.findChildren<ThicknessPresetButton*>();
        QCOMPARE(sizes.size(), 3);

        auto thicknesses = [&]() {
            QList<qreal> values;
            for (ThicknessPresetButton* button : sizes) {
                values << button->thickness();
            }
            return values;
        };

        // Whole-stroke is the default mode and shows its own three sizes.
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(thicknesses(), QList<qreal>({5.0, 15.0, 40.0}));
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(!button->isHidden());
        }

        // Pick the third whole-stroke preset, so the mode has a distinctive
        // selection.
        QTest::mouseClick(sizes.at(2), Qt::LeftButton);
        QCOMPARE(bar.currentSize(), 40.0);

        // Switching to partial brings its own values, not the shared ones.
        const QList<QAction*> modes = modeList(bar);
        QCOMPARE(modes.size(), 4);
        modes.at(1)->trigger();
        QCOMPARE(bar.currentModeIndex(), 1);
        QCOMPARE(thicknesses(), QList<qreal>({8.0, 15.0, 25.0}));
        QCOMPARE(bar.currentSize(), 15.0);   // partial starts on its middle preset

        // Remember the partial selection, then bounce back: whole-stroke must
        // still have its own values and its third preset selected.
        QTest::mouseClick(sizes.at(0), Qt::LeftButton);
        QCOMPARE(bar.currentSize(), 8.0);
        modes.at(0)->trigger();
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(thicknesses(), QList<qreal>({5.0, 15.0, 40.0}));
        QCOMPARE(bar.currentSize(), 40.0);

        // And back to partial: its first preset is still selected.
        modes.at(1)->trigger();
        QCOMPARE(bar.currentSize(), 8.0);

        // Lasso has no size, so the whole row disappears.
        modes.at(2)->trigger();
        QCOMPARE(bar.currentModeIndex(), 2);
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(button->isHidden());
        }

        // Cycling the size does nothing while lasso is active.
        const int beforeCycle = bar.currentModeIndex();
        bar.cycleSize();
        QCOMPARE(bar.currentModeIndex(), beforeCycle);

        // The partial lasso also works on a region, so it has no size either.
        modes.at(3)->trigger();
        QCOMPARE(bar.currentModeIndex(), 3);
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(button->isHidden());
        }

        // Returning to a disc-based mode brings the row back with that mode's
        // values intact.
        modes.at(1)->trigger();
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(!button->isHidden());
        }
        QCOMPARE(thicknesses(), QList<qreal>({8.0, 15.0, 25.0}));
        QCOMPARE(bar.currentSize(), 8.0);
    }

    // One mode button names the mode in words. Its list holds the four modes,
    // each a picture, words and a line of help, with exactly one checked.
    void testEraserModeButtons() {
        // The saved mode must not leak in: start from the defaults.
        const SettingsGroupGuard guard(QStringLiteral("eraser"));
        EraserSubToolbar bar;

        QCOMPARE(bar.findChildren<QToolButton*>().size(), 1);
        QToolButton* modeButton = bar.findChild<QToolButton*>();
        const QList<QAction*> modes = modeList(bar);
        // whole / partial / lasso / lasso-partial
        QCOMPARE(modes.size(), 4);

        auto checkedCount = [&]() {
            int count = 0;
            for (QAction* action : modes) {
                count += action->isChecked() ? 1 : 0;
            }
            return count;
        };

        // Whole-stroke is the default, and the button says so in words.
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(checkedCount(), 1);
        QVERIFY(modes.at(0)->isChecked());
        QCOMPARE(modeButton->text(), QStringLiteral("The whole line"));

        // Picking a mode checks it, unchecks the rest, renames the button and
        // reports the change.
        for (int mode : {1, 2, 3, 0}) {
            QSignalSpy spy(&bar, &EraserSubToolbar::eraserModeChanged);
            modes.at(mode)->trigger();
            QCOMPARE(bar.currentModeIndex(), mode);
            QCOMPARE(spy.count(), 1);
            QCOMPARE(spy.first().first().toInt(), mode);
            QCOMPARE(checkedCount(), 1);
            QVERIFY(modes.at(mode)->isChecked());
            QCOMPARE(modeButton->text(), modes.at(mode)->text());
        }

        // Picking the active mode again is a no-op, not a toggle-off.
        QSignalSpy repeat(&bar, &EraserSubToolbar::eraserModeChanged);
        modes.at(0)->trigger();
        QCOMPARE(repeat.count(), 0);
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(checkedCount(), 1);

        // A tap on an entry of the list picks it (the entry is a widget).
        QWidget* row = static_cast<QWidgetAction*>(modes.at(1))->defaultWidget();
        QVERIFY(row);
        QTest::mouseClick(row, Qt::LeftButton);
        QCOMPARE(bar.currentModeIndex(), 1);

        // External state changes (e.g. shortcut) move the check with them.
        bar.setModeState(2);
        QCOMPARE(bar.currentModeIndex(), 2);
        QCOMPARE(checkedCount(), 1);
        QVERIFY(modes.at(2)->isChecked());

        // A vertical bar has room for the picture only.
        bar.setOrientation(Qt::Vertical);
        QCOMPARE(modeButton->toolButtonStyle(), Qt::ToolButtonIconOnly);
        bar.setOrientation(Qt::Horizontal);
        QCOMPARE(modeButton->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);

        // The options button asks for the eraser window.
        IconPresetButton* optionsButton = bar.findChild<IconPresetButton*>();
        QVERIFY(optionsButton);
        QSignalSpy options(&bar, &EraserSubToolbar::optionsRequested);
        QTest::mouseClick(optionsButton, Qt::LeftButton);
        QCOMPARE(options.count(), 1);

        // Each mode has its own picture.
        for (int a = 0; a < 4; ++a) {
            for (int b = a + 1; b < 4; ++b) {
                QVERIFY(eraserModePicture(a, false).toImage()
                        != eraserModePicture(b, false).toImage());
            }
        }
    }

private:
    /// The list behind the eraser's mode button, in EraserMode order.
    static QList<QAction*> modeList(EraserSubToolbar& bar) {
        QToolButton* button = bar.findChild<QToolButton*>();
        return (button && button->menu()) ? button->menu()->actions() : QList<QAction*>();
    }

    /// The bars read the real QSettings. This empties one group for a test
    /// and puts the developer's values back afterwards, even if the test fails.
    struct SettingsGroupGuard {
        QString group;
        QMap<QString, QVariant> saved;
        explicit SettingsGroupGuard(const QString& name) : group(name) {
            QSettings settings;
            for (const QString& key : settings.allKeys()) {
                if (key.startsWith(group + QLatin1Char('/'))) {
                    saved.insert(key, settings.value(key));
                }
            }
            settings.remove(group);
        }
        ~SettingsGroupGuard() {
            QSettings settings;
            settings.remove(group);
            for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
                settings.setValue(it.key(), it.value());
            }
        }
    };
};

/**
 * Run button tests.
 * @return 0 if all tests pass, non-zero otherwise
 */
inline int runButtonTests() {
    ToolbarButtonTests tests;
    return QTest::qExec(&tests);
}

#endif // TOOLBARBUTTONTESTS_H

