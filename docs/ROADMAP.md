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

## Where we are, and what is next (plan of 2026-11-22)

Everything up to here is in git: commit `83087fe` on `custom/pen-and-ui`, pushed to the user's fork.

### Built and tried by the user

The floating two-level bar, the colours, the stroke presets, the three pen icons, the selected-tool pill, draw and hold, clean shapes, hand-drawn arrows with the pen, line end handles, "Edit points", the selection menu next to the selection, the ruler, the marker shapes.

### Built, but not tried by the user yet

| What | How to try it |
|---|---|
| The X button with unsaved work (it froze the app before) | On a throw-away note: draw one line, click the X. The save box must answer. |
| Pen vibration (A4) | Settings > Stylus > Pen Vibration. Write, change tool, draw and hold. |
| Library page (C1) | Start screen > Library. New notebook, sections, a note in a section, drag and drop, delete, Pack notebook. |
| Fixes of 2026-11-21 | The three dots never minimise; their menu stays in the window; the settings window fits the screen; a tap on the selected tool hides its options; the tab bar after full screen. |
| Laser pointer (B1) | K key. |
| Earlier versions (R4) | In a note that has a place, draw and wait 5 s: the page as it was before is kept. The three dots > "Restore an Earlier Version...": pick a time, "Open as a Copy". After 15 min of changes there is a second time. Settings > Page > Saving. |
| The four faults (F1-F4) | With a mouse, hover the grey space next to a page (hand), then write with the pen: the pen cursor. Draw quick rough boxes with tails and overlaps, then hold: rectangles. Marker: a line, then a V at its end. Select a clean shape, corners button: move the slider, then Ctrl+Z once. |
| Papers drawn by code (N, stage S5) | The three dots > "Current Document Settings..." > Page > Background Style: pick Cornell, a month planner, a to-do list, staves. Zoom in and out, write on it, export to PDF. "Add a paper from a PDF or picture..." for your own. Settings > Page > Default Background sets the paper of new notes. |
| The New note window (N1, stage S6) | Ctrl+N, the "+" of the title row, the round "+" of the start screen, or a section of the Library page: the window opens. Pick a size, a colour, landscape, a paper, the place (Where), then Create: the note opens, already saved there, with its picture on the start screen. Try "Endless canvas", "Add from a PDF or picture...", and "Do not ask again" (then + > "New Note with Options..." or Settings > Page brings the window back). |
| Covers, move to a notebook, note pictures (N2-N4, stage S7) | New note window > Cover: pick a design, a colour, or "Picture...", then Create: the start screen card shows the cover. Library page > a notebook > right click > "Cover...": the notebooks column shows it as a book ("+" > "Show notebooks as covers" turns it off). Open a section: each note shows its cover or its first page; notes never opened get their picture within a second or two. Start screen > a card's menu > "Move to a Notebook...": pick a section. |
| Autosave and recovery (R1, R3) | Draw in a note, stop: the sign right of its name turns into a check within 5 s. Close the note: no question. On a new note: draw, wait 5 s, end SpeedyNote in the Task Manager, start it again: the note is offered back. Settings > Page > Saving. |
| Fixes after the review | Open a PDF directly, write on it, wait 5 s, end SpeedyNote in the Task Manager: the PDF is offered back. Settings: scroll over a list without clicking it, the page scrolls and the value stays. Start screen > Duplicate a note, open both. Ctrl+N with Settings > Page on Grid: the window shows "Grid, 32 px" first. |

### Step R - Never lose notes (first of all)

The user, 2026-11-22: "This is a work app. It must be robust. We do not want to lose weeks or months of notes." The X-button freeze already cost him his test notes twice.

| # | Step | Size |
|---|---|---|
| R0 | **Check how a note is saved today.** Read the save code: is each file written to a temporary file and then renamed (so a crash in the middle cannot leave a half file)? What happens when the disk is full, the folder is read-only, or a sync tool holds a file? Write the answers here before any change. | S |
| R1 | **Autosave.** The open note saves by itself: a short time after the pen stops (a setting, for example 5 s) and at least every few minutes, in the background, without a stutter while writing (rule 1). Only the pages that changed are written. A small sign shows "saved". A switch turns it off. | M |
| R2 | **Safe writes.** Every file of a note is written to a temporary file, flushed, then renamed over the old one. A failed save says so clearly and keeps the old file. | S-M |
| R3 | **Recovery after a crash or a freeze.** Changes not yet saved go to a small journal next to the note. At the next start the app says "This note has changes that were not saved" and offers to bring them back. | M |
| R4 | **Backups.** The app keeps the last few versions of each page (a number and an age, both settings), in the note's folder. "Restore an earlier version" in the menu. | M |
| R5 | **A watchdog test.** An automatic test that kills the app in the middle of a save, many times, and checks that the note always opens with either the old or the new content, never a broken one. | S |

#### R0 answers: how a note was saved before Step R (checked in the code on 2026-11-22)

- A note is a folder (`.snb`). `document.json` holds the settings and the list of pages. Each page is one file in `pages/` (each tile in `tiles/` for an endless note). Pictures are in `assets/images/`. OCR text sits next to each page (`.ocr.json`).
- Each page, tile and `document.json` was written through `QSaveFile`: the data goes to a temporary file, is flushed to the disk, then renamed over the old file. So a crash in the middle of one file could not leave a half file.
- But the note as a whole was not safe. `document.json` was written first, the pages after. A crash between the two left a list of pages that named page files never written. Those pages then failed to load.
- The OCR files, the `.snb_marker` file and the library list were written straight over the old file. A crash there could leave a half file.
- Disk full, read-only folder, a file held by a sync tool: the save stopped and the old files stayed. But the app only said "Failed to save document", without the reason.
- When quitting, a failed save showed a box with only OK, and the app closed anyway. The changes were lost.
- A Save As that failed early (a folder it could not create) left the note pointing at the new, unfinished folder.
- An endless note: a tile emptied and drawn again before the save was written, then deleted in the same save. Its drawing was lost.
- HarmonyOS cannot rename files in user folders, so there the files are written straight (unchanged, see `platform/BundleFile.h`).

