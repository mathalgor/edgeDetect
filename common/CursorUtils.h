#ifndef EDGEDETECT_CURSOR_UTILS_H
#define EDGEDETECT_CURSOR_UTILS_H

#include <QCursor>

// Builds a small white arrow cursor with a tolerance circle around the tip.
// Used by edit modes that pick a seed pixel within a radius (Ctrl-pick).
// radiusPx — visible radius of the tolerance circle in widget pixels.
QCursor makePickCursor(int radiusPx = 8);

#endif
