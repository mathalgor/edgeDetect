#ifndef EDGEDETECT_EDIT_COLORS_H
#define EDGEDETECT_EDIT_COLORS_H

#include <QColor>

// Shared overlay colors used by the rect/strip selection in both
// cannyToOutline and outlineChooser. Centralised so the two apps stay
// visually consistent.
namespace edit_colors {

// Rubber-band lines and fill while drawing a rect / strip / pending poly.
inline QColor rubberBand()     { return QColor(0, 200, 255);     }
inline QColor rubberBandFill() { return QColor(0, 200, 255, 40); }

// Live candidate preview after the polygon is captured:
//   orange = pixels that pass threshold and will be committed
//   yellow = pixels of eligible components that the threshold rejects
inline QColor candidateOrange() { return QColor(255, 140, 0,  210); }
inline QColor candidateYellow() { return QColor(200, 200, 35, 190); }

}  // namespace edit_colors

#endif
