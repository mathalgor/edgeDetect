# Pin fit algorithm

## Transformation model

Full model (up to 12 parameters):

```
oxc = dX + aX·gxc + bXY·gyc + cX·gxc² + eXY·gxc·gyc
oyc = dY + bYX·gxc + aY·gyc + cY·gyc² + eYX·gxc·gyc
```

where

```
gxc = grayX    - grayW/2     gyc = grayY    - grayH/2
oxc = outlineX - outlineW/2  oyc = outlineY - outlineH/2

aX  = scaleX/grayW   aY  = scaleY/grayH        (diagonal linear scale)
cX  = quadX          cY  = quadY               (diagonal quadratic scale)
bXY = rotXY          bYX = rotYX               (linear cross-axis = rotation/shear)
eXY = crossXY        eYX = crossYX             (quadratic cross-axis)
dX  = deltaX         dY  = deltaY              (translation)
```

The bXY/bYX and eXY/eYX terms are **optional** — activated by the `R` and `XY` checkboxes (separate in the UI). Full model = 12 parameters per image. By default (`R` and `XY` disabled, both quads disabled) the model reduces to 4 linear parameters (dX, dY, aX, aY).

Parameters saved in JSONL: `scaleX, scaleY, deltaX, deltaY, quadX, quadY, rotXY, rotYX, crossXY, crossYX`. A missing field in the file == 0 (backwards compat.).

## Scale modes (ScaleMode)

| Mode | What is frozen during fit |
|------|---------------------------|
| `Free`        | nothing |
| `PinScaleX`   | `aX` and `cX` (scale X *and* quad X) |
| `PinScaleY`   | `aY` and `cY` |
| `PinScale`    | `aX, cX, aY, cY` — only translation is fitted |
| `PinAspect`   | the ratio `a = scaleY/scaleX` (snapshot) — introduces a constraint between X and Y |

Rule: **pinning scale implies pinning the quad for the same axis.** The quad is treated as a "higher-order scale" — when an axis is locked, its entire parameterisation is frozen (except translation).

In `PinScaleX/Y/Scale` modes the `X²`/`Y²` checkbox is ignored for the locked axis — the value of `cX`/`cY` is *kept* (not zeroed).

### PinAspect

The snapshot `a = aspect = scaleY/scaleX` is taken when the mode is entered (and on `loadParamsFromMap`). In this mode **the aspect is invariant**: the X/Y spinboxes are linked (changing one forces a proportional change in the other), and the fit does not alter the ratio.

Constraints:
```
aY = k · aX            where  k = a · grayW / grayH
cY = a · cX            (only when both X² and Y² are checked — "linked")
```

When only one of `X²`/`Y²` is active in PinAspect, the other quad is zero (independent of aspect).

## Number of unknowns

Each pin contributes **2 equations** (1 for X, 1 for Y). The system is **separable** only when `R` and `XY` are disabled and the scale mode is not PinAspect. Otherwise (PinAspect / R / XY) — the system is **coupled**.

### Separable modes (Free / PinScaleX / PinScaleY / PinScale, R and XY disabled)

```
uX = pinAX ? 1 : (2 + qX)     // pinned → only dX; otherwise 2 (dX, aX), 3 if X² active
uY = pinAY ? 1 : (2 + qY)
```

where `pinAX = mode ∈ {PinScaleX, PinScale}`, `pinAY = mode ∈ {PinScaleY, PinScale}`.

### Coupled (PinAspect *or* R *or* XY)

Total number of unknowns (2N equations):

```
total = 2                                # dX, dY (always)
if mode == PinAspect:
    total += 1                           # shared aX (aY = k·aX)
    if qX or qY: total += 1              # one column c (linked or only)
else:
    if not pinAX:        total += 1      # aX
    if not pinAY:        total += 1      # aY
    if qX and not pinAX: total += 1      # cX
    if qY and not pinAY: total += 1      # cY
if R:  total += 2                        # bXY, bYX
if XY: total += 2                        # eXY, eYX
```

Section 2 (Exact) is non-empty when `total` is even and reachable at `N = total/2`.

## Pin context-menu sections

The menu shows 10 pin slots divided by separators into 3 sections:

1. **Section 1** — too few pins (under-determined system; fit works after partial drop).
2. **Section 2** — exactly enough pins for an *Exact* system (equations = unknowns).
3. **Section 3** — more pins (LSQ — fit minimising the residual).

```
separable:
  maxU   = max(uX, uY)
  sec1End = max(0, maxU - 1)             // pins in section 1
  sec2End = (uX == uY) ? maxU : sec1End  // section 2 non-empty only when uX==uY

pin aspect:
  total   = 3 + (anyQuad ? 1 : 0)
  minN    = ceil(total / 2)
  sec1End = max(0, minN - 1)
  sec2End = (total even) ? total/2 : sec1End
```

Examples (10 slots):

| Mode              | quad      | sec 1   | sec 2 (Exact) | sec 3 (Fit) |
|-------------------|-----------|---------|---------------|-------------|
| Free              | –         | 1       | 2             | 3–10        |
| Free              | X²        | 1–2     | (empty)       | 3–10        |
| Free              | X² + Y²   | 1–2     | 3             | 4–10        |
| PinScaleX         | –         | 1       | (empty)       | 2–10        |
| PinScaleX         | X²        | 1       | (empty)       | 2–10        |
| PinScaleX         | Y²        | 1–2     | (empty)       | 3–10        |
| PinScaleY         | Y²        | 1       | (empty)       | 2–10        |
| PinScale          | –         | (empty) | 1             | 2–10        |
| PinScale          | X² or Y²  | (empty) | 1             | 2–10        |
| PinAspect         | –         | 1       | (empty, total=3 odd) | 2–10 |
| PinAspect         | X² or Y²  | 1       | 2             | 3–10        |
| PinAspect         | X² + Y²   | 1       | 2             | 3–10        |

## Solver

### Common steps

1. Read `currentAX = scaleX/grayW`, `currentAY = scaleY/grayH`, `currentCX = quadX`, `currentCY = quadY`.
2. Centre coordinates and compute moments:
   ```
   Σgxc^k, Σoxc·gxc^k    for k = 0..2
   Σgxc^k                for k = 3, 4    (needed only when useQuadX)
   ```
   Analogously for Y.

### Separable modes — `solveAxis` per axis

Input: `pinScale, useQuad, curA, curC, sums, n`. Output: `(b, a, c)`.

```
if pinScale:
    # Scale and quad frozen, fit only dX
    # rhs_i = o_i - curA·g_i - curC·g_i²
    # dX = (Σo - curA·Σg - curC·Σg²) / n
    b = (sO - curA·sG - curC·sG²) / n
    a = curA
    c = curC
    return

# free scale for this axis
unknowns = 2 + (useQuad ? 1 : 0)
if n < unknowns and useQuad:
    useQuad = false                # drop quad if too few pins
    unknowns = 2
if n < unknowns:
    # drop scale too — translate by current
    a = curA
    c = curC
    b = (sO - curA·sG - curC·sG²) / n
    return

if not useQuad:
    # 2×2 LSQ for (b, a)
    det = n·sG² - sG²
    a = (n·sGO - sG·sO) / det
    b = (sO - a·sG) / n
    c = 0
else:
    # 3×3 LSQ via Gauss with pivoting for (b, a, c):
    #   [ n   sG   sG² ] [b]   [sO  ]
    #   [ sG  sG²  sG³ ] [a] = [sGO ]
    #   [ sG² sG³  sG⁴ ] [c]   [sG²O]
```

### PinAspect — coupled system

`aspect = scaleAspectRatio_`, `k = aspect · grayW / grayH`.

Column configuration (`M ∈ {3, 4}`):

| M | qX | qY | col 3                                              |
|---|----|----|----------------------------------------------------|
| 3 | –  | –  | (none)                                             |
| 4 | ✓  | –  | `cX` — X eq: `gxc²`,   Y eq: `0`                   |
| 4 | –  | ✓  | `cY` — X eq: `0`,      Y eq: `gyc²`                |
| 4 | ✓  | ✓  | `cX` (primary, linked) — X: `gxc²`, Y: `a·gyc²`    |

Design matrix rows (for pin *i*):

