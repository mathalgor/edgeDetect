# Technical Overview — edgeDetect

> Pipeline of tools (CLI + Qt GUIs) that convert raster images into editable
> binary outlines starting from Canny edges, with a human in the loop.

This document describes two Qt6 + OpenCV C++ applications that share a workflow
for turning raster images into clean binary outlines:

* `cannyToOutline` — interactive editor that converts a *multi-Canny gray map*
  into a manually-curated binary outline mask.
* `outlineChooser` — interactive merger that compares two outline variants for
  the same image and lets the user cherry-pick segments into a final result.

A third CLI tool, `toMultiCanny`, is the upstream generator of the multi-Canny
gray maps consumed by `cannyToOutline`. Its source is currently outside this
directory; it produces single-channel PNGs where pixel intensity encodes how
"strong" the edge is across several Canny threshold layers (255 = no edge,
lower values = stronger / more layers detected an edge).

The intended workflow is:

```
input image  →  toMultiCanny  →  multi-Canny gray PNGs
                                        │
                                        ▼
                               cannyToOutline (GUI)
                                        │
                                        ▼
                                 outline PNGs
                                        │  (optionally produce two variants)
                                        ▼
                              outlineChooser (GUI)
                                        │
                                        ▼
                                 final outline PNGs
```

---

## 1. Common conventions

### Build system

Each program is an independent CMake project (no shared library). Required:

* CMake ≥ 3.16
* C++20
* Qt 6 (Widgets)
* OpenCV 4

Out-of-source build:

```bash
cd <program>
cmake -B build
cmake --build build
```

### Output executable

Each `CMakeLists.txt` builds a single executable into `build/<name>`, where
`<name>` matches the source directory.

### Outline PNG format ("file format")

Outlines are stored as single-channel PNGs with the convention:

* **0 = line** (a pixel that is part of the outline)
* **255 = background**

This is the inverse of the in-memory representation used by the widgets
(`255 = line`, `0 = background`). The conversion happens at I/O boundaries via
`cv::bitwise_not`. When PNG-writing outlines we always use:

```cpp
cv::imwrite(path, mat, {cv::IMWRITE_PNG_COMPRESSION, 8,
                        cv::IMWRITE_PNG_BILEVEL, 1});
```

Bilevel = 1 bit per pixel — outlines are binary, so this shrinks files
considerably without losing data.

### Source ("canny gray") PNG format

Single-channel 8-bit grayscale. `255 = white background, no edge`. Values
strictly less than 255 carry edge intensity. The convention `v >= 255 means
ignore` is used everywhere in the algorithms.

### Project files and app config

Both programs use the same pattern:

* **App config** (`AppConfig`): one JSON file per program at
  `$XDG_CONFIG_HOME/edgeDetect/<programName>/<programName>.json`
  (`QStandardPaths::AppConfigLocation`). Stored fields:
  * `currentProjectPath` — last opened project path
  * `recentProjects` — MRU list (max `mruSize`, default 20)
  * `mruSize` — list cap

* **Project files**: per-project JSON, user picks the filename. Distinct
  extensions:
  * `cannyToOutline` → `*.ctoprj`
  * `outlineChooser` → `*.ocproj`

Both Qt apps are registered with:

```cpp
QApplication::setOrganizationName("edgeDetect");
QApplication::setApplicationName("<program>");
```

so they share the org-level config dir.

---

## 2. cannyToOutline

The most complex of the two. Lets a user load a multi-Canny gray PNG and
incrementally build (or correct) an outline mask using several editing
primitives. State is kept per file: opening another file from the project
saves the current outline first (with prompt).

### 2.1 Project file (`*.ctoprj`)

JSON:

```json
{
  "sourceDir":    "/path/to/multi-canny-gray",
  "outputDir":    "/path/to/outline",
  "referenceDir": "/path/to/fit-references",   // optional
  "originalDir":  "/path/to/original-images"   // optional
}
```

* `sourceDir` (required): multi-Canny gray PNGs. The project iterates these
  files in alphabetical order.
* `outputDir` (required): where outline PNGs are written; same filename as
  source.
* `referenceDir` (optional): outline PNGs from another source used by
  *Fit to others* mode (e.g., outlines produced by a model you want to
  reconcile with).
* `originalDir` (optional): original colour/gray photos that the multi-Canny
  came from. Used only for display (an alternate background); files matched by
  stem with any image extension (`.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`,
  `.tiff`, `.webp`).

### 2.2 Source files

