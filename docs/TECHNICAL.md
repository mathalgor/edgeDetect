# Technical Overview — edgeDetect

> Pipeline of tools (CLI + Qt GUIs) that convert raster images into editable
> binary outlines starting from Canny edges, with a human in the loop.

This document describes three C++ programs that share a workflow for turning
raster images into clean binary outlines:

* `toMultiCanny` — CLI that runs a precomputed Canny over a folder of images
  with 256 thresholds and writes one "multi-Canny gray" PNG per image.
* `cannyToOutline` — Qt 6 GUI that converts a multi-Canny gray PNG into a
  manually-curated binary outline mask.
* `outlineChooser` — Qt 6 GUI that compares two outline variants for the
  same image and lets the user cherry-pick segments into a merged result.

A small static library `common/` holds code shared between the two GUI apps
(app config, pick cursor, original-image loader, view-preset struct).

Intended workflow:

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

## 1. Build system & layout

```
edgeDetect/
├── CMakeLists.txt          # top-level: find_package(Qt6, OpenCV) + subdirs
├── common/                 # static lib edgeDetectCommon (Qt6::Core+Gui)
│   ├── AppConfig.{h,cpp}       # MRU + last-project JSON (per-app filename)
│   ├── CursorUtils.{h,cpp}     # makePickCursor(radiusPx)
│   ├── OriginalLoader.{h,cpp}  # stem-based image lookup for "originalDir"
│   └── ViewPreset.h            # struct used by outlineChooser
├── toMultiCanny/           # CLI; class Canny (prepare/apply split, 256x reuse)
├── cannyToOutline/         # GUI
└── outlineChooser/         # GUI
```

Requirements: CMake ≥ 3.21, C++20, Qt 6 (Widgets), OpenCV 4.

Out-of-source build from the repo root:

```bash
cmake -B build
cmake --build build -j
```

Executables land in `build/<subdir>/<exe>`.

### Output / common code conventions

* Each GUI app sets `QApplication::setOrganizationName("edgeDetect")` and
  `setApplicationName("<app>")`. `common::AppConfig::configPath()` therefore
  resolves to `$XDG_CONFIG_HOME/edgeDetect/<app>/<app>.json` per app, even
  though the implementation lives in `common`.
* The shared pick cursor (white arrow with a dotted tolerance ring) is built
  by `common::makePickCursor(radiusPx)`. Both view widgets use it when Ctrl
  is held.
* `common::loadOriginalForStem(dir, stem)` is the canonical way both apps
  look up an "original photo" sibling for the current source filename:
  try `<dir>/<stem>.<ext>` for the common extensions, then any `stem.*`.

### Outline PNG format ("file format")

Outlines are single-channel PNGs:
* **0 = line** (a pixel that is part of the outline)
* **255 = background**

This is the inverse of the in-memory representation used by both widgets
(`255 = line`, `0 = background`). The conversion happens at I/O boundaries
via `cv::bitwise_not`. PNG-writing uses 1-bit-per-pixel mode:

```cpp
cv::imwrite(path, mat, {cv::IMWRITE_PNG_COMPRESSION, 8,
                        cv::IMWRITE_PNG_BILEVEL, 1});
```

### Source ("canny gray") PNG format

Single-channel 8-bit grayscale. `255 = white background, no edge`. Lower
values carry edge intensity. The convention `v >= 255 means ignore` is used
everywhere in the algorithms.

### Project files

Per-project JSON, user picks the filename. Distinct extensions:
* `cannyToOutline` → `*.ctoprj`
* `outlineChooser` → `*.ocproj`

---

## 2. toMultiCanny

CLI: `toMultiCanny <in_dir> <out_dir> [--invert]`. For each image: gray ←
`COLOR_BGR2GRAY`, Gaussian k=3, then a 256-level edge map produced by
running Canny 256 times with `(low, high) = (i, 2.5·i)` and stamping each
result into a single gray PNG via a log(pct) → value mapping.

### `Canny` class (renamed from MyCanny)

Not `cv::Canny`. Split into:

* `prepare(src8u)` — Sobel ksize=3, L2² magnitude, NMS without atan2 (compares
  `ay*1000` vs `ax*414/2414` for the 22.5°/67.5° boundaries). Result is a
  CV_32S magnitude-squared map with non-max pixels zeroed.