```
X eq:  [1, 0, gxc_i, <col3_X>]   = oxc_i
Y eq:  [0, 1, k·gyc_i, <col3_Y>] = oyc_i
```

We build `AᵀA` (`M×M`) and `Aᵀb` (`M`), solve via Gauss with pivoting (`solveLinearSystem`).

Result:
```
dX = x[0]
dY = x[1]
aX_fit = x[2]
newScaleX = |aX_fit · grayW|
newScaleY = |aX_fit · grayW · aspect|     # = aspect · newScaleX

if linkedBoth:  cX = x[3];  cY = aspect · x[3]
if qX only:     cX = x[3];  cY = 0
if qY only:     cX = 0;     cY = x[3]
if no quads:    cX = 0;     cY = 0
```

Fallback when `2N < M`: drops the quad column, then falls back to translation only (scale = current).

## GUI behaviour

- The `scaleX`/`scaleY` spinboxes are linked **only in PinAspect mode**. In all other modes editing one scale does not affect the other.
- Editing the scale spinbox in Pin… modes is **allowed** — the pin only constrains pin fitting, not manual correction.
- Changing scaleX via spinbox in `PinScaleX` **does not clear** `cX` (they are independent).
- `scaleAspectRatio_` is:
  - updated on `loadParamsFromMap` (when the current mode is PinAspect),
  - updated on `onScaleModeChanged` when the user enters PinAspect,
  - updated after a pin fit **when mode ≠ PinAspect**,
  - **not changed** during a fit in PinAspect (the snapshot is preserved).

## Rendering

When any of `quadX, quadY, rotXY, rotYX, crossXY, crossYX` is non-zero, the gray image is warped via `cv::remap`:

### Forward T (gray → outline-centered)

```
T_x(u, v) = aX·u + bXY·v + cX·u² + eXY·u·v
T_y(u, v) = bYX·u + aY·v + cY·v² + eYX·u·v
```

This is a plain polynomial evaluation — no iteration required, regardless of the number of parameters.

### Canvas bbox

- **Separable** (R and XY both = 0): bbox per axis as before (analytical for parabolas).
- **Coupled** (R or XY ≠ 0): sample ~64 points on the gray bounding-box edges, apply forward T to outline coords, take bbox of the resulting samples + 2% margin.

### Inverse (outline → gray)

- **Separable**: `u = invQuad(ox, aX, cX)`, `v = invQuad(oy, aY, cY)` analytically (per-axis parabola).
- **Coupled**: **2D Newton** per pixel:
  1. Linear start: solve `[aX bXY; bYX aY]·[u;v] = [ox;oy]`.
  2. Iterate ≤ 6× (condition `‖F‖² < 1e-8`):
     ```
     F = T(u,v) - (ox,oy)
     J = [aX+2cX·u+eXY·v,  bXY+eXY·u;
          bYX+eYX·v,        aY+2cY·v+eYX·u]
     (u,v) -= J⁻¹ · F
     ```
- `screenToGrayImageCoords` uses the same inversion (analytical for separable, Newton for coupled).

Newton converges in 2–3 iterations for typical parameter magnitudes. Convergence is not guaranteed for extreme distortions — the warp map may have artefacts (usually acceptable for visualisation).

## Parameter interpretation

- **dX, dY** — translation.
- **aX, aY** — diagonal scale.
- **cX, cY** — parabolic curvature along the same axis (X² affects the Y output only when linked in PinAspect).
- **bXY, bYX** (R) — linear cross-axis coupling. Pure rotation: `bXY = -bYX · k` (when aspect=1: `bXY = -bYX`). Independently: shear/affine.
- **eXY, eYX** (XY) — quadratic cross-axis coupling. Twist growing proportionally to `|gxc·gyc|` — maximum at corners, zero on axes.

Typically for AI-generated images with slanted text: the combination of `R` (global rotation) + `XY` (local compensation in the centre) allows the rotation to be matched in one part of the image and compensated in another.

## Minimum required number of pins

A pin can be placed manually via the context menu (right-click), up to `NUM_PINS = 10`. The action `Pins → Restore last` restores the last applied set of pins (after `applyPins` the `pins_[]` slots are cleared, but `lastAppliedPins_` retains its contents).
