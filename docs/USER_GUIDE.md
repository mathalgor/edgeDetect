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
* `Ctrl+E` — jump to the first source without a saved outline.

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
| 1   | **Standard** *(default)*   | White bg; black = in result, red = only in outline 2, green = only in outline 1, dark yellow = common but **not** in result. |
| 2   | Original + result red      | Original photo, every pixel currently in your result drawn as a semi-transparent red overlay. |
| 3   | Original + diff            | Original photo with diff colors over it (green/red/yellow); ignores the result. |
| 4   | Gray source + diff         | Same diff colors over the multi-Canny gray map. |
| 5   | Result only                | Just your result, black on white. |
| 6   | Result + outline 1 (green) | Result in black + the *non-accepted* outline-1 pixels in green (so you can see in isolation what only outline 1 would still add). |
| 7   | Result + outline 2 (red)   | Same idea for outline 2. |

Same shortcuts as cannyToOutline: digit keys pick a preset, **Tab** swaps
with the previous one.

Presets 6 and 7 are handy when one of the two candidate outlines has
many more (often noisier) pixels than the other but the smaller one
contributes a few useful additions — flip between them and the Standard
view to decide which extras to bring in.

### Editing

There's just one gesture: **click**.

* Mouse drag pans.
* A click that doesn't move the cursor more than 3 px is treated as a real
  click.

Lookup depends on the color under the click:

* **Black** (a pixel in your result) — flood the same-value segment and
  **remove every pixel of that segment from the result**.
* **Green / Red / Yellow** (only-O1, only-O2, common-not-in-result) —
  flood the same-value segment and **add to the result only those pixels
  of the segment whose current color matches the clicked color**. So a
  segment with both green and red pixels needs two clicks to fully
  include.
* **White / Original photo** (no source line here) — no-op.

Hold **Ctrl** and the cursor switches to the pick cursor (tolerance ring).
If your click lands on the background, the **nearest non-white pixel**
within `round(8 / zoom)` image pixels is taken as the seed.

The **8-conn** toolbar checkbox toggles between 4- and 8-connected
flood-fill.

### Saving

`Ctrl+S`. The file format matches cannyToOutline (1-bit PNG, 0 = line).

There is currently no undo/redo and no bulk-merge tool — outlineChooser is
designed for hand-curation of a few hard cases.

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
* If you don't do anything for **60 seconds** the timer effectively
  pauses — the gap up to that point still counts (it's a tolerance
  window), but seconds beyond it are skipped until you act again.

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
