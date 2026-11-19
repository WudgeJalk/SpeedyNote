# Next session: build the whole new toolbar

Written 2026-11-20, before a `/clear`. Read this file and `docs/ROADMAP.md` first.

## The order from the user

"If you create the brand new toolbar, you just need to go all in with all the toolbar modifications I asked for. Take your time, do a workflow of as many agents as you want."

- Build **all of Part T** of `docs/ROADMAP.md` (T1 to T6), in one batch.
- T5 and T6 need parts of Part B. Build those too: **B1** (laser pointer), **B2** (ruler), **B3** (stroke style saved in the file), **B4** (dotted and dashed lines), **B5** (pen types), **B6** (shapes, connectors, arrows).
- This is a very large batch. If it must be cut into stages, keep this order of value for the user: the floating two-level bar with colours and stroke presets; draw and hold with arrows; the eraser; the laser; fixed shapes; connectors that follow shapes; the ruler.
- "Make sure the pen has dotted and dashed lines as well." Line style is a choice of the pen itself, not only of the shape tool.
- Use a multi-agent **workflow**. Agents run on **Opus and Sonnet only**.
- The user tests **once, at the end**. Do not ask for a test after each step.

## State of the repo

- Branch `custom/pen-and-ui`, pushed to `origin` (`WudgeJalk/SpeedyNote`). `main` is untouched. `upstream` is the original author's repo: never push there.
- Everything up to the full screen fixes is committed. This file and the last plan changes are in the newest commit.

## How to build and test (Windows, this machine)

- Fast version: `.\compile.ps1 -norun` (about 2 minutes). Result: `build\speedynote.exe`.
- Test version: `.\compile.ps1 -debug -norun`. The tests only exist in this version.
- Run one test group and see its text:
  `$env:QT_FORCE_STDERR_LOGGING='1'; cmd /c "build\speedynote.exe --test-buttons 2>&1"`
