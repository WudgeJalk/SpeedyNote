# Roadmap

The list of ideas for this version of SpeedyNote, and the order to do them.
Written on 2026-11-19. Update the "Status" column when a step is done.

## The vision

A note app for students, where each student shapes the app to their own way of working.

- **Your notes are yours.** No subscription. No account. Notebooks are normal folders on your disk.
- **Freaking fast.** No web-app feeling. It must feel instant on an old tablet.
- **The hand stays on the page.** To change colour, tool or line type, you do not reach for the top of the screen.
- **Each student is different.** Layout, pen buttons and notebook structure are choices, not fixed.
- **Settings you can understand.** Each setting has a clear name and one line that says what it does. No hunt through menu 1, menu 2, menu 3.
- **Easy in, easy out.** To import a PDF or to change old notes is never a struggle.

What is wrong with the others (the user's words, 2026-11-19):
- OneNote: good notebooks and sections, but it was the only choice, and the pen workflow is slow.
- GoodNotes on Windows: feels good, but subscription, you own nothing, laggy, feels like a web app.
- All of them: settings are too few, or impossible to understand.

## The four rules

1. **It must stay fast** on old computers and old tablets. We keep C++ and Qt Widgets.
2. **Old notebooks must still open.** Each new thing saved in a file is optional, with a default.
3. **The user decides.** Each new feature comes with its settings. Nothing is fixed if it can be a choice.
4. **No feature needs a special pen.** Each pen shortcut also has a way with a finger, a mouse or a key. Not every tablet has a side button.

## What the app can already do

Checked in the code on 2026-11-19.

| Idea | Already there? | Where |
|---|---|---|
| Eraser that deletes the whole line | Yes (`Normal` mode). The user tested it: it works. | `source/core/DocumentViewport.h:415` |
| Eraser for a part, eraser with a lasso | Yes (`Partial`, `Lasso`, `LassoPartial`). | same |
| Straight lines | Yes, with a toolbar button. | `source/core/DocumentViewport.h:770` |
| Rotate and resize a selection | Yes, for lasso selections. | `LassoSelection`, `source/core/DocumentViewport.h:3166` |
| Lined pages and squared pages | Yes (`Lines`, `Grid`). | `source/core/Page.h:54` |
| Back-end eraser of the pen | Yes. | `source/core/DocumentViewport.cpp:5978` |
| List of app commands that a button can call | Yes, about 90, made for the keyboard. | `source/core/ShortcutManager.cpp:112` |
| Endless paper | Yes. "Edgeless canvas" mode. The paper is cut in tiles and only the tiles on screen are used, so it is built not to lag. To test with a big canvas. | `TileCoord`, `renderEdgelessMode()` in `source/core/DocumentViewport.h` |
| Paste images | Yes. | `source/objects/ImageObject.h` |
| Links | Yes. Link objects with 3 slots. To check: web links. | `source/objects/LinkObject.h` |
| Typed text boxes | Yes. | `source/objects/TextBoxObject.h` |
| Objects you move, resize and lock | Yes. Each object has a `locked` flag. | `source/objects/InsertedObject.h:41` |
| More than one PDF in a notebook | Partly. A document keeps a list of PDF sources. To check: can the user insert one between two pages? | `PdfSource`, `source/core/Document.h:117` |
| Notebooks are folders that you own | Yes. A notebook is a `.snb` folder on disk. | `source/core/NotebookLibrary.h` |
| Free to share | Yes. The license is GPL v3: you can share your version, and you must share its source code too. | `LICENSE` |

## What is missing

| Idea | Missing part |
|---|---|
| Pen side button and top button | The app reads the side button and then ignores it (`source/core/DocumentViewport.cpp:6010`). |
| Tools near the hand | The toolbar is fixed at the top (`source/MainWindow.cpp:1202`). The action bar is fixed on the left (`source/ui/actionbars/ActionBarContainer.h:20`). |
| Laser pointer | Nothing in the code. |
| Ruler | Nothing in the code. |
| Auto shapes | No shape recognition in the code. |
| Fountain pen, ball pen, brush pen | A stroke stores only points, colour and thickness (`source/strokes/VectorStroke.h:33`). |
| Dotted and dashed lines | Same cause. A stroke has no line style. |
| Notebook > section > sub-section, like OneNote | Only "starred" notebooks and one level of folders (`source/core/NotebookLibrary.h:28`). |
| Notebook covers, more page designs | Only `Grid` and `Lines`. No covers. |
| Clear settings | One dialog with tabs (`source/ControlPanelDialog.cpp`). Not searched, few descriptions. |
| Modern look | Styles are set in 55 code files (416 calls), plus 8 files in `resources/styles/`. |

## The steps

Size: S = about one sitting. M = a few sittings. L = many sittings.

### Part A - The hand stays on the page

| # | Step | Size | Status |
|---|---|---|---|
| 0 | Build tools, build, run the tests. | S | Done 2026-11-19. Build: `.\compile.ps1 -norun`. Tests need `.\compile.ps1 -debug -norun`, then `build\speedynote.exe --test-viewport-unit` (also `page`, `document`, `notebooklibrary`, `linkobject`, `textboxobject`, `actionbars`, `buttons`, `pdfexporter`). All 9 passed. |
| A1 | **Pen buttons.** Side button and back end. Each gesture (press, press twice, hold while writing, hold in the air) can call: any app command, a tool while held, or "switch tool then back". Settings table in the Stylus tab. | S | Code written 2026-11-19, tests pass (`--test-penbindings`). Tried by the user on the Surface: the lasso hold and the press-once toggle work. Fixed after that test: the toggle acted late, and the top bar was white on white with a light Windows accent colour. |
| A2 | **Quick bar at the pen tip.** Hold the side button (or long-press with a finger, or a key): a small bar opens where the pen is. It holds what you change often: favourite colours, pen / highlighter / eraser, straight line, line type. You choose what is in it. Release or tap to pick. | M | First version written 2026-11-19 (`source/ui/QuickBar.cpp`). Opens with a side button hold in the air, or the Q key. Holds 5 tools, straight line, and the 3 pen colours. **Not yet tried with a real pen.** Still to do: you choose its content, more colours, line width, open with a long finger press. |
| A3 | **Bar where you want.** Main toolbar: top, bottom, left, right, or hidden. Action bar: left or right. Hide the buttons you do not use. | M | Partly done 2026-11-19: you can hide toolbar buttons (Settings > Tools > Toolbar buttons) and turn the scroll bars off for good (Settings > Theme). Also done 2026-11-19: an icon editor for the toolbar (drag to reorder, double-tap to hide; Settings > Tools) and toolbar position top / bottom / hidden. **Next to do, asked again by the user on 2026-11-19 (GoodNotes is the model):** the toolbar floats above the page and takes only the room its buttons need. You drag it. When you drop it, it snaps to the nearest side: top, bottom, left or right, never the middle. On the left and right it is vertical. Hard parts: the toolbar is one horizontal row with a paging system and tool options that open in the row; all three must learn the vertical direction. Also: move the action bar. |
| A4 | **Top click and vibration** of the Slim Pen 2. Windows only. | M | Needs A1 |
| A5 | **Profiles.** Save a full setup (bars + pen buttons + quick bar) under a name. Example: "Lecture", "Exercise", "Reading". | S-M | Needs A1-A3 |

#### A1 device test (to do on the Surface with the Slim Pen 2)

Settings are in Settings (Ctrl+K) > Stylus > Pen Buttons. Code: `source/core/PenBindings.h`.

1. Draw with the tip. Erase with the back end. Both must work as before.
2. Hold the side button and draw a loop around some ink: it must be a lasso (the default). Move the selection. Tap outside: the pen tool must come back.
3. Set "press once" to "Switch to Eraser, and back the next time". Press the side button with the tip near the screen but not on it. The toolbar must show the eraser. Press again: back to the pen.
4. Set "press twice" to Undo. Draw a line, press twice in the air: the line must go.
5. Set "hold in the air" to any command. Hold the button 0.5 s with the tip near the screen.
6. Set "hold while you write" to Eraser. Hold and rub over ink.

If 2 and 6 work but 3, 4, 5 do nothing: Windows does not tell the app about the side button in the air on this device. Then the "in the air" gestures need another way (A4).

Known limits:
- The back end has one setting only (what it does on the screen). Press / press twice / hold for the top of the pen is the Bluetooth click, step A4.
- "Hold in the air" also fires if you hold the button for 0.4 s and then write.
- A command from the pen acts on the active pane. In split view, that can be a different pane from the one under the pen.
- Linux, macOS, Android, iPad and the Qt5 Windows build: the code is there, with no default on the side button. Not tested.

### Part T - The new toolbar (GoodNotes is the model)

The user's words, 2026-11-19: the GoodNotes toolbar "feels smooth, limitless and great"; OneNote "has been built by people who never take notes". Today's toolbar has only 3 colours, and options the user does not understand ("there's like three different erasers").

What the user likes in GoodNotes, from two screenshots:
- **Two levels.** A main bar with the tool families (lasso, pen, eraser, text, image, shapes, ruler). Under it, a small bar with the options of the tool in use.
- **Pen bar**: the pen, then 3 line widths, then a row of colours (many, not 3), then "add a colour".
- **Pen window** (tap the pen again): the pen type (fountain, ball, brush) with a drawn preview of the stroke, a "stroke stabilisation" slider, a "draw and hold" switch, and the pen gestures.
- **Shape bar**: free shape, two kinds of connector lines, rectangle, circle, triangle, diamond, rounded rectangle, then the line style and the colour.

| # | Step | Size | Status |
|---|---|---|---|
| T1 | **The floating bar.** It floats above the page and takes only the room it needs. Drag it; it snaps to the top, bottom, left or right, never the middle. Vertical on the left and right. (This is the rest of A3.) | M | |
| T2 | **Two levels.** Main bar = tool families. Option bar = the options of the tool in use. This replaces the options that open inside the row today. | M-L | Needs T1 |
| T3 | **Colours without a limit.** Any number of colours in the option bar. Add, remove, change, reorder. The row scrolls when it is long. Each tool keeps its own list. | M | Needs T2 |
| T4 | **One eraser, clear choices.** One eraser button. Its options use plain words and a small picture: "the whole line", "only what I touch", "all lines in a loop". | S | Needs T2. The 4 eraser modes exist today; this is naming and layout. |
| T5 | **Pen window.** Pen type with a live preview, stabilisation slider, "draw and hold" switch, link to the pen button settings. | M | Needs T2, B3, B5, B6 |
| T6 | **Shape bar.** The shapes and connectors above, line style, colour. | M-L | Needs T2, B6 |

**Decided by the user on 2026-11-20: go all in.** Build T1 to T6 in one batch, with the parts of Part B they need (B3, B4, B5, B6). "Make sure the pen has dotted and dashed lines as well": the line style is a choice of the pen, not only of the shapes. The brief for that work is `docs/NEXT_SESSION.md`.

What the user's second recording of the GoodNotes free shape tool shows (2026-11-20; full description in `docs/NEXT_SESSION.md`): each stroke is cleaned when the pen lifts (straight parts become straight, curves stay smooth, corners where the pen turned); it closes only when the end is near the start, otherwise it stays an open smooth curve; the ends of a new stroke snap onto the edge of a shape that is already there, so shapes join; the eraser removes one whole shape at a time.

### Part B - Tools for class (engineering first)

| # | Step | Size | Status |
|---|---|---|---|
| B1 | **Laser pointer.** A trail that fades, and is never saved. For when you explain to a friend. | S | |
| B2 | **Ruler.** A ruler on the page that you move and turn. The pen follows its edge. | M | |
| B3 | **Stroke style in the file.** Two optional fields in a stroke: pen type and line style. Opens B4 and B5. | S | |
| B4 | **Dotted and dashed lines.** | S | Needs B3 |
| B5 | **Pen types.** Ball pen, fountain pen, brush pen, with sizes. | M | Needs B3 |
| B6 | **Shapes, two ways.** (1) Hold the pen still at the end of a stroke: that one stroke becomes clean. (2) **Shape mode** (a switch): each stroke is cleaned when the pen leaves the screen. The stroke is cut where the pen stops or turns, each part becomes a straight line, and the shape closes if the end is near the start. It then becomes a line, triangle, rectangle, polygon, circle or ellipse. | M-L | |
| B7 | **Shape handles.** Rotate and resize a shape with 4 corner handles. Reuse the lasso handles. | S-M | Needs B6 |

**Pen rule for all of Part B: four independent choices.** Colour, width, pen type (pencil, ball pen, fountain pen, brush pen) and line type (solid, dotted, dashed). A change to one never changes the other three.

### Part C - Your library

| # | Step | Size | Status |
|---|---|---|---|
| C1 | **Notebook > section > sub-section > note.** See "The library model" below. A side panel with 3 columns, like OneNote: notebooks, sections, notes. | M-L | Plan written 2026-11-19, to be corrected by the user |
| C1b | **Export a whole notebook as one PDF.** All notes, in the order of the panel, from top to bottom. | M | Needs C1. The app already exports one note to PDF and has a batch export. |
| C2 | **Structure templates.** Make a new class with its sections in one action (Cours, Examen, Résumé, Exercices, Corrigés). | S | Needs C1 |
| C3 | **PDF between pages.** Drop a PDF on a section, or between two pages of a notebook. Example: an A4 PDF between two A4 note pages. | M | To check: what works today |
| C3b | **PDF on a page.** Drop a PDF on top of a page, like an image. You move it, resize it and write on it. | M | |
| C4 | **Change old notes easily.** Move, copy and reorder pages between notebooks. | M | To check: what works today |
| C5 | **Other files next to the notes.** Link or keep files in a section and open them from the app. | M | |
| C6 | **Page designs and notebook covers.** Dots, Cornell, planner. Covers so a notebook looks like a book. | M | |

#### The library model (plan for C1, 2026-11-19)

The user's example, from OneNote: notebook `HEIG26` > section `04_Math_TIN` > sub-sections `Cours`, `Examen`, `Résumé`, `Exercices`, `Corrigés` > notes. Two loose sections at the top: `Info-Générale`, `Brouillon`.

What the user wants: you work in ONE note of ONE section, and only that note is loaded. So it stays fast, however big the notebook is. At the end you can export the whole notebook as one PDF.

The plan: **the library is a folder tree on disk. Nothing new in the file format.**

| In the app | On disk |
|---|---|
| Notebook (`HEIG26`) | A normal folder |
| Section, sub-section (`04_Math_TIN`, `Cours`) | A sub-folder, as deep as the student wants |
| Note (`outil pour hachure`) | One `.snb` bundle, the format the app has today, with its own pages |

Why this way:
- Only the open `.snb` is loaded. This is the speed the user asked for, and it is already how the app works.
- The student owns plain folders. They can be copied, backed up or synced with any tool.
- Old notebooks still open (rule 2): an old `.snb` is just a note that is not in a section yet.

Decided with the user on 2026-11-19:
- **Free names.** The app never forces a name or a number on a section.
- **Free order by drag and drop** in the panel. The order is saved in a small file in each folder. The PDF of a whole notebook follows that order: the top section first, the bottom section last.
- **A colour for each section.** Saved in the same small file.
- **Cloud without an integration.** The library is a normal folder, so the student puts it inside OneDrive, iCloud Drive, Dropbox or any other synced folder. Many small files sync well: only the note that changed is sent.

Proposed, waiting for the user's yes: **many small files to work, one file to carry.**
- Work: the folder tree above. Fast, and safe (a damaged file costs one note, not the notebook).
- Carry: "Pack notebook" makes ONE file of a whole notebook (all sections, order and colours). "Open a packed notebook" unpacks it into the library. The app already has this for a single note (`.snbx` package, `source/batch/BatchOperations.h`); the work is to extend it to a whole notebook.

Still to decide:
- Where the library lives on disk: one root folder chosen in the settings.
- What happens to today's "starred" list and its folders: keep it as a "favourites" view.

### Part E - Hoverbox

A Hoverbox is a post-it on the page. You move it, resize it and lock it. It holds one or more blocks.
It is built on the object system that exists (`source/objects/InsertedObject.h`), so move, resize, lock and undo come with it.

| # | Step | Size | Status |
|---|---|---|---|
| E1 | **The box.** A post-it with a colour. Move, resize, lock. Holds a list of blocks. First block type: plain text (reuse the text box). | M | |
| E2 | **Code block.** Paste code: it becomes an editable block with the look of the user's FancyCode LaTeX package: rounded box, JetBrains Mono font, line numbers, coloured syntax, 8 background colours (sand, coffee, lilac, moss, ink, midnight, terracotta, paper). Reference: `C:\Users\sebas\Desktop\Fourre tout\Latex\FancyCode\FC\fancycode.sty`. The font license (OFL) allows us to ship it. | M | Needs E1 |
| E3 | **Web link block and image block.** | S | Needs E1 |
| E4 | **Calendar event block.** Drop an `.ics` file: the box shows the title, date, time and place. | S | Needs E1 |
| E5 | **Pen block.** Handwriting that belongs to the box and moves with it. | M-L | Needs E1. Hard part: today a stroke belongs to a page layer, not to an object. |
| E6 | **Math block (LaTeX).** Type an equation, see it drawn. | L | Needs E1. Hard part: Qt cannot draw LaTeX. We must pick a small math library or use a LaTeX program on the computer. To research first. |
| E7 | **More block types.** Search what people ask for most in note apps, then choose. | - | |

Later idea for E2: "copy as LaTeX" that gives a ready `\begin{FancyCode}` block.

### Part D - Clear and good-looking

| # | Step | Size | Status |
|---|---|---|---|
| D1 | **Settings you can understand.** One settings page with a search box, good icons and good readability. Each setting: a clear name and one line of help. The user asked for this again on 2026-11-19: today an option is hard to find. Also to fix here: the Background tab shows the app defaults, not the paper of the open notebook. | M-L | Next big step after the Part A tests |
| D0 | **Window bar like a native app (OneNote is the model).** The user's words, 2026-11-19: OneNote has the perfect top (logo, quick tools, minimise / maximise / close in one integrated bar); GoodNotes looks like a cheap Chromium web app; SpeedyNote looked the cheapest. | M | Step 1 done 2026-11-19: the Windows title bar has no grey band of its own, our bar colour runs under it, and the native buttons stay (`Qt::ExpandedClientAreaHint`, Qt 6.9+, in `MainWindow`). Step 2 done 2026-11-19: one row. From left to right: app icon, back, left panel, save, new, search, the title with the app name, full screen, export, right panel, menu, then minimise / maximise / close. Qt offers each press in that row to our buttons first, and empty space drags the window. **To check by the user: every button of the row reacts to a mouse click and to a pen tap.** Still to do: a real search field in the row (today the search button opens the search bar that exists). Also done 2026-11-19: house icon for home; minimise / maximise / close drawn with Lucide icons, placed exactly on the real Windows button zones (so snap, double-click and Win+arrows stay native); full screen is a real full screen (no title row, no tab bar, no taskbar, a small button to leave) and uses every pixel (it lost 8 px on three sides before). How it works: `MainWindow::hideQtTitleBarDrawing()` and the `WM_NCHITTEST` / `WM_NCCALCSIZE` cases in `MainWindow::nativeEvent()`. |
| D1b | **Each menu and window looks like a real app.** The user, 2026-11-19: the menus look "too generic", and some options cannot be understood. Each window gets the same treatment as the settings: clear words, grouping, the app's own style. Remove or explain each option that a student cannot understand. | L | With D1 |
| D2 | **Modern look.** First all colours and sizes in one place. Then one screen at a time. | L | |

## How the screen is built today

Checked in the code on 2026-11-19.

- Three bars are stacked at the top, in a fixed order: navigation bar, tab bar, toolbar (`source/MainWindow.cpp:1123`, `:1196`, `:1202`).
- The toolbar is one horizontal row with a fixed height. Its buttons are added in a fixed order (`source/ui/Toolbar.cpp:29`).
- The bar on the side is the "action bar". It shows copy, cut, delete and similar actions when a lasso or an object is selected. It is fixed 24 px from the left edge (`source/ui/actionbars/ActionBarContainer.h:20`).

## Why this order

- Part A first: it is the daily pain, and A1 must be tested on the real Surface before A2 is built on it.
- Part B next: these are the tools used in every class. B1 is small and gives a quick win.
- Part C after: it is the biggest change to how notebooks are stored, so it needs the most care.
- Part D last and in small parts: it touches the most files. But rule 3 means each step in A, B and C already brings its own clear settings.

## Risks to remember

- **Speed.** Pen types, dashed lines and the quick bar add work while you draw. Measure on an old device before and after. The app has a performance display (F10).
- **File format.** B3, C1 and C6 change what is saved. Old files must open. New files opened in the original SpeedyNote will lose the new parts.
- **The original project.** This repo follows another author's work. Big changes (A3, C1, D2) make later updates from that author harder to merge.
- **The big file.** `source/core/DocumentViewport.cpp` has 19,383 lines and 513 methods. New features go in new small files when possible.
- **Pen hardware.** The side button works only near the screen. Windows can keep the top click for itself. Both must be checked on the real device.

## Done outside the steps

- 2026-11-19: One unit setting for the whole app: millimetres or pixels (Settings > Page > Units, code in `source/ui/widgets/LengthSpinBox.h`). It covers pen and eraser width, grid and line spacing, page size and scroll speed. A page unit is 1/96 inch, so 1 unit = 0.2646 mm. Only the display changed, not the files. Limit: grid and line spacing are stored as whole units, so "5 mm" is kept as 5.03 mm. An exact 5 mm grid needs a change in the page format.
- 2026-11-19: Icons replaced by Lucide icons (https://lucide.dev, ISC license, text in `resources/icons/LUCIDE_LICENSE.txt`). 73 icons. To run again or change an icon: `resources/icons/lucide-icons.ps1` (needs `rsvg-convert` from MSYS2). Not replaced, no good match: `benchmark`, `dial`, `preset`, `savepreset`, `selection_ocr`, `selection_pdf`.
- 2026-11-19: Fixed: a black pen colour and its width dots were invisible on the dark toolbar.
- 2026-11-19: Fixed: pressing OK in the settings repainted every page of the open notebook with the app's default paper.
- 2026-11-19: Fixed: the top bar was white on white with a light Windows accent colour.

## Ideas for later

Add new ideas here so they are not lost.

- **Connect an AI (LLM) with your own API key.** A settings screen where you choose a service (Claude, Gemini, OpenAI, or any other with a compatible address) and paste your key. The key stays on your computer. Rule 1 applies: no AI code may slow the pen. What the user wants from it (handwritten list, 2026-11-19):
  - Index the content of the notes, to search in them.
  - Generate images.
  - An MCP connection, so an AI can create notes in the app.
  - Earlier ideas: explain a page, summarise notes, turn handwriting into LaTeX, answer questions about a PDF.

From the user's handwritten list of 2026-11-19 (each one still needs a size and a place in the parts above):

- **Hatching tool** (outil pour hachure): fill a closed area with parallel lines. Goes with shapes (B6).
- **Flip** a selection, horizontal or vertical (symétrie). Small: the lasso already rotates and scales.
- **Circle from a centre and a radius.** Goes with shapes (B6) and the ruler (B2).
- **Paper colour yellow / white / black, with or without effect on printing.** The colour exists today. New: a choice "print with the paper colour" or "print on white".
- **Insert space**: push the content below a line up or down, as in OneNote.
- **Floating toolbar that snaps to any side.** This is the rest of A3.
- **Templates: a ruler with an angle you set in degrees or radians, to 1 degree.** Extends B2.
- **Several notes open in split screen.** Exists today (split view, `Ctrl+\`). To check with the user if it does what they need.
- **More tools from GoodNotes, wanted but later** (the user, 2026-11-20): a pencil, a tape tool (it hides a part of the page until you tap it), stickers, sticky notes, audio recording. The new toolbar must leave room for them.
- Quick bar: you choose its content, more colours, line width, open with a long finger press.
- Toolbar: when undo and redo are both hidden, a small empty gap stays where they were.