| File | Purpose |
|---|---|
| `main.cpp` | Sets org/app name, creates `CannyMainWindow`. |
| `AppConfig.{h,cpp}` | MRU + last project path. |
| `ProjectConfig.{h,cpp}` | The four dirs above; `load(path)` / `save(path)`. |
| `ProjectDialog.{h,cpp}` | Modal dialog for editing the four dirs. |
| `CannyMainWindow.{h,cpp}` | Toolbar, menus, file navigation, side-panels orchestration, undo/redo stack, save/load workflow. |
| `CannyViewWidget.{h,cpp}` | The painted canvas: holds `src_`, `outline_`, builds `display_`, handles all mouse/keyboard editing gestures, contains the algorithms (flood-fill, component analysis, rect-select, oriented-strip, fit-mode, threshold-mode). |

### 2.3 CannyViewWidget — internal representation

The widget holds:

```cpp
cv::Mat src_;          // CV_8UC1   — source gray (multi-Canny)
cv::Mat outline_;      // CV_8UC1   — 255 = line, 0 = bg (INVERSE of file fmt)
QImage  display_;      // current background image (built by rebuildDisplay)
QImage  outlineImage_; // RGBA overlay of the outline
QImage  selImage_;     // yellow Shift-select preview
```

`rebuildDisplay()` produces a grayscale `display_` whose value depends on the
toggles: source on/off, filter on/off, black-mode on/off, hide-done on/off,
lo/hi range, min component size, min component extent.

`rebuildOutlineImage()` produces an RGBA overlay with red pixels for outline
positions; if `filter` AND `filterOutline` are both on, it additionally
respects the same filter rules that hide source pixels.

#### Coordinate handling

* `widgetToImage(QPoint) → QPointF` and `imageToWidget(QPointF) → QPointF`
  use `scale_` (current zoom) and `panOffset_` (pan offset in widget pixels).
* All editing gestures operate in image coordinates (integer pixel positions).

#### Component analysis (`analyzeComponents`)

On `setSource` (and when `conn8_` toggles), it runs a connected-component
analysis over `src_` where two pixels belong to the same component iff they
have the *same gray value* and are 4/8-connected. It produces:

* `labels_` — `CV_32S`, label per pixel (0 = background = `v==255`)
* `labelSize_` — pixel count per label
* `sizeMap_` — `CV_32S`, broadcasts the component size to every pixel
* `extentMap_` — `CV_32F`, max(width, height) of the component bbox per pixel

These power the *Filter* feature (`minSize`, `minExtent`) and the *Touching /
Inside* rect-select modes.

### 2.4 Editing primitives

#### Ctrl + click (per-pixel add/remove)

Click directly on a red outline pixel removes it via `floodSelectSameValue`
(same-value flood-fill seeded from the click). Click on a pixel passing the
filter adds it via `floodSelectSameValue`. If the click is between targets,
the closer of "nearest filter-passing pixel" or "nearest outline pixel" wins.

Options that affect the fill:
* `joinTol_` — widens the value-equality test to `±tol`.
* `allDarker_` — also accepts any pixel strictly darker than the seed.
* `conn8_` — 8-connectivity instead of 4.

#### Shift + drag (axis rect selection)

Drag draws an axis-aligned rectangle (rubber-band). On release a "candidate
dialog" opens, with a threshold spinbox `≤ X → red, > X → keep gray`, plus a
`Touching / Inside` mode combo:

* **Touching** — every same-value component that has *any* pixel inside the
  rectangle.
* **Inside** — every same-value component that lies *entirely* inside the
  rectangle.

Note: there is no "Clip" mode (pixel-wise clip to rectangle) in the dialog,
though the enum used to have it. Component-based modes only.

#### Shift + click (oriented strip)

A three-click ritual: click P1, click P2 (defines the axis), click a width
reference point (perpendicular distance from the axis). The polygon formed by
extending the axis with that width is treated like a rect-select polygon
(same candidate dialog, Touching/Inside).

Right-click or Escape during the strip cancels.

#### Bulk operations

Both *Fit to others* (`F4`) and *Threshold* (`F5`) tools can run a *Bulk*
sweep over every file in `sourceDir`:

* First creates a **snapshot directory** as a sibling of `outputDir`:
  if `outputDir` is `…/train`, the snapshot is `…/train_0` (or `_1`, `_2`…
  if `_0` is taken). All existing outline PNGs in `outputDir` are copied
  there so the user can roll back manually if needed.