What Step R2 changed (stage S1):
- One helper (`Document::commitFile`) writes every file of a note: temporary file, flushed, renamed. Not yet the markdown notes of `assets/notes/` (`MarkdownNote::saveToFile`, a plain write on every key typed): a commit there would flush to the disk on every key; it needs a short delay first (to do).
- The order is now: pages and tiles first, `document.json` last, old files removed after. A crash at any moment leaves the old `document.json` with all its pages whole, or the new one with all its pages present. Each page holds its old or its new content, never a mix.
- A failed save says why ("The disk is full." or "The folder is read-only, or a file is held by another program"), keeps the last saved version on disk, and offers: Try again, Save to another place, Cancel. Cancel when quitting stops the quit.
- The library list is written the safe way too.
- Test `--test-savecrash`: kills the app 40 times during saves, makes each file of a save fail in turn, and checks that the note always opens, each page old or new.

What Steps R1 and R3 changed (stage S2):
- **Autosave** (`core/AutoSaver`). A note saves itself 5 s after the last change, and at least every 2 minutes while you keep writing (at a 1 s pause). Never while a pen, finger or mouse is down, or while you type in a text box. Only the pages that changed are written.
- The GUI thread only takes a copy of the changed pages (the strokes are shared, not copied) and their small settings. Turning the strokes into JSON, the writing and the flush to disk run on one worker thread. A change made while a save runs stays unsaved and goes into the next save. A page that a running save holds is not unloaded until the save ends.
- Measured (debug build with AddressSanitizer, many times slower than the real app): a page of 400 strokes of 80 points costs the GUI thread 3 ms (1.1 s when the JSON was built there), 10 changed pages 7 ms; the event loop never stopped longer than 12 ms while the worker wrote.
- A failed autosave does not ask anything. The sign turns red; autosave tries again 30 s later; a click on the sign gives the usual choices (try again, save to another place).
- **The sign**, right of the note's name: a check ("Saved 14:05"), a circle while saving, a red triangle when a save failed. A new note without a place says "Kept safe. Press Ctrl+S to choose a place."
- **Settings > Page > Saving**: "Save notes by themselves while you work" (on), "Save after a pause of" (5 s, 1-60), "Save at least every" (2 min, 1-30).
- **Closing with autosave on**: a note that has a place is saved without a question. Only a failed save asks (with "Close without saving"). A new note without a place that holds work asks "Keep this note?": Save to..., Delete, Cancel. With autosave off, closing asks as before.
- **New notes without a place** (paged and endless) live in a temporary folder `<AppData>/unsaved/` (before: the system temp folder, which Windows Storage Sense can empty), and autosave writes there.
- **After a crash, a freeze or a kill**: at the next start, the notes without a place that hold work are offered: "SpeedyNote did not close properly. These notes were never saved to a place and were kept: ... [Open them] [Later]". They open as "(recovered)", still without a place. The old system temp folder is searched too. Then the usual "Restore Previous Session" box, with "Your notes were saved automatically up to the last few seconds." Notes that have a place need nothing more: autosave already wrote them where they belong. The list of open tabs is now kept after each tab opens or closes, so it survives a crash.
- What can still be lost: the changes of the last few seconds (up to "Save after a pause of"). (A PDF opened directly has a temporary folder since the review fixes.)
- Test `--test-autosave`: the timing rules with a fake clock, an edit during a save, a failed save and its retry, and a child process killed after an autosave whose note is then offered back with its strokes.

What Step R4 changed (stage S3):
- **Earlier versions** (`core/NoteVersions`). Before a save writes over a page, a tile or `document.json`, the old file is copied into the note's own folder: `versions/pages/<page>/<time>.json` (`versions/tiles/...`, `versions/document/...`). The name is the time the old content was written. A removed page is always kept. Pictures and OCR files get no versions. The copy runs on the autosave worker thread; only a page saved when it leaves memory copies on the GUI thread (at most once per page per spacing). A failed copy never fails the save.
- **How many**: a new version of a page at most every 15 minutes; at most 10 per page; versions older than 30 days are deleted, but the newest one of each page stays. When a note opens, a worker thread deletes what is over these limits, and the versions of removed pages once they are too old.
- **Disk use is bounded**: at most "versions kept per page" copies of each page you change (with the defaults, 10 times the size of the pages you changed). A page you never change has none. Measured: a heavy page of 400 strokes of 80 points is 903 KB, so its 10 versions take 9 MB at most; a page of handwriting is usually far smaller. After 30 days without a change, a page keeps one copy.
- **Restore**: the note's menu (the three dots) > "Restore an Earlier Version...". It lists the times, newest first, with the current page as it was then. "Open as a Copy" makes a new note without a place (in `<AppData>/unsaved/`, named "<note> (<time>)"): each page as it was at that time, the pictures and PDFs of the note, a new id. Nothing of the note is overwritten. If some pages have no copy that old, the window says so and the copy has them as they are now. Paged and endless notes; the preview is for paged notes only.
- **Settings > Page > Saving**: "Keep earlier versions of each page" (on), "New version at most every" (15 min, 1-240), "Versions kept per page" (10, 1-100), "Keep versions for" (30 days, 1-365).
- Packing a notebook (`.snbpack`) or a note (`.snbx`) leaves `versions/` out; Save As does not copy it. Old app versions ignore the folder.
- Fixed on the way: the settings window opened taller than the screen (the tabs kept the height they had before they could scroll).
- Test `--test-versions`: spacing, count and age, which file is picked for a time, a real note saved, a page removed, copies at two times, a pack without versions, a picture of the window.

