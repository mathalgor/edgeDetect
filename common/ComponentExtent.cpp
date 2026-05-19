#include "ComponentExtent.h"

#include <opencv2/imgproc.hpp>
#include <cmath>

float componentExtent(const std::vector<cv::Point>& pts)
{
    if (pts.size() < 2) return 0.0f;
    std::vector<cv::Point> hull;
    cv::convexHull(pts, hull);
    double best = 0;
    for (size_t i = 0; i + 1 < hull.size(); ++i) {
        for (size_t j = i + 1; j < hull.size(); ++j) {
            const double dx = hull[i].x - hull[j].x;
            const double dy = hull[i].y - hull[j].y;
            const double d2 = dx * dx + dy * dy;
            if (d2 > best) best = d2;
        }
    }
    return static_cast<float>(std::sqrt(best));
}
