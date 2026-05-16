# User Guide — edgeDetect

A three-step pipeline for turning raster images into clean binary outlines
with a human in the loop:

```
photos  →  toMultiCanny  →  cannyToOutline  →  outlines
                                      ↓
                                outlineChooser
                                      ↓
                                final outlines
```

* **toMultiCanny** — command-line tool. For each input photo it produces
  one "multi-Canny gray" PNG by running Canny at many thresholds and
  baking the result into a single grayscale image.
* **cannyToOutline** — GUI editor. You load a multi-Canny gray PNG and
  carve a clean binary outline out of it by clicking on segments.
* **outlineChooser** — GUI merger. When you have two competing outline
  variants for the same image (e.g. one from a model and one hand-edited),
  it lets you cherry-pick segments from each into a single result.

## Building

Requires CMake ≥ 3.21, C++20, Qt 6, OpenCV 4. From the repo root:

```bash
cmake -B build
cmake --build build -j
```

The three executables land at:

* `build/toMultiCanny/toMultiCanny`
* `build/cannyToOutline/cannyToOutline`
* `build/outlineChooser/outlineChooser`

---

## 1. toMultiCanny (CLI)

```
toMultiCanny <in_dir> <out_dir> [--invert]
```

Reads every image in `<in_dir>` (`.png .jpg .jpeg .bmp .tif .tiff`), writes
one PNG of the same stem to `<out_dir>`. The default output is **dark lines
on a white background**; pass `--invert` to flip.

Per file you'll see timings:

```
foo.jpg -> foo.png  [imread=12  cvtColor=2  gauss=3  canny(prep+256x)=180  …]
```

The 256-iteration Canny is amortized: Sobel + non-maximum suppression run
once per image; only the hysteresis pass is repeated.

---

## 2. cannyToOutline (GUI)

The editor where the actual outline gets built. Load multi-Canny grays,
carve out an outline by clicking, save.

### Projects

A project is a small JSON file (`*.ctoprj`) that remembers four
directories:

* **Source** (required) — the multi-Canny gray PNGs.
* **Output** (required) — where outline PNGs are written. Same filenames
  as the source.
* **Reference** (optional) — outline PNGs used by the *Fit to others*
  tool.
* **Original** (optional) — the original photos. Used as an alternate
  background. Looked up by filename stem, any common image extension.

Create with **File → New project**. Reopen with **File → Open project** or
the **Recent projects** submenu.

### Navigating

* Image list is alphabetical inside Source.
* **Page Up / Page Down** or the ◀ / ▶ buttons — previous / next file.
* The spinbox in the toolbar — jump to image *n*.
* `Ctrl+E` (cannyToOutline) — jump to the first source without a saved
  outline.
* `Ctrl+E` (outlineChooser) — jump to the first file not marked Done.

Switching files prompts to save if the current outline has unsaved changes.

### View modes

A single **View** combobox in the toolbar (or digit keys `1`..`7`) cycles
through display presets:

| Key | Preset | What you see |
|-----|--------|--------------|
| 1   | Original                        | Just the original photo. |
| 2   | Original + outline red          | Photo with your outline overlaid in red. |
| 3   | Outline black                   | Just your outline, black on white. |
| 4   | Gray source                     | Multi-Canny gray map, no outline. |
| 5   | Source (all black)              | Multi-Canny pixels rendered as solid black silhouettes. |
| 6   | **Gray source + red outline**   | Editing view. *(default)* |
| 7   | **Black source + red outline**  | Editing view, source as silhouettes. |

**Editing only works in modes 6 and 7** — the two that show both the
source and the outline. In every other mode mouse buttons just pan; this
prevents accidental edits while you're inspecting results against the
original photo.

**Tab** swaps to your previously active mode. Useful for quickly flipping
between, say, mode 2 (compare against photo) and mode 6 (keep editing).

If no original photo is loaded, modes 1 and 2 are disabled.

### Editing

#### Ctrl + click — add or remove a segment

Hold **Ctrl** and the cursor turns into a small arrow surrounded by a
dotted yellow tolerance ring. Click on:

* a red outline pixel → **remove** that whole segment from the outline.
* a gray source pixel → **add** that whole segment to the outline.
* empty/white space → the **nearest** seed within the tolerance ring is
  picked (whichever is closer wins between "nearest outline pixel" and
  "nearest source pixel").

The "segment" is the connected component of same-value source pixels
containing the seed.

Modifiers and toolbar options that affect what counts as the same segment:

* **join tol** spinbox — accepts pixels within ± *tol* of the seed value.
* **all darker** — also accepts any pixel strictly darker than the seed.
* **8-conn** — 8-connectivity flood fill (otherwise 4-connectivity).

#### Shift + drag — rectangle select

Drag a rectangle. On release a dialog asks for a brightness threshold and
a *Touching / Inside* mode:

* **Touching** — every same-value component with at least one pixel
  inside the rectangle.
* **Inside** — only components fully inside the rectangle.

Pixels with `gray ≤ threshold` are added to the outline; the rest are
left as gray.

#### Shift + click ×3 — oriented strip

