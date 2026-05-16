#ifndef EDGEDETECT_VIEW_PRESET_H
#define EDGEDETECT_VIEW_PRESET_H

#include <QColor>
#include <QString>

// A view preset is (background source, color table over 8 (in1,in2,out) cells).
// cell index = (in1 << 2) | (in2 << 1) | out, so:
//   0=000 none, 1=001 out-only, 2=010 o2-only, 3=011 o2+out,
//   4=100 o1-only, 5=101 o1+out, 6=110 common (no out), 7=111 all.
// A cell with alpha==0 is transparent and shows the background through.
struct ViewPreset {
    enum class Background { White, GraySource, Original, Black };

    QString name;
    Background bg = Background::White;
    QColor cells[8];
};

#endif