* `apply(low, high, dst)` — hysteresis on the magnitude map; reuses
  `mapBuf_` (status map) and `stackBuf_` (DFS stack) between calls. Returns
  edge count.
* `applyThresholdOnly(high, dst)` — drops the maybe/edge two-level logic for
  variants that don't want hysteresis.
* `buildGrayMapNms(dst, valueFunc)` — single-pass alternative that derives
  the per-pixel value from the magnitude histogram, no 256-iteration loop.
  Currently not the default path (the CLI uses 256× `apply`), but available.

The split is what makes the 256-iteration `buildGrayMap` cheap in practice:
Sobel + NMS run once per image; hysteresis (the hot loop) runs 256 times
with reused buffers.

---

## 3. cannyToOutline

The most complex of the three. Loads a multi-Canny gray PNG and lets the
user incrementally build (or correct) an outline mask using several editing
primitives. State is kept per file: opening another file from the project
saves the current outline first (with prompt).

### 3.1 Project file (`*.ctoprj`)

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
* `referenceDir` (optional): outline PNGs used by *Fit to others* mode.
* `originalDir` (optional): original color/gray photos. Looked up by stem
  through `common::loadOriginalForStem` and used as an alternate background
  in the View presets that need it.

### 3.2 Source files

| File | Purpose |
|---|---|
| `main.cpp` | Sets org/app name, creates `CannyMainWindow`. |
| `ProjectConfig.{h,cpp}` | The four dirs above; `load(path)` / `save(path)`. |
| `ProjectDialog.{h,cpp}` | Modal dialog for editing the four dirs. |
| `CannyMainWindow.{h,cpp}` | Toolbar, menus, file navigation, side-panels orchestration, undo/redo stack, view-mode combobox, save/load workflow. |
| `CannyViewWidget.{h,cpp}` | The painted canvas: holds `src_`, `outline_`, builds `display_`, handles all mouse/keyboard editing gestures, contains the algorithms (flood-fill, component analysis, rect-select, oriented-strip, fit-mode, threshold-mode). |

### 3.3 CannyViewWidget — internal representation

```cpp
cv::Mat src_;          // CV_8UC1   — source gray (multi-Canny)
cv::Mat outline_;      // CV_8UC1   — 255 = line, 0 = bg (INVERSE of file fmt)
QImage  display_;      // current background image (built by rebuildDisplay)
QImage  outlineImage_; // RGBA overlay of the outline
QImage  selImage_;     // yellow Shift-select preview
```

`rebuildDisplay()` produces the grayscale `display_` from the current view
flags (`showSource_`, `showOutline_`, `blackMode_`, `hideDone_`, lo/hi
range, min component size/extent).

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

### 3.4 Editing primitives