Three clicks: P1 (axis start), P2 (axis end), then a third click whose
perpendicular distance to the axis defines the strip width. The strip is
then treated like a rectangle (same dialog, Touching/Inside).

Right-click or Escape during the strip cancels.

### Filters (toolbar row 1)

Independent of the view mode. With **filter** on, pixels are shown only if
they're in the `lo..hi` range AND pass the component-size and
component-extent thresholds (row 2 spinboxes). **filter outline** extends
the same rules to the outline layer.

### Bulk tools

* **F4 Fit to others** — opens a side panel that aligns the current
  outline to a reference outline (the *referenceDir* in the project, or
  pick one). Useful for reconciling outputs of different models. The
  **Bulk** button applies the same Fit settings to every file in the
  project, first snapshotting outputs into `<outputDir>_N`.
* **F5 Threshold add/remove** — opens a side panel with two pixel-wise
  rules (`gray ≤ addVal → add`, `gray ≥ removeVal → remove`). The **Limit
  to region** option restricts both rules to a Shift-rect or Shift-strip.
  **Bulk** applies the rules to every file (Limit-to-region forced off).

Each bulk run takes one slot on the undo stack per file; you can roll back
manually from `<outputDir>_N` if needed.

### Saving

`Ctrl+S` saves the current outline. Switching files or closing the window
prompts if there are unsaved changes. Files are 1-bit PNGs (very small,
0 = line, 255 = background — inverse of the in-memory representation).

### Undo / redo

`Ctrl+Z` / `Ctrl+Shift+Z`. Each editing primitive is one entry; bulk Fit
and bulk Threshold each push one entry per modified file.

---

## 3. outlineChooser (GUI)

For comparing two outline candidates and merging the best segments into a
final result.

### Projects

`*.ocproj` JSON:

* **Source** — multi-Canny gray PNGs (used as a background option and as
  the source-of-truth for flood-fill segments).
* **Outlines 1**, **Outlines 2** — the two candidate outline directories.
* **Output** — final outlines. May start empty; created on first save.
  **If a result PNG already exists for the current file, it is loaded**,
  so you can resume work.
* **Original** (optional) — original photos. Looked up by stem.

### View modes

| Key | Preset | What you see |
|-----|--------|--------------|
| 1   | **Standard** *(default)*    | White bg; black = in result, red = only in outline 2, green = only in outline 1, dark yellow = common but **not** in result. |
| 2   | Original only               | Just the original photo, no overlays. Display-only. |
| 3   | Original + result red       | Original photo, every pixel currently in your result drawn as a semi-transparent red overlay. |
| 4   | Original + result + diff    | Original photo with diff colours over it (green/red/yellow); result pixels are shown in the same colour as their origin outline. |
| 5   | Gray source + diff          | Same diff colours over the multi-Canny gray map. |
| 6   | Result only                 | Just your result, black on white. |
| 7   | Result + outline 1 (green)  | Result in black + the *non-accepted* outline-1 pixels in green (so you can see in isolation what only outline 1 would still add). |
| 8   | Result + outline 2 (red)    | Same idea for outline 2. |
| 9   | Gray + result + diff        | Standard palette on a gray-source background. The only preset that supports the "click on gray" advanced edit (see below). |

Same shortcuts as cannyToOutline: digit keys pick a preset, **Tab** swaps
with the previous one.

#### When can you edit?

Editing by click works only in presets where you can both **see the
current result** AND **see at least one candidate outline outside the
result** — so it's meaningful to choose what to add. With the built-in
presets that means **1, 3, 4, 6, 7, 8**. Presets **2** (Original +
result red) and **5** (Result only) are display-only — clicks just pan.

#### "Click on gray" (advanced)

The **Gray + result + diff** preset (8) additionally lets you add a
segment to the result by clicking on a **gray source pixel** that is
not in either candidate outline — useful when you spot an edge both
candidates missed.

This is gated by **Edit → Allow click on gray (advanced)**. The first
time you click on a gray pixel with the option off, a dialog asks
whether to enable it: **Enter** = Yes (turns it on for the session and
performs the edit), **Esc** = Cancel.

Presets 6 and 7 are handy when one of the two candidate outlines has
many more (often noisier) pixels than the other but the smaller one
contributes a few useful additions — flip between them and the Standard
view to decide which extras to bring in.

### Editing

There's just one gesture: **Ctrl + click**.

* A bare click without Ctrl always pans / does nothing — this prevents
  accidental edits while you scroll around the image.
* Hold **Ctrl** and the cursor immediately switches to the pick cursor
  with a dotted tolerance ring.
* A Ctrl-click that doesn't move the cursor more than 3 px is treated
  as an edit.

Lookup depends on the color under the click:

* **Black** (a pixel in your result) — flood the same-value segment and
  **remove every pixel of that segment from the result**.
* **Green / Red / Yellow** (only-O1, only-O2, common-not-in-result) —
  flood the same-value segment and **add to the result only those pixels
  of the segment whose current color matches the clicked color**. So a
  segment with both green and red pixels needs two clicks to fully
  include.
* **White / Original photo** (no source line here) — no-op.