* Then iterates source files. For each:
  1. Read `<source>/<name>.png` (gray).
  2. Read `<outputDir>/<name>.png` if it exists, else use an all-white image
     (255 = no outline yet) of the same size as source.
  3. For Fit bulk: read `<referenceDir>/<name>.png` (skip the file if missing,
     unless reference dir itself is unset — that's an error before bulk
     starts). Enter fit mode, commit.
  4. For Threshold bulk: ignore Limit-to-region (forced off); apply current
     add/remove thresholds, commit.
  5. Save outline.

The widget is reused for each file via `setSource` + `setOutlineMask`, with
`QSignalBlocker` to keep the bulk edits out of the undo stack and
`setUpdatesEnabled(false)` to suppress paints. A `QProgressDialog` provides
cancellation and progress.

After the loop the previously-loaded file is reloaded so the user keeps their
position.

### 2.5 Fit to others mode

`enterFitMode(fitRef, conn8, coveragePct, tol, append)`:

1. Snapshot the current `outline_` (`fitOutlineSnap_`).
2. Binarize `fitRef` to `fitRef_` (255 = line internally).
3. Compute `fitDist_` = distance transform from "not a fitRef pixel" → distance
   to nearest fitRef pixel.
4. `recomputeFitSegments`: build `fitLabels_` from same-value 8/4-connected
   components in `src_`, ignoring `v >= 255`.
5. `recomputeFitCoverage`: for each segment, count pixels whose
   `fitDist_[p] ≤ tol`. This is `fitLabelCovered_[L]`.
6. `rebuildFitMask`: `fitMask_` = pixels of segments where coverage exceeds
   `coveragePct%` *and* segment size > `fitMinGreen_`.

`fitGreen_` = `fitMask_ \ fitOutlineSnap_` (would-be additions).
`fitPurple_` = `fitOutlineSnap_ \ dilate(fitMask_, tol)` (would-be removals,
only when not in `append` mode).

These overlay on the view as semi-transparent green/purple.

`commitFitMode`:
* `outline_ ∪= fitGreen_` — emits `outlineBulkOp(green, /*add*/true)`
* `outline_ \= fitPurple_` — emits `outlineBulkOp(purple, /*add*/false)`

These are two separate undo entries.

### 2.6 Threshold add/remove mode

A side panel (not a dialog — see UI section) controls two pixel-wise rules:

* **add**: if checked, every pixel with `gray ≤ addVal` that is *not yet* in
  outline → add.
* **remove**: if checked, every pixel with `gray ≥ removeVal` that *is* in
  outline → remove.

Constraint enforced in the spinbox UI: `remove > add`.

Two preview modes via the "final preview" checkbox:

* **off (raw overlay)** — paints would-be-added in orange and would-be-removed
  in dark yellow on top of the regular view; no filters applied to
  candidates.
* **on (final preview)** — builds a synthetic outline
  `(outline ∪ addMask) \ removeMask`, then renders it through the regular
  `rebuildDisplay`/`rebuildOutlineImage` pipeline, so blackmode / filter /
  filterOutline / hide-done all take effect. Candidate selection itself
  respects filters here: add candidates also require `filter` AND
  `filterOutline`; remove candidates require `filter` only (the existing
  outline already accounted for outline filtering).

#### Limit-to-region

Optional restriction. When the side-panel checkbox is on:

* The cursor switches to a cross.
* `Shift+drag` draws an axis-aligned rectangle region.
* `Shift+click ×3` draws an oriented strip (same gesture as the strip-select
  above, but stored in a separate state to avoid collisions).
* Region is filled (`cv::fillPoly` or `cv::rectangle`) into
  `thresholdRegionMask_`.
* A Touching/Inside combo controls how `thresholdEligibleMask_` is derived
  from the region using the global `labels_`:
  * **Touching** — any same-value component with at least one pixel in the
    region.
  * **Inside** — only components fully contained in the region.
* The threshold candidates are then masked by `thresholdEligibleMask_`.
* Before the region is drawn, all counts are zero (no work happens).

Bulk-threshold forces this off (per-image regions don't make sense for the
whole project).

#### Step-snap spinboxes

The add/remove spinboxes are `CountSnapSpinBox` instances: stepping
(arrow / wheel) jumps to the *next* value that changes the affected pixel
count, using histograms (`thrAddHist_`, `thrRmHist_`) precomputed in
`recomputeThresholdMasks`. Typed values still accept anything.

### 2.7 Side panels (Wayland note)

Originally both *Fit to others* and *Threshold add/remove* were floating
dialogs. Under Wayland, an application cannot set the position of its own
toplevel window (the compositor decides), so reopening a closed dialog always
recentred it. The current implementation hosts both tools as `QWidget`
panels placed in a horizontal `QSplitter` whose left side is `view_`:

```
[ view ] [ optional panel ]
   ↑          ↑
   stretch 1  stretch 0, lazy-created on first F4/F5
```

* `F4` toggles the Fit panel; `F5` toggles the Threshold panel.
* Showing one **auto-hides** the other (and calls the corresponding
  `exit*Mode` on the view).
* Panel widths are user-adjustable via the splitter handle and persist for
  the lifetime of the window.
* `panel->setMinimumWidth(140)` so panels can be dragged narrow.

The panels are persistent; closing them keeps their widget tree alive so
their settings (spinbox values, checkboxes) survive between toggles even on
Cancel.

### 2.8 Undo / redo

`Op` struct per edit; the stack lives in `CannyMainWindow::undoStack_` /
`redoStack_`. Each Ctrl-click adds one `Op{ isBulk=false, seed, add, … }`;
each bulk op (rect-select commit, fit commit, threshold commit, fill from
strip) adds one `Op{ isBulk=true, mask, add }`. Undo applies the inverse via
`view_->applyOp` / `view_->applyBulkOp`; these are *signal-less* equivalents
of the editing primitives, so undoing doesn't push onto redo via signals.

### 2.9 Other view options

* `Source`, `Outline` checkboxes — show / hide source layer or outline layer.
* `Filter` — `lo..hi` range plus `min size` / `min extent` filters; with
  filter on, source pixels outside the rules render as white in `display_`,
  and *Filter outline* checkbox additionally hides outline pixels that don't
  match the filter.
* `Black mode` — pixels that pass become solid black instead of preserving
  their gray value (good for high-contrast review).
* `Hide done` — temporarily hide outline pixels so you can see what's
  underneath.
* `Original` — replace `display_` with `originalImage_` (loaded from
  `originalDir` if available).

### 2.10 Save flow

* `currentPath_` holds the path of the currently-loaded source.
* `defaultOutlinePath()` returns the outline path inside `outputDir` with the
  same filename.
* `doSave()` writes `view_->saveOutline(defaultOutlinePath())`.
* `maybeSave()` prompts (Save / Discard / Cancel) when `dirty_` is set.
  Invoked from `closeEvent`, file-switch (prev/next, spinbox, first-empty
  shortcut), and project change.

`saveOutline` writes 1-bit PNG (see Outline PNG format above).

---

## 3. outlineChooser

Much simpler app: no algorithms beyond same-value flood-fill. Compares two
outline candidates for the same set of source images and lets the user pick
which segments end up in the merged output.

### 3.1 Project file (`*.ocproj`)

```json
{
  "sourceDir":    "/path/to/multi-canny-gray",
  "outputDir":    "/path/to/final-outline",
  "outlines1Dir": "/path/to/outline-variant-A",
  "outlines2Dir": "/path/to/outline-variant-B",
  "originalDir":  "/path/to/original-images"   // optional
}
```

All three of `sourceDir`, `outlines1Dir`, `outlines2Dir` are required for
`isValid()`. `outputDir` is allowed to be empty/non-existent initially —
it's created on first save.

### 3.2 Source files

| File | Purpose |
|---|---|
| `main.cpp` | Sets org/app name, creates `OcMainWindow`. |
| `AppConfig.{h,cpp}` | Same pattern as cannyToOutline. |
| `ProjectConfig.{h,cpp}` | Five dirs above. |
| `ProjectDialog.{h,cpp}` | Modal dialog editing the five dirs. |
| `OcMainWindow.{h,cpp}` | Project plumbing, toolbar, file nav, save flow. |
| `OcViewWidget.{h,cpp}` | The colored visualization + click logic. |

### 3.3 Pixel state and color encoding

Per pixel we track:

* `in1` ← outline1 has this pixel as a line (post bitwise-not, so `255` =
  line internally; same convention as cannyToOutline)
* `in2` ← outline2 has it
* `inOut` ← the merged result currently has it

These three booleans yield one of 5 colours, drawn directly to `vis_`:

| inOut | in1 | in2 | colour | meaning |
|---|---|---|---|---|
| 1 | * | * | **black** | included in merged result |
| 0 | 1 | 1 | **yellow** (255,220,0) | was in both, currently *not* in result |
| 0 | 1 | 0 | **green** (0,180,0) | only in outline1 |
| 0 | 0 | 1 | **red** (220,0,0) | only in outline2 |
| 0 | 0 | 0 | gray of `src` | no outline at all (source rendered as background) |