Editing only works when the active view mode includes both the gray source
AND the outline layer (presets "Gray source + red outline" and "Black source
+ red outline" — see §3.7). In all other modes Ctrl-click and Shift gestures
fall through to panning, and any in-progress rect/strip is cancelled.

#### Ctrl + click (per-pixel add/remove)

Click directly on a red outline pixel removes it via `floodSelectSameValue`
(same-value flood-fill seeded from the click). Click on a pixel passing the
filter adds it via `floodSelectSameValue`. If the click is between targets,
the closer of "nearest filter-passing pixel" or "nearest outline pixel" wins
(spatial search radius = `round(8 / scale)` image pixels). The search always
returns the **nearest** matching pixel — there is no "prefer darkest" pass.

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

#### Shift + click (oriented strip)

Three-click ritual: click P1, click P2 (defines the axis), click a width
reference point (perpendicular distance from the axis). The polygon formed
by extending the axis with that width is treated like a rect-select polygon
(same candidate dialog, Touching/Inside).

Right-click or Escape during the strip cancels.

#### Bulk operations

Both *Fit to others* (`F4`) and *Threshold* (`F5`) tools can run a *Bulk*
sweep over every file in `sourceDir`. They first snapshot existing outputs
into a sibling `<outputDir>_N` directory, then iterate source files, apply
the operation, and save. The widget is reused via `setSource` +
`setOutlineMask`, with `QSignalBlocker(view_)` to keep bulk edits out of
the undo stack and `setUpdatesEnabled(false)` to suppress paints. A
`QProgressDialog` provides cancellation.

### 3.5 Fit to others mode

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

`commitFitMode`:
* `outline_ ∪= fitGreen_` — emits `outlineBulkOp(green, add=true)`.
* `outline_ \= fitPurple_` — emits `outlineBulkOp(purple, add=false)`.

Two separate undo entries.

### 3.6 Threshold add/remove mode

A side panel controls two pixel-wise rules:

* **add**: every pixel with `gray ≤ addVal` not yet in outline → add.
* **remove**: every pixel with `gray ≥ removeVal` already in outline → remove.

Constraint: `remove > add`. Two preview modes via the "final preview"
checkbox (raw overlay vs. composited final outline through the regular
display pipeline).

Limit-to-region narrows candidates with a `thresholdEligibleMask_` derived
from a Shift-rect or Shift-strip drawn into `thresholdRegionMask_` with the
same Touching/Inside semantics as rect-select.

Bulk-threshold forces Limit-to-region off (per-image regions don't make
sense project-wide).

The add/remove spinboxes are `CountSnapSpinBox` instances that step
(arrow/wheel) to the next value that changes the affected pixel count.

### 3.7 View modes (presets)

A single `View` combobox in the toolbar replaces the older five checkboxes
(`original`, `source`, `outline`, `black`, `hide done`). Each preset maps
to a (showOriginal, showSource, showOutline, blackMode, hideDone) tuple
applied through the existing `view_->setShow…()` setters — the renderer
itself is unchanged.

| # | Name | original | source | outline | black | hideDone |
|---|---|---|---|---|---|---|
| 1 | Original                       | ✓ |   |   |   |   |
| 2 | Original + outline red         | ✓ |   | ✓ |   |   |
| 3 | Outline black                  |   |   |   |   |   |
| 4 | Gray source                    |   | ✓ |   |   |   |
| 5 | Source (all black)             |   | ✓ |   | ✓ |   |
| 6 | Gray source + red outline ★    |   | ✓ | ✓ |   |   |
| 7 | Black source + red outline ★   |   | ✓ | ✓ | ✓ |   |

★ = editing enabled. The two preset modes that show *both* the gray source
and the outline are the only ones where Ctrl-click and Shift gestures
operate; the other five are display-only.

Preset #3 (Outline black) is produced by `rebuildDisplay` itself: when both
`showSource_` and `showOutline_` are off, it paints outline pixels as black
on a white background.

Shortcut: digit keys **1..7** select a preset; **Tab** swaps with the
previously active preset. When no original photo is loaded, presets 1 and 2
are disabled and the current mode falls back to "Gray source".

### 3.8 Side panels (Wayland note)

Both *Fit to others* (F4) and *Threshold add/remove* (F5) are hosted as
`QWidget` panels placed in a horizontal `QSplitter` whose left side is
`view_`. Reason: under Wayland an application cannot set the position of
its own toplevel windows, so the previous floating-dialog design recentred
on every reopen.

* `F4` toggles the Fit panel; `F5` toggles the Threshold panel.
* Showing one auto-hides the other (and calls the corresponding `exit*Mode`
  on the view).
* Panel widths persist for the lifetime of the window.

### 3.9 Undo / redo

`Op` struct per edit; the stack lives in `CannyMainWindow::undoStack_` /
`redoStack_`. Each Ctrl-click adds one `Op{ isBulk=false, seed, add, … }`;
each bulk op (rect-select commit, fit commit, threshold commit, fill from
strip) adds one `Op{ isBulk=true, mask, add }`. Undo applies the inverse via
`view_->applyOp` / `view_->applyBulkOp`; these are *signal-less* equivalents
of the editing primitives so undoing doesn't push onto redo via signals.

### 3.10 Save flow

* `currentPath_` holds the path of the currently-loaded source.
* `defaultOutlinePath()` returns the outline path inside `outputDir` with
  the same filename.
* `doSave()` writes `view_->saveOutline(defaultOutlinePath())`.
* `maybeSave()` prompts (Save / Discard / Cancel) when `dirty_` is set.
  Invoked from `closeEvent`, file-switch (prev/next, spinbox, first-empty
  shortcut), and project change.

---

## 4. outlineChooser

Compares two outline candidates for the same set of source images and lets
the user pick which segments end up in the merged output.

### 4.1 Project file (`*.ocproj`)

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
`isValid()`. `outputDir` may be empty/non-existent initially — it's created
on first save.

### 4.2 Source files

| File | Purpose |
|---|---|
| `main.cpp` | Sets org/app name, creates `OcMainWindow`. |
| `ProjectConfig.{h,cpp}` | Five dirs above. |
| `ProjectDialog.{h,cpp}` | Modal dialog editing the five dirs. |
| `OcMainWindow.{h,cpp}` | Project plumbing, toolbar (with View combobox), file nav, save flow, original loading. |
| `OcViewWidget.{h,cpp}` | Preset-driven renderer, click logic, Ctrl tolerance picker. |

### 4.3 Pixel state and cell index

Per pixel we track three bits:

* `in1` — outline1 has this pixel as a line (internal: 255 = line; same
  convention as cannyToOutline)
* `in2` — outline2 has it
* `inOut` — the merged result currently has it

The triple is packed into a **cell index** `(in1<<2)|(in2<<1)|inOut` (0..7).
The two view widgets refer to it as `cellAt(x, y)`. The eight cells and the
typical Standard-preset colors:

| cell | in1 | in2 | out | Standard preset |
|---|---|---|---|---|
| 0 = 000 | 0 | 0 | 0 | (background)
| 1 = 001 | 0 | 0 | 1 | black (rare; manually added pixel)
| 2 = 010 | 0 | 1 | 0 | red (only outline2)
| 3 = 011 | 0 | 1 | 1 | black
| 4 = 100 | 1 | 0 | 0 | green (only outline1)
| 5 = 101 | 1 | 0 | 1 | black
| 6 = 110 | 1 | 1 | 0 | dark yellow (common, deselected)
| 7 = 111 | 1 | 1 | 1 | black

At load time the result mask is initialised to `in1 AND in2` **only if no
saved result PNG exists**. When `<outputDir>/<name>.png` is present it is
loaded and used as `out_` — this is the fix for the original bug where
reopening a project always reset progress to the intersection.

### 4.4 View presets

A view preset is `(Background, QColor cells[8])`. Cells with alpha == 0
are transparent and let the background show through. The struct lives in
`common::ViewPreset` (header-only).

Backgrounds:

* `White` — solid white.
* `GraySource` — multi-Canny `src_` as grayscale.
* `Original` — color photo from `originalDir`, loaded via
  `common::loadOriginalForStem` and resized to `src_` size. Falls back to
  `GraySource` if no original was loaded.
* `Black` — solid black (reserved; currently no preset uses it).

Built-in presets (hard-coded in `OcViewWidget::buildDefaultPresets()`):

| # | Name | Background | Notes |
|---|---|---|---|
| 1 | Standard                        | White       | Current colors (red/green/dark-yellow/black). |
| 2 | Original + result red           | Original    | Every "out" cell rendered as semi-transparent red over the photo. |
| 3 | Original + diff                 | Original    | Diff colors over the photo, `out` ignored. |
| 4 | Gray source + diff              | GraySource  | Same diff colors over `src_`. |
| 5 | Result only                     | White       | Black for any cell with `out=1`; everything else transparent. |

Switching presets:

* `View` combobox in the toolbar.
* Digit keys **1..N** select a preset.
* **Tab** swaps with the previously active preset.

The `presetChanged(int)` signal keeps view and combobox in sync.

### 4.5 Rendering pipeline

`composePixel(x, y)` is the single source of truth: pick the background
RGB for the active preset's background, then alpha-blend the foreground
QColor from `preset.cells[cellAt(x,y)]`, returning a `QRgb`.

`rebuildVisualization()` runs `composePixel` over every pixel into a fresh
`QImage(Format_ARGB32)`. `updateVisualizationAt(pts)` calls it just on the
pixels touched by a flood-fill — both paths therefore stay consistent.

(Note: `Format_ARGB32` matches `QRgb`'s memory layout natively;
`Format_RGBA8888` would swap red and blue if written through
`reinterpret_cast<QRgb*>(scanLine)`.)

### 4.6 Click semantics

Mouse drag pans. The click is only consumed when the cursor didn't move
(>3 px = treated as pan).

* When Ctrl is **held** the cursor changes to the pick cursor (tolerance
  ring). On click, if the exact pixel is white, search a radius of
  `round(8 / scale)` image pixels for the **nearest non-white pixel** (any
  pixel with `in1 || in2`) and operate on its segment.
* When Ctrl is **not held** the click is exact — clicking white does
  nothing.

After the seed is determined, `colorAt(x, y)` decides what to do:

* white (0) — no-op.
* green / red / yellow (1 / 2 / 3) — flood-fill the same-value gray segment
  containing the seed (8/4-connected per `conn8_`), then add to `out_` only
  the pixels of that segment whose current color equals the clicked color.
* black (4) — flood-fill the whole same-value segment regardless of color
  and remove from `out_` every pixel of that segment currently in `out_`.

`updateVisualizationAt(pts)` patches just the modified pixels in `vis_`.

### 4.7 Save

`outputFileFmt()` returns `cv::bitwise_not(out_)` — back to the standard
`0 = line` convention. Written via `cv::imwrite` with the same
compression/bilevel options as cannyToOutline.

Save prompt and dirty tracking mirror cannyToOutline (`maybeSave`,
`doSave`, dirty flag emitting `dirtyChanged`).

### 4.8 Not implemented

* No undo/redo.
* No bulk-merge tool (each file is curated by hand).

---

## 5. Things that commonly trip up

* **Outline storage is inverted between file and memory.** All loaders do
  `cv::bitwise_not` immediately after `imread`; all savers do it again just
  before `imwrite`. Easy to forget when prototyping.
* **The cannyToOutline widget owns mutable algorithm state.** Bulk
  operations reuse a single `CannyViewWidget` instance: `setSource(...)`;
  `setOutlineMask(...)`; `enterXxxMode(...)`; `commitXxxMode()`. Any signal
  emitted during these calls needs blocking (`QSignalBlocker(view_)`) and
  any paint needs suppressing (`setUpdatesEnabled(false)`) — otherwise
  hundreds of files balloon the undo stack and freeze the UI.
* **`v >= 255` is "background".** Both component analysis and most
  threshold/fit paths skip pixels with `v == 255`. If an outline pixel
  ends up at a position where `src == 255` (which can happen if an outline
  file was produced externally and doesn't align with the multi-Canny),
  the threshold tool with `remove = 255` is the easiest way to clean it.
* **Wayland is hostile to floating dialogs.** Anything user-positioned
  must live inside the main window. Don't add new
  `QDialog`-with-WindowStaysOnTop popups without considering the Wayland
  positioning limitation.
* **`fileList_` is alphabetically sorted PNGs from `sourceDir`.** All
  project navigation (prev/next/spinbox/first-empty) operates on indices
  into this list. Other directories (output, references, outlines1/2,
  original) are matched **by filename equality** with the source entry —
  except the original, which is matched **by stem** through
  `common::loadOriginalForStem`.
* **`*.ctoprj` and `*.ocproj` are plain JSON.** Safe to hand-edit if a
  path needs updating; the app re-reads on next open.
* **Edits in cannyToOutline are gated.** If Ctrl-click "does nothing" or
  Shift-drag pans, check the View combobox: editing only works in modes 6
  and 7 (the two that show both gray source and outline).
* **`QImage::Format_ARGB32` vs `Format_RGBA8888`.** When writing `QRgb`
  through `scanLine`, use ARGB32 — the byte layout matches. RGBA8888 stores
  bytes literally as R,G,B,A and will swap red and blue if fed `QRgb` via
  reinterpret_cast.

---

## 6. Suggested first steps for someone new

1. `cmake -B build && cmake --build build -j` from the repo root.
2. Pick a directory of multi-Canny gray PNGs (or generate one with
   `toMultiCanny`).
3. Launch `cannyToOutline`, File → New project, point Source at the gray
   PNGs and Output at an empty dir, save as `*.ctoprj`. Walk through a
   couple of files with Ctrl-click, Shift+drag rect-select, F5 threshold.
   Try the View combobox / digit keys / Tab to flip between display modes.
4. Once you have two slightly different outline directories for the same
   set of sources, launch `outlineChooser` with both as outlines1 /
   outlines2 and try the click semantics on a few segments. Use 1..5 / Tab
   to compare views.

Reading order inside this codebase:

1. `common/ViewPreset.h` — the cell-index/preset model used by outlineChooser.
2. `outlineChooser/OcViewWidget.{h,cpp}` — the simpler renderer.
3. `cannyToOutline/CannyViewWidget.{h,cpp}` — the data model and editing API
   (mouse events first, then paintEvent, then per-mode `recompute*` /
   `rebuild*` methods).
4. `cannyToOutline/CannyMainWindow.cpp` — see how UI is wired up, how bulk
   operations reuse the view, and how the View combobox maps to the five
   underlying display flags.
