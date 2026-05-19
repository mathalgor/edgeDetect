#ifndef EDGEDETECT_COMPONENT_EXTENT_H
#define EDGEDETECT_COMPONENT_EXTENT_H

#include <opencv2/core.hpp>
#include <vector>

// Maximum Euclidean distance between any two points on the convex hull
// of `pts`. Used as a component-extent metric so long thin segments
// score higher than compact blobs of the same size. Returns 0 when
// pts.size() < 2.
float componentExtent(const std::vector<cv::Point>& pts);

#endif