Fixed after the review of stages S1-S7 (2026-11-22):
- **A PDF that loses its last page** (all its pages deleted): its bundled copy was deleted by the next autosave, so Ctrl+Z brought the pages back without their PDF. Deleting pages now keeps their PDF for undo, and a save removes a bundled PDF only after the new `document.json` is written.
- **Two folders with one id** (a Duplicate from the start screen, a copy made in Explorer): opening the copy pointed the open note at it, and autosave wrote there. Duplicate now gives the copy a new id and its own name. Opening a copy while the original is open says so and opens nothing. Deleting, renaming or moving a copy no longer closes the original.
- **Earlier versions of a removed page**: kept for "Keep versions for" days counted from the removal (not from when the page was last written), and a copy at an earlier time has a removed page even when the manifests of that time are already pruned (it goes at the end of the note).
- **Save As carries the whole `assets/` folder**, the markdown notes too (a note without a place lost them when it got one).
- **A folder moved or deleted while its note is open** is never rebuilt as a half note at the old place, by autosave, by closing or by Ctrl+S: the save says "The note's folder was moved or deleted." and offers another place.
- **A tile that fails to load** (a sync tool, an offline file) stays in the note; drawing there sets the old file aside (`tiles/x,y.json.unreadable-<time>`) before the new tile is written.
- **A broken `document.json`** opens from its newest earlier version. On HarmonyOS a failed write no longer deletes the file it was writing.
- **No writes on the GUI thread while autosave is on**: a changed page or tile that leaves the view stays in memory until autosave writes it (a change of paper on a 300-page note used to write 300 pages when the pen lifted). An OCR result for a tile that a running save holds no longer waits for that save.
- **The corners slider** moved and brought back to where it began leaves nothing pending (undo used to leave a floating copy).
- **The New note window keeps the classic Grid and Lines** of Settings > Page at their own spacing ("Grid, 32 px", first in Basic), instead of turning them into squared or lined paper and changing Settings > Page.
- **A PDF opened directly** gets a temporary folder: its annotations are autosaved and offered back after a crash. Closing it with autosave on asks "Keep this note?" only when it was annotated.
- **Your own paper from a PDF** is recognised by its content, and a PDF that Android hands over as `content://` is copied to a file first.
- Not done: with autosave off, the changes of a note that has a place are not kept anywhere for a crash (R3 asks for a journal; it needs its own design). The markdown notes are still written directly (see R2). On iOS, "Add a paper from a PDF or picture..." still uses the plain file dialog.

### Step F - Small faults found by the user on 2026-11-22

| # | Fault | Wanted |
|---|---|---|
| F1 | In pen mode the cursor is sometimes a hand, not the pen tip. It still writes. | Always the pen cursor when the pen tool is active. Find what leaves the hand (after a pan, a finger touch, a selection, the option bar?). |
| F2 | Not every hand-drawn rectangle becomes clean. In the user's picture of about 30 boxes, some stay wobbly, some keep a short tail where the stroke started or overshot, some become a trapezoid. | A box drawn fast, with an overlap or a small gap at the closing point, must still become a rectangle. Cut the tails. Prefer the rectangle over the trapezoid when the sides are nearly parallel. Test with many rough boxes. |
| F3 | Hand-drawn arrows do not work with the highlighter. | Same arrow recognition for the marker as for the pen. |
| F4 | The corner roundness is a menu with 4 steps (sharp, small, medium, large). | A slider in the small panel, from sharp to very round, that redraws the shape while it moves. One undo step. |
| F5 | In the settings, a scroll (wheel, finger or pen) over a list or a number box changes its value. The user found his "Selection menu" setting changed to "at the side" without wanting it. | A list or a number box in the settings changes by scroll only after it was clicked (it has the focus). Otherwise the scroll moves the page. One event filter for the whole settings window. |

What Step F changed (stage S4):
- **F1, the hand in pen mode.** The cause: only the mouse set the open hand over the grey space around the pages. The pen's hover never reaches that code, so a hand left by a mouse or a touchpad stayed while the pen wrote. The hand also stayed when the pointer left the view. Now the pen's hover sets the hand and takes it away like the mouse, and leaving the view, the pen going away and the end of a pan give the tool's cursor back.
- **F2, rough boxes.** Three changes in the shape cleaner. (1) The closing point is where the two ends of the line come nearest each other, so a tail at the start and an overshoot or an overlap at the end are both cut. (2) A corner where the fitted sides hardly turn (a wobble, a stub) is dropped. A corner rounded wide by a quick hand is found by a second look at half the points. (3) A box with one pair of sides parallel becomes a trapezoid only when the other pair differs by more than a new setting, Settings > Drawing > Shapes, "Trapezoid only when two opposite sides differ by over" (25°, 12-45°).
- Measured on generated rough boxes (tails, overshoots, overlaps, gaps, 3 to 25 units between points, up to 20° tilted, sides up to 15° off): 27 of 30 became rectangles before, 30 of 30 now (169 of 200 before, 197 of 200 now). Rough trapezoids and parallelograms stay what they are (5 of 5 each); rough triangles 172 of 200 before, 194 now; rough circles and ellipses unchanged.
- **F3, marker arrows.** Hand-drawn arrows now work with the marker too (a head after the line, or in the same stroke). A marker head does not finish a pen line, nor the other way round. The head is see-through like the line, without darker overlaps.
- **F4, the roundness slider.** The corners button of the selection panel opens a slider "Sharp ... Round". The shape is redrawn while it moves; letting go is one undo step (none if nothing changed). Arrow keys or the wheel work too (the step is kept 0.3 s after the last key). It works on a cleaned shape (lasso) and on a fixed shape (rectangle, triangle, diamond; the rounded rectangle keeps its own corners). The round corners are drawn with twice as many points, so a large radius has no bump where it meets a side.
- Saved: a fixed shape gets `"cornerRadius"` in its JSON only when the slider set it; `"roundness"` still holds the nearest of the old 4 steps, so an older version shows a rounding close to it. A cleaned stroke needs no new field.
- Fixed on the way: undo or redo while a line or a shape stayed selected after an edit left the edited copy floating over the restored one. The selection now lets go first.
- Tests: `--test-shapes` (30 rough boxes, trapezoids, parallelograms, the trapezoid setting), `--test-viewport-unit` (the hand and the pen, marker arrows, the slider and its undo).
- **F5, scroll in the settings** (fixed after the review): every list, number box and slider of the settings window takes the wheel only once clicked; otherwise the page scrolls (`DialogSizing::wheelNeedsFocus`, one event filter).