At load time the merged output is initialised to `in1 AND in2` so every
common pixel starts black. User work consists in upgrading green/red/yellow
to black (selecting), or downgrading black back to its origin colour
(deselecting).

### 3.4 Click semantics

Clicking is the only edit gesture; mouse drag pans. The click is consumed
only if cursor didn't move (>3 px = treated as pan).

Lookup `colorAt(x, y)`:

* **white (0)** — no-op (clicking on actual background does nothing).
* **green / red / yellow (1 / 2 / 3)** — flood-fill the same-value gray
  segment containing `(x, y)` (8/4-connected per `conn8_` toggle), then add
  to `out_` *only* the pixels of that segment whose current colour equals
  the clicked colour. The rest of the segment is untouched.

  This means a segment that contains both green and red pixels (yes, it
  happens) will require two clicks to fully include — once on green, once
  on red.

* **black (4)** — flood-fill the entire same-value segment, regardless of
  colour, and remove from `out_` every pixel of that segment currently in
  `out_`. Their colour reverts to origin (green/red/yellow per `in1`/`in2`).

`updateVisualizationAt(pts)` patches just the modified pixels in `vis_`
instead of rebuilding from scratch — important for responsiveness on
large segments.

### 3.5 Save

`outputFileFmt()` returns `cv::bitwise_not(out_)` — back to the standard
`0 = line` convention. Written via `cv::imwrite` with the same
compression/bilevel options as cannyToOutline.

The save prompt and dirty tracking mirror cannyToOutline (`maybeSave`,
`doSave`, dirty flag on the view that emits `dirtyChanged`).

### 3.6 Things not implemented yet

* No undo/redo.
* No bulk-merge tool (each file is curated by hand).
* No "Original" display toggle (would mirror cannyToOutline's pattern).
* No keyboard shortcuts beyond PageUp/PageDown for prev/next file.

These are deliberate omissions, not blockers.

---

## 4. Where things commonly trip up

* **Outline storage convention is inverted between file and memory.** All
  loaders do `bitwise_not` immediately after `imread`; all savers do it
  again just before `imwrite`. Stay consistent — easy to forget when
  prototyping.
* **The widget owns mutable algorithm state.** Bulk operations reuse a single
  `CannyViewWidget` instance: `setSource(...)`; `setOutlineMask(...)`;
  `enterXxxMode(...)`; `commitXxxMode()`. This is convenient but means
  any signal emitted during these calls needs blocking
  (`QSignalBlocker(view_)`) and any paint needs suppressing
  (`setUpdatesEnabled(false)`), otherwise hundreds of files balloon the
  undo stack and freeze the UI.
* **`v >= 255` is "background".** Both component analysis and most
  threshold/fit paths skip pixels with `v == 255`. If an outline pixel ends
  up at a position where `src == 255` (it can, if an outline file was
  produced externally and doesn't align with the multi-Canny), the
  threshold tool with `remove = 255` is the easiest way to clean it.
* **Wayland is hostile to floating dialogs.** Anything user-positioned must
  live inside the main window. Don't add new `QDialog`-with-WindowStaysOnTop
  popups without considering the Wayland positioning limitation.
* **`fileList_` is alphabetically sorted PNGs from `sourceDir`.** The whole
  project navigation (prev/next/spinbox/first-empty) operates on indices
  into this list. Any other directory (output, references, outlines1/2,
  original) is matched **by filename equality** with the source entry.
* **`*.ctoprj` and `*.ocproj` are plain JSON.** Safe to hand-edit if a path
  needs updating; the app re-reads on next open.

---

## 5. Suggested first steps for someone new

1. Build both projects.
2. Pick a directory of multi-Canny gray PNGs (or generate one with
   `toMultiCanny`).
3. Launch `cannyToOutline`, File → New project, point Source at the gray
   PNGs and Output at an empty dir, save as `*.ctoprj`. Walk through a
   couple of files with Ctrl-click, Shift+drag rect-select, F5 threshold.
4. Once you have two slightly different outline directories for the same
   set of sources, launch `outlineChooser` with both as outlines1 / outlines2
   and try the click semantics on a few segments.

Reading order inside this codebase:

1. `CannyViewWidget.h` — the data model and editing API.
2. `CannyViewWidget.cpp` (mouse events first, then paintEvent, then the
   per-mode `recompute*` / `rebuild*` methods).
3. `CannyMainWindow.cpp` — see how UI is wired up, and how bulk operations
   reuse the view.
4. `OcViewWidget.{h,cpp}` for the simpler model in `outlineChooser`.