If your Ctrl-click lands on the background, the **nearest seedable pixel**
within `round(8 / zoom)` image pixels is taken as the seed. In a
gray-source preset, gray edges (`src<255`) also count as seedable, so the
tolerance ring can snap to a gray edge for the advanced "click on gray"
edit.

#### Shift selection — rect and oriented strip

For bulk-adding multiple segments at once:

* **Shift + drag** draws an axis-aligned rectangle (rubber-band yellow).
* **Shift + click** three times draws an oriented strip — click P1, click
  P2 (defines the axis), then click a third point whose perpendicular
  distance from the axis sets the strip width.
* **Right-click** or **Esc** during the gesture cancels.

On release, a small floating dialog (stays on top, non-modal) asks for:

* **Threshold** — only components whose source gray value is ≤ threshold
  are eligible. 255 means "any component", lower values restrict to
  stronger edges.
* **Spatial** — *Touching* (any pixel of the component inside the
  polygon) or *Inside* (component fully inside).
* **Add color** — *Red* (only-outline-2 pixels), *Green* (only-outline-1),
  or *Gray* (no outline at all). Picking *Gray* while **Edit → Allow
  click on gray** is off pops a confirmation; **Enter/Yes** turns it on
  for the session, **Esc/Cancel** reverts the combo to the previous
  selection.

While the dialog is open the eligible pixels are previewed live: the
ones that **will** be committed at the current settings show in
**blue**, while eligible-component pixels that the **threshold
rejects** show in **dimmed yellow** — same colors as cannyToOutline.
Adjust the spinbox / combos and the overlay updates in place.

OK applies; Cancel discards. The commit is one undo step.

The **8-conn** toolbar checkbox toggles between 4- and 8-connected
flood-fill.

### Saving

`Ctrl+S`. The file format matches cannyToOutline (1-bit PNG, 0 = line).

### Undo / redo

`Ctrl+Z` / `Ctrl+Shift+Z`, one entry per click edit. Undo flips the
pixels touched by that edit back to their previous state; redo replays
it. The stacks are cleared on every file switch.

---

## 4. Common shortcuts (both GUIs)

| Shortcut | Action |
|----------|--------|
| Digit keys `1`..`N` | Pick view preset N |
| **Tab**            | Swap with the previously active preset |
| **Ctrl + click**   | Pick cursor (tolerance ring); nearest seed within radius |
| **Page Up / Down** | Previous / next file |
| **Ctrl + S**       | Save |
| **Ctrl + Z / Shift+Z** | Undo / redo (cannyToOutline only) |
| Mouse wheel        | Zoom at cursor |
| Mouse drag         | Pan |

## 5. File format reference

* **Multi-Canny gray PNG** — 8-bit single channel. `255` = background
  (no edge). Lower values carry edge intensity at increasing Canny
  threshold layers.
* **Outline PNG** — 1-bit PNG. `0` = line, `255` = background. Both GUIs
  invert this on read/write — the in-memory convention is the opposite.

## 6. Time tracking and "Done"

Both GUIs measure how long you actually work on each file and let you
mark a file as done.

* The **Done** button in the top right of the toolbar is red when the
  current file is not done and green with a check mark when it is.
  Click toggles. Setting "Done" is purely informational — it doesn't
  affect editing, saving, or navigation.
* The status bar shows **Time: MM:SS** (or `H:MM:SS` for long sessions),
  updated every second.
* "Activity" that counts: key presses, mouse-button presses, mouse wheel.
  Plain mouse moves do **not** count, so just resting the cursor over the
  window doesn't inflate the timer.
* Pause behavior: the counter optimistically keeps ticking up for up to
  **60 seconds** after your last event. If you come back within that
  window, the whole gap is counted as active time. If 60 s pass without
  any event the displayed counter **snaps back** to the value it had at
  the last event — the over-counted seconds are dropped, never saved —
  and stays there until your next event resumes it.

The **Tools → Project time...** dialog shows the total for the project,
a per-file table (filename / time / done) and an estimated remaining
time based on the average per-done-file divided across the not-yet-done
files. The dialog folds the running gap into the committed totals before
displaying, so what you see in the report matches the status bar.

Both pieces of state are saved per project in
`$XDG_CONFIG_HOME/edgeDetect/<appName>/projects/<projectStem>.<hash>.times.json`:

```json
{
  "projectPath": "/abs/path/to/foo.ocproj",
  "idleSeconds": 60,
  "files": {
    "001.png": { "seconds": 134, "done": true },
    "002.png": { "seconds":  28, "done": false }
  }
}
```

Persisted automatically every minute and on file/project switch and on
close — so a crash costs you at most ~1 minute of timer data.

## 7. Config

Each app writes one JSON file in
`$XDG_CONFIG_HOME/edgeDetect/<appName>/<appName>.json` with:

* `currentProjectPath` — last opened project, reopened on next launch.
* `recentProjects` — MRU list (default cap 20).

Project files (`*.ctoprj`, `*.ocproj`) are plain JSON — safe to hand-edit
if a path needs updating.

Per-project time tracking and Done state live in
`projects/<projectStem>.<hash>.times.json` next to the main app config
(see §6).
