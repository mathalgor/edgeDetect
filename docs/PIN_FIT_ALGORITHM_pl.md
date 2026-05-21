# Algorytm dopasowania (fit) szpilek

## Model transformacji

Pełny model (do 12 parametrów):

```
oxc = dX + aX·gxc + bXY·gyc + cX·gxc² + eXY·gxc·gyc
oyc = dY + bYX·gxc + aY·gyc + cY·gyc² + eYX·gxc·gyc
```

gdzie

```
gxc = grayX    - grayW/2     gyc = grayY    - grayH/2
oxc = outlineX - outlineW/2  oyc = outlineY - outlineH/2

aX  = scaleX/grayW   aY  = scaleY/grayH        (diagonalna skala liniowa)
cX  = quadX          cY  = quadY               (diagonalna skala kwadratowa)
bXY = rotXY          bYX = rotYX               (cross-axis liniowy = rotacja/shear)
eXY = crossXY        eYX = crossYX             (cross-axis kwadratowy)
dX  = deltaX         dY  = deltaY              (translacja)
```

Człony bXY/bYX i eXY/eYX są **opcjonalne** — aktywowane checkboxami `R` i `XY` (oddzielnymi w UI). Pełny model = 12 parametrów na obraz. Domyślnie (`R` i `XY` wyłączone, oba quady wyłączone) model redukuje się do 4 parametrów liniowych (dX, dY, aX, aY).

Parametry zapisywane w JSONL: `scaleX, scaleY, deltaX, deltaY, quadX, quadY, rotXY, rotYX, crossXY, crossYX`. Brak danego pola w pliku == 0 (wsteczna kompat.).

## Tryby skali (ScaleMode)

| Tryb | Co jest zamrożone podczas fit |
|------|--------------------------------|
| `Free`        | nic |
| `PinScaleX`   | `aX` i `cX` (skala X *i* kwadrat X) |
| `PinScaleY`   | `aY` i `cY` |
| `PinScale`    | `aX, cX, aY, cY` — fituje się tylko translacja |
| `PinAspect`   | stosunek `a = scaleY/scaleX` (snapshot) — wprowadza więzy między X i Y |

Reguła: **pin skali implikuje pin kwadratu dla tej samej osi.** Kwadrat jest traktowany jak "skala wyższego rzędu" — gdy zamykamy oś, blokujemy całą jej parametryzację (oprócz translacji).

W trybach `PinScaleX/Y/Scale` checkbox `X²`/`Y²` jest ignorowany dla zamkniętej osi — wartość `cX`/`cY` pozostaje *aktualna* (nie zerowana).

### PinAspect

Snapshot `a = aspect = scaleY/scaleX` jest pobierany w momencie wejścia w tryb (i przy `loadParamsFromMap`). W trybie tym **aspect jest niezmienny**: spinboxy X/Y są wzajemnie powiązane (zmiana jednej wymusza proporcjonalną zmianę drugiej), a fit nie zmienia stosunku.

Więzy:
```
aY = k · aX            gdzie  k = a · grayW / grayH
cY = a · cX            (tylko gdy oba X² i Y² zaznaczone — "linked")
```

Gdy tylko jeden z `X²`/`Y²` jest aktywny w PinAspect, drugi quad jest zerem (niezależny od aspectu).

## Liczba niewiadomych

Każda szpilka daje **2 równania** (1 dla X, 1 dla Y). Układ jest **separowalny** tylko gdy `R` i `XY` wyłączone i tryb skali nie jest PinAspect. W innych przypadkach (PinAspect / R / XY) — układ **sprzęgnięty**.

### Tryby separowalne (Free / PinScaleX / PinScaleY / PinScale, R i XY wyłączone)

```
uX = pinAX ? 1 : (2 + qX)     // pin → tylko dX; inaczej 2 (dX, aX), 3 jeśli X² aktywne
uY = pinAY ? 1 : (2 + qY)
```

gdzie `pinAX = mode ∈ {PinScaleX, PinScale}`, `pinAY = mode ∈ {PinScaleY, PinScale}`.

### Sprzęgnięty (PinAspect *lub* R *lub* XY)

Łączna liczba niewiadomych (2N równań):

```
total = 2                                # dX, dY (zawsze)
if mode == PinAspect:
    total += 1                           # wspólne aX (aY = k·aX)
    if qX or qY: total += 1              # jedna kolumna c (linked albo only)
else:
    if not pinAX:        total += 1      # aX
    if not pinAY:        total += 1      # aY
    if qX and not pinAX: total += 1      # cX
    if qY and not pinAY: total += 1      # cY
if R:  total += 2                        # bXY, bYX
if XY: total += 2                        # eXY, eYX
```

Sekcja 2 (Exact) niepusta gdy `total` parzyste i osiągalne przy `N = total/2`.

