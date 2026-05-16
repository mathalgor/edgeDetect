#ifndef EDGEDETECT_EDIT_COLORS_H
#define EDGEDETECT_EDIT_COLORS_H

#include <QColor>
#include <opencv2/core.hpp>

// Shared overlay colors used by the rect/strip selection and by the
// preset palettes in cannyToOutline and outlineChooser. Centralised so
// the two apps stay visually consistent.
//
// Candidate colors are exposed as cv::Vec4b because they are stamped
// directly into RGBA cv::Mat overlays. The rubber-band helpers stay
// QColor because they're consumed by QPen / QBrush in paintEvent.
namespace edit_colors {

// Rubber-band lines and fill while drawing a rect / strip.
inline QColor rubberBand()     { return QColor(0, 200, 255);     }
inline QColor rubberBandFill() { return QColor(0, 200, 255, 40); }

// Canonical overlay colors. Used for the live candidate preview
// (blue = will be committed, dark yellow = eligible but rejected) and
// for the "common, not in result" cell in outlineChooser presets.
// Blue is used instead of an orange shade because the standard chooser
// palette already uses red for only-outline-2 pixels — orange would be
// too easy to confuse with it.
inline cv::Vec4b blue()     { return cv::Vec4b(0, 0, 240, 220); }
inline cv::Vec4b darkYellow() { return cv::Vec4b(180, 180, 0, 220); }

// Convenience: Vec4b -> QColor for the paths that need a QColor.
inline QColor toQColor(const cv::Vec4b& v) {
    return QColor(v[0], v[1], v[2], v[3]);
}

}  // namespace edit_colors

#endif