- Groups: `penbindings`, `viewport-unit`, `page`, `document`, `notebooklibrary`, `linkobject`, `textboxobject`, `actionbars`, `buttons`, `pdfexporter`. All 10 passed on 2026-11-20.
- Both builds write to the same `build\` folder. **Leave the fast version there at the end**: it is the one the user runs.
- Never run two builds at the same time.

Traps:
- The tests read the user's **real settings** (registry `HKCU\Software\SpeedyNote\App`). A test can fail because of a setting the user changed. `testEraserModeButtons` did.
- `document` prints "Failed to open ... corrupt ... pdf". That is on purpose.
- `viewport-unit` has one test that uses the Windows clipboard. It failed once when the clipboard was busy. Run it again before blaming the code.
- The app is **single-instance**. If a copy is open, a new launch hands over to it and exits. Check `Get-Process speedynote` first. **Never kill a copy you did not start**: it can be the user's, with unsaved notes.

## How to see the UI without the user

Start the app, find the top-level windows of that process (`EnumWindows` + `GetWindowThreadProcessId`), and render each with `PrintWindow(hwnd, hdc, 2)`. Read the PNG. Use `--create-new <path>` to get the note window, not the start screen.

- **Never use `CopyFromScreen`.** It caught the user's private OneNote page once.
- Kill only the process ID you started.
- You cannot click. To toggle full screen, `PostMessage` an `F11` key to that window only.
- A hovering pen, drag and drop, and real clicks need the user.

## The four rules (from `docs/ROADMAP.md`)

1. It must stay fast on old devices. C++ and Qt Widgets. Nothing new on the per-pen-event path that is not cheap.
2. Old notebooks must still open. Each new saved field is optional, with a default.
3. The user decides. Each feature comes with its settings.
4. No feature needs a special pen.

Also: new texts in English only for now (translation later). Lengths are shown through `source/ui/widgets/LengthSpinBox.h` (mm or px). Icons are Lucide: add one with `resources/icons/lucide-icons.ps1`, and list it in `resources.qrc`.

## What the new toolbar must be

The model is GoodNotes. The user's screenshots and a 50-frame recording are described in Part T of the plan.

- **Floating bar.** Above the page, only as wide as its buttons. Drag it. It snaps to top, bottom, left or right, never the middle. Vertical on the left and right.
- **Two levels.** Main bar: tool families (lasso, pen, eraser, text, image, shapes, ruler later). Option bar: the options of the tool in use.
- **Pen option bar**: 3 line widths, line style (solid, dotted, dashed), then colours, then "add a colour".
- **Colours without a limit**: add, remove, change, reorder. Each tool keeps its list.
- **Pen window** (second tap on the pen): fountain / ball / brush with a drawn preview, a stroke stabilisation slider, a "draw and hold" switch, a link to the pen button settings.
- **One eraser button**, with choices in plain words and a small picture: "the whole line", "only what I touch", "all lines in a loop".
- **Shape bar**: free shape, two connector lines, rectangle, circle, triangle, diamond, rounded rectangle, line style, colour.
- **Four independent pen choices**: colour, width, pen type, line style. Changing one never changes another.

### How the GoodNotes free shape tool behaves

From the user's second recording (289 frames, 2026-11-20, 24 of them looked at). This is the behaviour to copy.

1. **Each stroke is cleaned when the pen lifts.** A wobbly freehand outline becomes clean: the parts that were nearly straight become straight lines, the parts that were curved stay smooth curves, and a corner appears where the pen changed direction.
2. **It closes only if it makes sense.** When the end is near the start, the result is a closed shape. A spiral whose end stays far from its start stays an **open** smooth curve. Nothing is forced into a rectangle or a circle.
3. **New strokes snap to shapes that are there.** A loop drawn next to the first shape became a clean teardrop whose two ends meet **on the edge of the first shape**, at one point. A third stroke became a smooth oval that joins the same shape and the same point. So the ends of a new stroke snap to a nearby edge or corner of an existing shape.
4. **The stroke keeps the pen's colour and width.** The colour comes from the last button of the shape bar.
5. **The eraser removes one whole shape at a time.** Two added shapes were erased and the first one stayed whole.
6. **The eraser option bar uses words.** It shows a drop-down that reads "Trait" (stroke), a settings icon, then three sizes. This confirms T4: name the eraser mode in words.

The fixed shapes of the bar (rectangle, circle, triangle, diamond, rounded rectangle) and the two connector lines were not used in the recording.

### More details, from four more recordings and six screenshots (2026-11-20)

The user's rule for all of it: GoodNotes is "not perfect", and "everything should be parameterable in the settings, which is not the case in GoodNotes". Each threshold below (hold time, snap distance, fade time, animation) is a setting.

**Pen bar** (recording and screenshots)
- Tool row: pen (with a small arrow that opens the pen window), pencil, highlighter, tape, shapes.
- Then **three stroke presets**. A preset is a width AND a line style: the recording shows "dashed, thin solid, thick solid". So the dotted and dashed choice lives in the preset.
- Then the colours: black, blue, red, green, purple, grey, and "add". The chosen colour has a small arrow to edit it.
- **The colour is on the pen**: the pen icon itself shows the chosen colour on its tip.
- The highlighter has its own three sizes and its own colours.
- There are short animations when the tool changes. Rule 1 applies: they must cost nothing while the pen writes, and there must be a switch to turn them off.

**Stroke preset window** ("Réglages du trait", shows the width as "0.30 mm" with a reset button)
- Solid / dashed / dotted.
- A width slider.
- "Rounded corners" switch.
- **Arrow heads**: for the start and for the end, one of: none, open arrow, filled arrow.

**Draw and hold** (pen recording)
- Draw a line and keep the pen still at the end: it becomes a straight line. Draw a curve and hold: it becomes a smooth curve.
- **GoodNotes cannot draw an arrow by hand.** The user drew a line and then a small triangle for the head: the head was turned into a broken shape. The user showed this as a failure.

**Arrows by hand** (OneNote recording)
- OneNote does it right in one way: a line plus a hand-drawn head becomes a clean arrow with a proper head.
- OneNote does it wrong in two ways: it is **not fluid** (the change comes late and jumps), and a curved line was flattened into a straight arrow.
- Target for SpeedyNote: recognise "a line, then a head at its end" and make a clean arrow, at once, and **keep the curve** of the line.

**Shape tool** (shape recording)
- Fixed shapes (rectangle, circle, triangle, diamond, rounded rectangle) are placed as **objects**: they show corner handles, side handles and a rotate handle. A small bar under a selected shape changes its type. A second bar offers order, cut, copy, delete and more.
- **Connectors.** Two kinds: with right angles (the corners are rounded) and curved. A rough L drawn by hand became a clean right-angle connector. A connector **attaches to the side of a shape**, and it is drawn again when the shape moves. A connector can also start from another connector.
- Design consequence: fixed shapes and connectors are objects (the app has an object system: `source/objects/InsertedObject.h`). The free shape and "draw and hold" produce cleaned strokes. These are two different models. Decide this early.

**Eraser** (two screenshots)
- The mode list has two entries with words and one line of help: "Standard eraser" and "Stroke eraser: erases whole strokes by touching them once". Then three sizes.
- Advanced settings: an **eraser filter** with one switch per tool (pen, pencil, highlighter, tape: "the eraser ignores what was added with the tools that are off"), an "auto deselect" switch, and "Clear the page".

**Laser pointer** (recording and screenshot)
- Two modes: **Point** (a glowing red dot that follows the pen) and **Line** (a glowing red trail).
- In line mode the strokes stay while the user keeps writing, then they **fade out together** a moment after the pen stops. Nothing is saved.

**Ruler** (screenshot)
- A wide ruler lies across the page, with ticks and numbers, and a rotate handle in its centre.
- Its menu: "Set the angle", "Set the position", "Hide", "Options".
- The user wants the ruler to **show its current angle on itself**, and to set the angle by number (degrees or radians, to 1 degree, from the earlier list).

**Not asked for by the user, seen in GoodNotes only**: pencil tool, tape tool, stickers, sticky note, audio. Do not build them unless the user asks.

## What exists today (do not rebuild, do not break)

- `source/ui/Toolbar.{h,cpp}`: one horizontal row in the main window layout. Tool options open **inside the row** (`ExpandableToolButton` + `PenSubToolbar`, `MarkerSubToolbar`, `EraserSubToolbar`, `HighlighterSubToolbar`, `OcrSubToolbar` in `source/ui/subtoolbars/`). A two-page paging system (`m_page1Widgets`, `m_page2Widgets`, `updatePagination`) handles a narrow window.
- `PenSubToolbar`: **3 fixed** colour presets and 3 width presets (`NUM_PRESETS = 3`), with per-tab state and `QSettings`.
- User settings that must keep working: button order and hidden buttons (`Toolbar::setButtonLayout`, editor in `ControlPanelDialog::createToolsTab`), toolbar position top / bottom / hidden (`MainWindow::applyToolbarPosition`), the "leave full screen" button at the end of the toolbar.
- `source/ui/QuickBar.{h,cpp}`: the bar at the pen tip. It reads the pen colours through `PenSubToolbar::presetColor / selectedColorPreset / pickColorPreset`. It must follow the new colour lists.
- `source/core/PenBindings.{h,cpp}`: pen buttons. Bindings name tools as `pen`, `marker`, `eraser`, `highlighter`, `lasso`, `object_select`, `pan`.
- `source/ui/ToolbarButtonTests.h` tests the paging and the eraser mode buttons. It reaches into private members of `Toolbar`. Update the tests with the code.
- Tools: `source/core/ToolType.h`. Eraser modes: `DocumentViewport::EraserMode` (`Normal`, `Partial`, `Lasso`, `LassoPartial`). Straight line mode exists (`setStraightLineMode`).
- A stroke: `source/strokes/VectorStroke.h` (points with pressure, colour, base thickness). No pen type and no line style yet. Where strokes are drawn and cached has **not** been read yet: find it first, because dashed lines and pen types change the drawing and the stroke cache.
- `source/core/DocumentViewport.cpp` has about 19,500 lines. Put new logic in new small files where possible.
- Window frame internals (title row, full screen): `MainWindow::nativeEvent`, `hideQtTitleBarDrawing`, `applyFullscreenChrome`. Not part of this task; do not disturb them.

## Suggested shape of the work

1. **Explore** (Sonnet, parallel): stroke drawing and caching; the toolbar and sub-toolbar code; how tools and eraser modes are switched; the file format of a stroke and its tests.
2. **Design** (Opus): the classes of the new bar, the stroke style fields and their file format, the shape recogniser. Check the design against the four rules.
3. **Build in this order**, one build at a time:
   - Stroke style in the file (B3), with a test that an old file still loads.
   - Dotted and dashed drawing (B4), measured for speed.
   - The floating two-level bar (T1, T2), then colours (T3), then the eraser (T4).
   - Pen types (B5) and the pen window (T5).
   - Shapes (B6) and the shape bar (T6).
4. **Review** (Opus, not the author of the code): regressions for users who change nothing, speed on the pen path, old files, the other platforms (Linux, macOS, Android, iOS, Qt5 on Windows must still compile; they are not built here).
5. **Verify**: all 10 test groups, a picture of the window, then one test list for the user.

## Still open with the user

- "Spaces on both sides": fixed as 8 lost pixels in full screen. The user has not said if they also meant the grey area beside the page.
- Where the library folder lives (Part C). Not needed for the toolbar.
