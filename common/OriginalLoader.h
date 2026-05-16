#ifndef EDGEDETECT_ORIGINAL_LOADER_H
#define EDGEDETECT_ORIGINAL_LOADER_H

#include <QString>
#include <opencv2/core.hpp>

// Looks up "<dir>/<stem>.<ext>" trying common image extensions, then any
// "<stem>.*" match. Reads via cv::imread(IMREAD_UNCHANGED) and returns the
// raw cv::Mat (BGR/BGRA/gray). Returns empty Mat if not found or dir invalid.
cv::Mat loadOriginalForStem(const QString& dir, const QString& stem);

#endif