## Sekcje menu kontekstowego szpilek

Menu pokazuje 10 slotów na szpilki, podzielonych separatorami na 3 sekcje:

1. **Sekcja 1** — pinów za mało (niedookreślony układ; fit działa po częściowym dropie).
2. **Sekcja 2** — pinów dokładnie tyle, że układ jest *Exact* (równania = niewiadome).
3. **Sekcja 3** — więcej pinów (LSQ — Fit minimalizujący residual).

```
separable:
  maxU   = max(uX, uY)
  sec1End = max(0, maxU - 1)             // pinów w sekcji 1
  sec2End = (uX == uY) ? maxU : sec1End  // sekcja 2 niepusta tylko gdy uX==uY

pin aspect:
  total   = 3 + (anyQuad ? 1 : 0)
  minN    = ceil(total / 2)
  sec1End = max(0, minN - 1)
  sec2End = (total parzyste) ? total/2 : sec1End
```

Przykłady (na 10 slotach):

| Tryb              | quad      | sec 1   | sec 2 (Exact) | sec 3 (Fit) |
|-------------------|-----------|---------|---------------|-------------|
| Free              | –         | 1       | 2             | 3–10        |
| Free              | X²        | 1–2     | (puste)       | 3–10        |
| Free              | X² + Y²   | 1–2     | 3             | 4–10        |
| PinScaleX         | –         | 1       | (puste)       | 2–10        |
| PinScaleX         | X²        | 1       | (puste)       | 2–10        |
| PinScaleX         | Y²        | 1–2     | (puste)       | 3–10        |
| PinScaleY         | Y²        | 1       | (puste)       | 2–10        |
| PinScale          | –         | (puste) | 1             | 2–10        |
| PinScale          | X² lub Y² | (puste) | 1             | 2–10        |
| PinAspect         | –         | 1       | (puste, total=3 nieparz.) | 2–10 |
| PinAspect         | X² lub Y² | 1       | 2             | 3–10        |
| PinAspect         | X² + Y²   | 1       | 2             | 3–10        |

## Solver

### Wspólne kroki

1. Wczytaj `currentAX = scaleX/grayW`, `currentAY = scaleY/grayH`, `currentCX = quadX`, `currentCY = quadY`.
2. Wycentruj współrzędne i policz momenty:
   ```
   Σgxc^k, Σoxc·gxc^k    dla k = 0..2
   Σgxc^k                dla k = 3, 4    (potrzebne tylko gdy useQuadX)
   ```
   Analogicznie dla Y.

### Tryby separowalne — `solveAxis` per oś

Input: `pinScale, useQuad, curA, curC, sums, n`. Output: `(b, a, c)`.

```
if pinScale:
    # Skala i quad zamrożone, fitujemy tylko dX
    # rhs_i = o_i - curA·g_i - curC·g_i²
    # dX = (Σo - curA·Σg - curC·Σg²) / n
    b = (sO - curA·sG - curC·sG²) / n
    a = curA
    c = curC
    return

# free skala dla tej osi
unknowns = 2 + (useQuad ? 1 : 0)
if n < unknowns and useQuad:
    useQuad = false                # drop quad jeśli za mało pinów
    unknowns = 2
if n < unknowns:
    # drop też skali — translacja po current
    a = curA
    c = curC
    b = (sO - curA·sG - curC·sG²) / n
    return

if not useQuad:
    # 2×2 LSQ na (b, a)
    det = n·sG² - sG²
    a = (n·sGO - sG·sO) / det
    b = (sO - a·sG) / n
    c = 0
else:
    # 3×3 LSQ przez Gauss z pivotingiem na (b, a, c):
    #   [ n   sG   sG² ] [b]   [sO  ]
    #   [ sG  sG²  sG³ ] [a] = [sGO ]
    #   [ sG² sG³  sG⁴ ] [c]   [sG²O]
```

### PinAspect — sprzęgnięty układ

`aspect = scaleAspectRatio_`, `k = aspect · grayW / grayH`.

Konfiguracja kolumn (`M ∈ {3, 4}`):

| M | qX | qY | col 3                                              |
|---|----|----|----------------------------------------------------|
| 3 | –  | –  | (brak)                                             |
| 4 | ✓  | –  | `cX` — X eq: `gxc²`,   Y eq: `0`                   |
| 4 | –  | ✓  | `cY` — X eq: `0`,      Y eq: `gyc²`                |
| 4 | ✓  | ✓  | `cX` (primary, linked) — X: `gxc²`, Y: `a·gyc²`    |

Wiersze macierzy projektowej (dla pinu *i*):

```
X eq:  [1, 0, gxc_i, <col3_X>]   = oxc_i
Y eq:  [0, 1, k·gyc_i, <col3_Y>] = oyc_i
```