### Step N - A new note starts with a place and a paper (next feature)

The user, 2026-11-22: "When we create a new paged notebook, we decide neither where to save it from the start, nor the colour of the background before it opens. A minimum of configuration."

Today the round "+" button of the start screen makes a loose "untitled" note. It has no place on disk until the first save, and it takes the default paper. Only the Library page creates a note inside a section.

| # | Step | Size |
|---|---|---|
| N1 | **"New note" window, as rich as the GoodNotes one** (the user's screenshot of 2026-11-22: "I would like to have as many possibilities"). One window for every way to make a note (the "+" of the start screen, the "+" of the title row, Ctrl+N, the Library page). Left side: the title, the cover (with its picture), the paper (with its picture). Top row: the page size (A4, A5, Letter, a size of your own...), the paper colour (white, yellow, dark, your own), portrait or landscape. Main part: a gallery of papers sorted in groups that fold: Basic (plain, lined, squared, dotted), Writing paper, Planning (monthly, weekly, to-do lists, accounting), Music (staves, guitar tablature), and more; each one with a small picture. Also: where the note goes (a notebook and a section of the library, or another folder; it starts in the last place used), and paged or endless paper. The note is saved at its place at once. A switch "do not ask, use my defaults" for the fast way (rule 3). The papers must be drawn by code or by small vector files so they stay sharp and cost nothing while writing (rule 1), and a paper that an old version of the app does not know must fall back to plain (rule 2). The user can add his own paper from a PDF or a picture. | L |
| N2 | **An old loose `.snb` goes into the library.** It exists in the Library page ("Add an existing note here"). To add: the same action from the Chronologie and Favoris pages ("Move to a notebook..."), and drag a note from there onto a section. | S |
| N3 | **Notebook covers** (part of C6; chosen in the "New note" window too). A notebook of the library shows a cover: a colour, a title, a picture or a ready-made design. Saved in the notebook's small file. The notebooks column can show covers like books on a shelf. | M |
| N4 | **Small pictures of the notes** in the notes column of the Library page (first page of each note, made once and cached). | S |

#### The papers of the "New note" window (from the user's GoodNotes screenshots, 2026-11-22)

We draw our own version of each kind of paper. The list is the target for N1:

| Group | Papers |
|---|---|
| Basic | Plain; dotted; lined, narrow; lined, wide; squared |
| Writing paper | Cornell; legal (a red margin line); one column; one column, mixed; two columns; two columns, left; two columns, right; three columns |
| Planning | Accounting; monthly planner (several kinds); weekly planner (several kinds); to-do lists |
| Music | Guitar score; guitar tablature; music paper (staves) |

Each paper works with each page size, each paper colour, and portrait or landscape.

What the paper engine changed (stage S5):
- **Papers drawn by code** (`core/Paper`). Each paper is a few rules in one table: ruled lines, a grid, dots, single lines, frames, cells, check boxes, and labels (weekday names, day numbers). A position is a share of the page side plus millimetres, so every paper fits every size, both ways. 25 papers: Basic (plain, dotted, lined narrow and wide, squared), Writing paper (Cornell, legal, one column, one column mixed, two columns, two columns left and right, three columns), Planning (accounting, month in a 5- or 6-week grid, month as a list, week in boxes, rows or columns, to-do list in one or two columns), Music (staves, guitar tablature, guitar score).
- **Fast while writing.** The lines are built once and kept (last 32 page sizes). A pen event redraws only what lies under the pen. Lines closer than 2 screen pixels, dots closer than 3 and labels smaller than 4 are left out when zoomed far out. Measured twice (debug build with AddressSanitizer, A3): a 40x40 pen update costs 52-127 µs, 6 to 8 times less than the old grid at the same spacing (469-767 µs); a whole page of squared paper costs about the same as the old grid (8.3 against 9.3 ms, 13.6 against 14.2 ms).
- **Sharp at every zoom**: lines are one screen pixel at any zoom; labels are shapes, not a picture. Weekday names follow the system language and its first day of the week.
- **Endless canvas**: dotted, lined, squared and staves fill it, laid out from the world origin so the tiles join without a seam. Other papers draw their fallback there.
- **Saved** (all optional, written only when used): a page gets `"paper"` (and `"customImage"` for your own paper); `default_background` in `document.json` gets `"paper"` and `"custom_image"`; new pages copy them. The page's old `backgroundType` keeps the paper's fallback: lined papers are "lines" with their spacing (24, 27, 33 or 34 px), squared is "grid" (19 px), the others "none". An older SpeedyNote shows ruled or plain paper; if it saves the page, the paper name is dropped and the page stays on its fallback. A paper name this version does not know draws its fallback and is kept.
- **PDF export**: the paper goes into the PDF as vector lines and filled shapes (labels too, no fonts).
- **Your own paper**: "Add a paper from a PDF or picture..." in the note's settings. The first page of a PDF (150 DPI) or a picture, at most 2400 px on its long side, is kept as a PNG in `<AppData>/papers` and listed as "My paper: <name>". When chosen, it is copied into the note (`assets/images/paper-<name>.png`) and stretched over each page. One picture in memory for all pages. The orphan cleanup keeps it; Save As, packing and versions copy it with the other pictures. Not for endless notes. An older version shows plain paper.
- **Settings**: the background list of Settings > Page and of the note's settings lists every paper after None / Grid / Lines; the spacing boxes are greyed out when a paper brings its own. New setting `background/paper` (the paper of new notes; empty = the old type).
- Fixed on the way: changing a note's background did not mark its pages as changed, so a page unloaded or reopened came back with its old background.
- Test `--test-paper`: every paper inside A4, A5 and Letter both ways; endless tiles join; small repaints match one whole paint; saved keys; fallback; unknown names; PDF operators; a real PDF export read back; your own paper through save, cleanup and reload; timings; pictures in `%TEMP%` (`speedynote_papers*.png`, `speedynote_paper_*.png`).

What the New note window changed (stage S6):
- **One way to make a note** (`MainWindow::newNote`): Ctrl+N, Ctrl+Shift+N, the "+" of the title row, the macOS File menu, the round "+" of the start screen and the Library page all open the New note window (`ui/dialogs/NewNoteDialog`). Its Create calls `MainWindow::createNote`, which makes the note, puts the choices on it (`NewNote::apply`), **saves it at its place at once**, adds it to the start screen with its picture, and opens it. If that save fails, the usual box asks (try again, another place); on Cancel the note stays open without a place, kept safe like any new note.
- **The window**, in the style of the start screen, light and dark: on the left the title ("Note 2026-11-22"), a picture of the page as chosen, its paper and size; at the top the page size (A3, B4, A4, B5, A5, Letter, Legal, Tabloid, a size of your own in mm or px), the paper colour (white, cream, yellow, dark, a colour of your own), portrait or landscape, pages or endless canvas; in the middle the papers in groups that fold (Basic, Writing paper, Planning, Music, My papers with "Add from a PDF or picture..."); at the bottom "Where" and "Do not ask again, use these settings". A tap chooses a paper, a double tap or Enter makes the note, a drag scrolls (finger, pen or mouse). With "Endless canvas" only papers that go on forever can be picked; a page paper gives way to the nearest one (lined, squared or plain) and comes back with "Pages". On a screen narrower than 700 px the parts stack and the page picture is left out.
- **Where** (`ui/widgets/LibraryPlacePicker`): shows "Notebook › Section" for a place in the Library, else the folder. Its menu lists the notebooks and their sections from the Library, and "Another folder..." on a computer. It starts in the last place used, else the last save folder, else Documents/SpeedyNote (the app's notebooks folder on a phone or tablet). From the Library page it starts in the chosen section; the name prompt there is gone.
- **Settings**: `newNote/ask` (on) is the switch of the window and is also in Settings > Page: "Show the New note window. Off: a new note uses your last choices at once." "+ > New Note with Options..." always shows it. The last choices: `newNote/place`, `newNote/landscape`, `newNote/edgeless` (the mode of "with options"), `newNote/customImage` (your own paper). Size, colour and paper are the ones of Settings > Page (`page/width`, `page/height`, `background/color`, `background/gridColor`, `background/paper` + `background/type`), so the window and the settings never disagree. Without a paper yet, the classic background of Settings > Page stays, at its spacing: the gallery shows it first as "Grid, 32 px" or "Lines, 32 px" (none → plain). An endless note does not change the paper or size of pages.
- Settings > Page shows a size of your own as "Custom (...)" instead of turning it into US Letter on OK.
- Nothing new in the note's files: the window writes the paper, size and colour keys of stage S5.
- Small pictures of papers (`Paper::paintDrawing` gained a `detail` factor): the gallery shows each paper on an A5-tall page, so a 5 mm grid, dots and staves are still seen at tile size; the page picture on the left is exact.
- Test `--test-newnote`: a note made from the choices (size, landscape, colour, Cornell and its fallback, your own paper copied into the note, an endless note), saved at once and loaded back, not marked changed; " (2)" for a name taken; "Notebook › Section"; remember then defaults; the window's Paged/Endless paper rules; pictures `%TEMP%\speedynote_newnote_{light,dark,landscape,endless}.png`. It puts back every setting and start-screen entry it touches.

What covers, moving and note pictures changed (stage S7):
- **Covers drawn by code** (`core/CoverPainter`): plain, spine (a dark band at the left), stripes, dots, frame, label (a white title box), linen, split, picture (your photo fills the cover). A colour, and the title on the cover or not. A cover is a small object `{"design", "color", "showTitle", "image"}`; only what differs from the defaults is written.
- **The cover window** (`ui/widgets/CoverPicker`): a preview, the designs (and "No cover"), 13 colours and a colour of your own, "Show the title on the cover", "Picture...". Used by the New note window (the Cover row on the left) and by "Cover..." in a notebook's menu on the Library page.
- **A note's cover** is saved as the optional `"cover"` key of its `document.json`; its picture is copied into the note (`assets/images/cover.jpg`, at most 1200 px, PNG when it has see-through parts) and kept by the clean-up of unused pictures. The start screen cards (Chronologie, Favoris, search) show the cover as a book instead of page 1. The New note window keeps the last design and colour for the next note (`newNote/cover`), not a photo.
- **A notebook's cover** is saved as `"cover"` in its `.speedynote-section.json` (the other keys stay); its picture is copied into the folder as `.speedynote-cover.jpg`. The notebooks column of the Library page shows each notebook as a book: its cover, else a spine book in the notebook's colour. Setting `library/showCovers` (on): "+" of the notebooks column > "Show notebooks as covers"; off gives the small colour mark of before.
- **Move to a Notebook...** (N2): in the menu of a card on the start screen (Chronologie, Favoris, search). It opens the menu of places of the New note window (notebooks, sections, another folder) and moves the note there. The note closes first if it is open (the editor saves or asks), and the start screen, the stars and the picture follow it. The Library page's drag and drop uses the same code (`LibraryView::moveNoteInto`). Dragging a card onto a section is not done (two different pages).
- **Pictures of the notes** (N4) in the notes column of the Library page, 48 × 64: the cover, else the start screen's cached picture of page 1. Listing reads only each note's `document.json` and the cached picture, never the note. A note without a picture gets one on an idle tick (one note every 0.15 s, only while the Library page is shown): it is loaded, page 1 (or the canvas around its last place) is drawn and kept in the same cache as the start screen. A note that cannot be loaded is not tried again in that session. The picture code moved from MainWindow to `ThumbnailRenderer::renderFirstPage`.
- The thumbnail cache now also works for a note that was never on the start screen (its id is read from its `document.json`).
- An older SpeedyNote ignores the covers: a card shows page 1. If it saves such a note, the cover key is dropped and its picture may be removed as unused; the note itself is not touched.
- Test `--test-covers`: every design, a picture that fills, a big picture copied smaller, a note cover through apply, save, clean-up and reload, the start screen's knowledge of it, a notebook cover with its other keys, moving a loose note into a section, a note never opened getting its picture from the Library page (paged and endless). Pictures in `%TEMP%`: `speedynote_covers.png`, `speedynote_card_cover.png`, `speedynote_library_covers.png`, `speedynote_library_nocovers.png`, `speedynote_coverpicker.png`, `speedynote_newnote_cover.png`.

### After that, in this order

1. **D1 - Settings with categories.** The toolbar work added many settings, spread over three tabs. One page per category (Interface, Writing experience, Pen and touch, Shapes, Page, Files), a search box, one line of help per setting.
2. **C1b - A whole notebook as one PDF.** Then **C2 - structure templates** (a new class with its sections in one action).
3. **C6 - More page designs** (dots, Cornell, planner).
4. **D3 - The app font**, with the user (which font).
5. **B7** is done in practice (handles on shapes). To close after the user's test.
6. Part E (Hoverbox), A5 (profiles), and the "wanted later" tools (pencil, tape, stickers, sticky notes, audio).

### Lessons of these three days (for whoever works next)

- **Unit tests were not enough.** The first batch passed every test and failed in the user's hands. Since then each change is checked in a real launch, with a synthetic pen, finger and mouse.
- **Never build in the repo while the user's app runs.** `compile.ps1` deletes `build\` first. Build in a copy of the tree instead.
- **The app is single-instance.** A second launch talks to the user's running copy. No launch while a copy that is not ours runs.
- **A launch clears the saved list of open tabs.** Save `session/lastOpenTabs` before and put it back after.
- **Save often was not enough for the user:** the note `Math-TIN` lost two days of test drawings to the X-button freeze. An autosave is worth a step of its own (see "Ideas for later").

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
| A4 | **Top click and vibration** of the Slim Pen 2. Windows only. | M | Vibration written 2026-11-21 (`source/core/PenHaptics.cpp`, Windows 11 + C++/WinRT): a click when something snaps into place, a light click on a tool change, a paper feel while writing (by tool). Settings > Stylus > Pen Vibration. Decision logic tested (`--test-penbindings`). **Not yet felt on the real pen.** Top click: still to do. |
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
| T1 | **The floating bar.** It floats above the page and takes only the room it needs. Drag it; it snaps to the top, bottom, left or right, never the middle. Vertical on the left and right. (This is the rest of A3.) | M | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| T2 | **Two levels.** Main bar = tool families. Option bar = the options of the tool in use. This replaces the options that open inside the row today. | M-L | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| T3 | **Colours without a limit.** Any number of colours in the option bar. Add, remove, change, reorder. The row scrolls when it is long. Each tool keeps its own list. | M | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| T4 | **One eraser, clear choices.** One eraser button. Its options use plain words and a small picture: "the whole line", "only what I touch", "all lines in a loop". | S | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| T5 | **Pen window.** Pen type with a live preview, stabilisation slider, "draw and hold" switch, link to the pen button settings. | M | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| T6 | **Shape bar.** The shapes and connectors above, line style, colour. | M-L | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |

**Decided by the user on 2026-11-20: go all in.** Build T1 to T6 in one batch, with the parts of Part B they need (B3, B4, B5, B6). "Make sure the pen has dotted and dashed lines as well": the line style is a choice of the pen, not only of the shapes. The brief for that work is `docs/NEXT_SESSION.md`.

What the user's second recording of the GoodNotes free shape tool shows (2026-11-20; full description in `docs/NEXT_SESSION.md`): each stroke is cleaned when the pen lifts (straight parts become straight, curves stay smooth, corners where the pen turned); it closes only when the end is near the start, otherwise it stays an open smooth curve; the ends of a new stroke snap onto the edge of a shape that is already there, so shapes join; the eraser removes one whole shape at a time.

### Part B - Tools for class (engineering first)

| # | Step | Size | Status |
|---|---|---|---|
| B1 | **Laser pointer.** A trail that fades, and is never saved. For when you explain to a friend. | S | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| B2 | **Ruler.** A ruler on the page that you move and turn. The pen follows its edge. | M | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| B3 | **Stroke style in the file.** Two optional fields in a stroke: pen type and line style. Opens B4 and B5. | S | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| B4 | **Dotted and dashed lines.** | S | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| B5 | **Pen types.** Ball pen, fountain pen, brush pen, with sizes. | M | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| B6 | **Shapes, two ways.** (1) Hold the pen still at the end of a stroke: that one stroke becomes clean. (2) **Shape mode** (a switch): each stroke is cleaned when the pen leaves the screen. The stroke is cut where the pen stops or turns, each part becomes a straight line, and the shape closes if the end is near the start. It then becomes a line, triangle, rectangle, polygon, circle or ellipse. | M-L | Code written 2026-11-20, all test groups pass. **Not yet tried by the user.** |
| B7 | **Shape handles.** Rotate and resize a shape with 4 corner handles. Reuse the lasso handles. | S-M | Needs B6 |

**Pen rule for all of Part B: four independent choices.** Colour, width, pen type (pencil, ball pen, fountain pen, brush pen) and line type (solid, dotted, dashed). A change to one never changes the other three.

### Part C - Your library

| # | Step | Size | Status |
|---|---|---|---|
| C1 | **Notebook > section > sub-section > note.** See "The library model" below. A side panel with 3 columns, like OneNote: notebooks, sections, notes. | M-L | Built 2026-11-21 ("Library" page of the start screen, `source/core/LibraryTree.*`, `source/ui/launcher/LibraryView.*`, packed notebook `.snbpack`). To test by the user. |
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

Decided by the user on 2026-11-21:
- **No single root folder.** The user chooses the folder each time he creates a notebook. The app keeps the list of the notebook folders it knows.
- **"Pack notebook": yes.** One file to carry or back up a whole notebook. The user's worry: a folder with inner folders is easier to damage than one file. The answer is this pack file, and that one damaged file costs one note only.
- Today's "starred" list stays, as a "favourites" view.

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
| D1 | **Settings you can understand.** One settings page with a search box, good icons and good readability. Each setting: a clear name and one line of help. The user asked for this again on 2026-11-19: today an option is hard to find. Also to fix here: the Background tab shows the app defaults, not the paper of the open notebook. | M-L | Next big step after the Part A tests. The user again on 2026-11-21, after the toolbar batch added many settings: "We will need to create a setting panel with categories, interface, writing experience, and many other things in order for the user not to get lost." So: a list of categories on the left (Interface, Writing experience, Pen and touch, Shapes, Page, Files...), one page per category, a search box. Each new feature keeps its settings (rule 3), and each setting goes in its category. |
| D0 | **Window bar like a native app (OneNote is the model).** The user's words, 2026-11-19: OneNote has the perfect top (logo, quick tools, minimise / maximise / close in one integrated bar); GoodNotes looks like a cheap Chromium web app; SpeedyNote looked the cheapest. | M | Step 1 done 2026-11-19: the Windows title bar has no grey band of its own, our bar colour runs under it, and the native buttons stay (`Qt::ExpandedClientAreaHint`, Qt 6.9+, in `MainWindow`). Step 2 done 2026-11-19: one row. From left to right: app icon, back, left panel, save, new, search, the title with the app name, full screen, export, right panel, menu, then minimise / maximise / close. Qt offers each press in that row to our buttons first, and empty space drags the window. **To check by the user: every button of the row reacts to a mouse click and to a pen tap.** Still to do: a real search field in the row (today the search button opens the search bar that exists). Also done 2026-11-19: house icon for home; minimise / maximise / close drawn with Lucide icons, placed exactly on the real Windows button zones (so snap, double-click and Win+arrows stay native); full screen is a real full screen (no title row, no tab bar, no taskbar, a small button to leave) and uses every pixel (it lost 8 px on three sides before). How it works: `MainWindow::hideQtTitleBarDrawing()` and the `WM_NCHITTEST` / `WM_NCCALCSIZE` cases in `MainWindow::nativeEvent()`. |
| D1b | **Each menu and window looks like a real app.** The user, 2026-11-19: the menus look "too generic", and some options cannot be understood. Each window gets the same treatment as the settings: clear words, grouping, the app's own style. Remove or explain each option that a student cannot understand. | L | With D1 |
| D3 | **An app font of our own.** The user, 2026-11-20: change the default font of the app, to give it an identity. To choose with the user: which font (the license must allow us to ship it, for example OFL), and a setting to go back to the system font. | S | |
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

- 2026-11-23: Fixed, with a bar docked at the top of the screen (PowerToys): the side of the maximise button closed the window (our three window buttons are 36 px wide, Qt's own zones 45 px; every click on them is now ours); the window opened at 0,0 with its title row under that bar (now centred in the free part of the screen); a restored window came back lower at each restore.
- 2026-11-23, asked by the user, built, **not tried by the user yet**:
  - **The library in the left side panel** (`source/ui/sidebars/LibraryPanel.*`): a "Library" tab after Layers. One tree: notebook > section > sub-section > note, same folders and order as the Library page. A tap on a note opens it, a tap on a folder opens or closes it. A folder is read only when it opens. To make, rename, move or colour, the Library page stays the place.
  - **The cover is page 1** (`NewNote::apply`): a new paged note with a cover gets the cover as its first page (drawn once into `assets/images/paper-cover-page.png`, used as that page's own paper) and the chosen paper from page 2. You write on it and it prints. Limits: a later rename or a new cover does not redraw this page; notes made before keep their cover as a card picture only.
  - **The app font is Rubik** (D3, done; OFL, `resources/fonts/`, the variable font file). Settings > Theme > "Use the SpeedyNote font (Rubik)" goes back to the system font at the next start (`ui/appFont`).
- 2026-11-23, second batch asked by the user, built, **not tried by the user yet**:
  - **Every option of a side panel is at its end.** The page bar (find, page up and down, the wheel, columns, select, add, insert, PDF pages, delete) left the page and sits under the pages, in two rows (`PagePanelActionBar::setInPanel`; its lock button is gone, it had no more use). The Library panel got its own row.
  - **The Library side panel does the work too**: new note (the New note window, starting in the chosen section), new section, rename, delete, at the end of the panel and in the menu of an entry (right click), plus "Set colour...". Drag a note or a section onto a section to move it there, onto a note to put it just before that note. Mouse or pen; a finger scrolls. A new notebook is still made on the Library page. The work is LibraryView's (`newSectionWithPrompt`, `renameWithPrompt`, `deleteWithPrompt`, `moveNoteInto`), so an open note closes first.
  - **The picture tool's options are under the toolbar** (`ObjectSubToolbar`): Select, Add a picture, Paste. The bar at the side of the page shows only when something is selected.
  - **A picture can be fixed to the page**: the lock button of a selected object. A fixed object can be selected with the picture tool, but not moved or resized, and a finger tap passes over it. It uses the `locked` key that objects always had. No undo step for the switch. Delete still works on it.
  - **Keep the proportions** has its own icon (it had the lock).
  - **A finger moves pictures**: a finger tap on a picture selects it, in any tool (the tool comes back on a tap on empty paper, as for shapes); a finger on the selected picture moves it; a finger anywhere else still pans.
  - **The notes of last time open again without a question.** Settings > Page: "When SpeedyNote starts, the notes open last time": Open again (default), Ask me, Stay closed (`session/restore`).

- 2026-11-23, third batch, built, **not tried by the user yet**:
  - **The pen always writes.** The fault: after a start in tablet mode an arrow stayed frozen on the page and the pen drew nothing more, while the tool buttons still reacted. The cause found in the code: a mouse or finger gesture whose release never came (a pen press that reached the page as a plain mouse press and was released as a pen event, or a release taken by another window) left the page "busy with the mouse", and `tabletEvent` then swallowed every pen event for the rest of the session. Now a pen press ends such a gesture first (`DocumentViewport::endStalePointerGesture`; a mouse gesture is stale after 0.3 s without a mouse event; a shape that was only a preview is dropped). Not made to happen again on the device: the cause is read from the code.
  - **Chronologie shows a notebook as one card**: a note that is in a notebook of the Library shows as that notebook (its name, its cover, else a plain book in its colour), once, at the place of its note used last. A tap opens that note, or the notebook on the Library page. Settings `timeline/groupNotebooks` (on) and `timeline/notebookTap` ("note", or "library"), in Settings > Interface > Start screen. The card's menu still acts on the note used last.
  - **A note of a notebook in the favourites**: "Add to the favourites" in the menu of a note, on the Library page and in the Library side panel.
  - **Library side panel with a finger**: rows 38 px high; a press held still opens the menu (finger, pen or mouse); "Move to..." in the menu moves a note or a section without a drag.
  - **A finger on a shape** moves the selected shape only where the shape tool would move it too (in line mode it started a new arrow).
  - **Settings with categories (D1, first pass).** A search box, a list of categories on the left (Interface, Writing experience, Pen and touch, Shapes, Page, Files, Shortcuts, About), one scrolling page per category; under 700 px wide the list is a combo box. The search reads names, help lines and tooltips, hides the categories without a match and marks the matches. Controls are 36 px high at least and the pages scroll with a finger. Where each old tab went: Page -> Page (Saving -> Files); Theme, Language, toolbar buttons, selection menu -> Interface; Tools and Drawing -> Writing experience, Shapes (shapes, ruler) and Pen and touch (panning, finger); Stylus -> Pen and touch; Cache -> Files. The old tabs Features, Button Mapping, Mouse Dial, Performance and Compatibility were already dead code (never shown), so nothing of them moved. Still to do: icons in the list, the search does not read the entries of a list box.

- 2026-11-28, asked by the user, built, **not tried by the user yet**:
  - **A 7 or a 4 is no more an arrow.** The fault: a line, then a second stroke near its end, became an arrow. Now only a line and its head drawn in one movement make an arrow. New setting `pen/handArrowTwoStrokes` (off): Settings > Shapes > "A head drawn after the line makes an arrow too" brings the old way back. With it off, a double arrow needs the stroke preset (arrow at the start).
  - **Multi-pen.** Several named pens of one pen type (fountain, ball, brush), each with its own three stroke presets: one pen for A4 and one for A5, for example. Settings > Writing experience > Pen > "Multi-pen": on/off, the pen type, the list of its pens (add, remove, double tap to rename). It applies at once. With it on, a loop button is the first button of the pen bar: a tap goes to the next pen of the pen type in use and shows its name for 1.5 s in a card left of the bar. The sizes of a pen are set in the pen bar as usual; a new pen starts from the usual three. A pen type without pens works as before. Saved in the app settings only (`pen/multiPen`, `pen/multi<type>/<id>/name` and the preset keys, `pen/multi<type>/current`); nothing in the notes. Not done: reorder the pens, a pen button or key for "next pen", a pen of its own per tab. Test: `--test-buttons` (`testMultiPen`).

## Ideas for later

Add new ideas here so they are not lost.

- **A cover page for a notebook and for each section** in the PDF of a whole notebook (the user, 2026-11-23). Needs C1b.

- **Autosave.** Save the open note by itself every few minutes and before a risky action, with a setting. Asked by nobody yet, but the freeze of 2026-11-20 cost the user his test notes twice.

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
