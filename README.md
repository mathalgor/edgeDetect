# edgeDetect

Pipeline of tools (CLI + Qt GUIs) that convert raster images into editable
binary outlines starting from Canny edges, with a human in the loop.

```
photos  →  toMultiCanny  →  cannyToOutline  →  outlines
                                      ↓
                                outlineChooser
                                      ↓
                                final outlines
```

* **toMultiCanny** — CLI that runs a precomputed Canny over a folder of
  images with 256 thresholds and writes one multi-Canny gray PNG per image.
* **cannyToOutline** — Qt GUI for hand-curating a binary outline mask out
  of a multi-Canny gray map.
* **outlineChooser** — Qt GUI for merging two outline variants of the same
  image into one final result.

A small `common/` static library holds shared pieces (per-app config, pick
cursor, original-image loader, view-preset struct).

## Build

CMake ≥ 3.21, C++20, Qt 6 (Widgets), OpenCV 4. From the repo root:

```bash
cmake -B build
cmake --build build -j
```

Executables:
`build/toMultiCanny/toMultiCanny`,
`build/cannyToOutline/cannyToOutline`,
`build/outlineChooser/outlineChooser`.

## Documentation

* [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md) — workflow, GUI shortcuts
  (digit keys pick a view preset, Tab swaps with the previous one),
  editing gestures, file formats.
* [`docs/TECHNICAL.md`](docs/TECHNICAL.md) — architecture, data model,
  rendering, the cell-index preset system, undo/redo, common pitfalls.

## License

See [LICENSE](LICENSE).