Budujemy `AᵀA` (`M×M`) i `Aᵀb` (`M`), rozwiązujemy Gauss z pivotingiem (`solveLinearSystem`).

Wynik:
```
dX = x[0]
dY = x[1]
aX_fit = x[2]
newScaleX = |aX_fit · grayW|
newScaleY = |aX_fit · grayW · aspect|     # = aspect · newScaleX

jeśli linkedBoth:  cX = x[3];  cY = aspect · x[3]
jeśli qX only:     cX = x[3];  cY = 0
jeśli qY only:     cX = 0;     cY = x[3]
jeśli bez quadów:  cX = 0;     cY = 0
```

Fallback gdy `2N < M`: dropuje quadową kolumnę, potem zostaje translacja (skala = current).

## Zachowanie GUI

- Spinboxy `scaleX`/`scaleY` są wzajemnie powiązane **tylko w trybie PinAspect**. W pozostałych trybach edycja jednej skali nie rusza drugiej.
- Edycja spinboxa skali w trybach Pin… jest **dozwolona** — pin dotyczy tylko fit szpilek, nie ręcznej korekty.
- Zmiana scaleX przez spinbox w `PinScaleX` **nie kasuje** `cX` (są niezależne).
- `scaleAspectRatio_` jest:
  - aktualizowany przy `loadParamsFromMap` (gdy bieżący tryb to PinAspect),
  - aktualizowany przy `onScaleModeChanged` gdy user wchodzi w PinAspect,
  - aktualizowany po fit szpilek **gdy tryb ≠ PinAspect**,
  - **nie zmieniany** podczas fit w PinAspect (snapshot pozostaje).

## Renderowanie

Gdy którykolwiek z `quadX, quadY, rotXY, rotYX, crossXY, crossYX` jest niezerowy, widok obrazu gray jest deformowany przez `cv::remap`:

### Forward T (gray → outline-centered)

```
T_x(u, v) = aX·u + bXY·v + cX·u² + eXY·u·v
T_y(u, v) = bYX·u + aY·v + cY·v² + eYX·u·v
```

To zwykła ewaluacja wielomianu — nie wymaga iteracji, niezależnie od liczby parametrów.

### Canvas bbox

- **Separowalne** (R i XY oba = 0): bbox per oś jak wcześniej (analityczny dla parabol).
- **Sprzęgnięte** (R lub XY ≠ 0): próbkowanie ~64 punktów na krawędziach gray-bounding box, forward T do outline coords, bbox z otrzymanych próbek + 2% margines.

### Inverse (outline → gray)

- **Separowalne**: `u = invQuad(ox, aX, cX)`, `v = invQuad(oy, aY, cY)` analitycznie (kwadrat na osi).
- **Sprzęgnięte**: **Newton 2D** per piksel:
  1. Start liniowy: rozwiąż `[aX bXY; bYX aY]·[u;v] = [ox;oy]`.
  2. Iteruj ≤ 6× (warunek `‖F‖² < 1e-8`):
     ```
     F = T(u,v) - (ox,oy)
     J = [aX+2cX·u+eXY·v,  bXY+eXY·u;
          bYX+eYX·v,        aY+2cY·v+eYX·u]
     (u,v) -= J⁻¹ · F
     ```
- `screenToGrayImageCoords` używa tej samej inwersji (analitycznie dla separable, Newton dla sprzęgniętego).

Newton zbiega 2-3 iteracje dla typowych param. magnitude. Brak gwarancji konwergencji przy ekstremalnych zniekształceniach — wtedy mapa ma artefakty (zwykle akceptowalne dla wizualizacji).

## Interpretacja parametrów

- **dX, dY** — translacja.
- **aX, aY** — skala diagonalna.
- **cX, cY** — krzywizna paraboliczna w obrębie tej samej osi (X² wpływa na Y wyniku tylko jeśli linkowane w PinAspect).
- **bXY, bYX** (R) — liniowe sprzężenie cross-axis. Czysta rotacja: `bXY = -bYX · k` (gdy aspect=1: `bXY = -bYX`). Niezależnie: shear/affine.
- **eXY, eYX** (XY) — kwadratowe sprzężenie cross-axis. Skręt rosnący proporcjonalnie do `|gxc·gyc|` — maksymalny w rogach, zero na osiach.

Typowo dla obrazów AI z ukośnym tekstem: kombinacja `R` (globalny obrót) + `XY` (lokalna kompensacja w środku) pozwala dopasować obrót w jednej części obrazu i wyrównać go w innej.

## Wymagana minimalna liczba szpilek

Pin może być umieszczony manualnie przez menu kontekstowe (right-click), do `NUM_PINS = 10`. Akcja `Pins → Restore last` przywraca ostatnio zastosowany zestaw szpilek (po `applyPins` slotów `pins_[]` są czyszczone, ale `lastAppliedPins_` zachowuje zawartość).