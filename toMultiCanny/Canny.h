#pragma once

#include <opencv2/core.hpp>

// Canny split into prepare() (Sobel + L2² magnitude + NMS — once per image)
// and apply(low, high, dst) (hysteresis only, very fast).
// Designed for the scenario of running Canny many times on the same source
// with different thresholds (e.g. 255 iterations to build a gray map).
class Canny {
public:
    void prepare(const cv::Mat& src8u);
    // Full hysteresis, dst CV_8UC1 (0 or 255). low/high on cv::Canny scale.
    // Returns the number of pixels equal to 255 (= cv::countNonZero(dst)).
    int  apply(double low, double high, cv::Mat& dst) const;
    // No hysteresis — just a threshold on NMS magnitude (variant C).
    void applyThresholdOnly(double high, cv::Mat& dst) const;

    // Direct gray-map generation for variant C — without 256 iterations.
    // valueFunc(pct) -> 1..255 computes the pixel value from the percentage
    // of non-black pixels.
    // grayResult: CV_8UC1, 0 for pixels not on NMS (or zero magnitude).
    template <class F>
    void buildGrayMapNms(cv::Mat& grayResult, F valueFunc) const;

    bool empty() const { return magSq_.empty(); }
    cv::Size size() const { return magSq_.size(); }

private:
    // Post-NMS squared magnitude (L2): gx² + gy², CV_32S.
    // Same scale as cv::Canny(L2gradient=true) uses internally.
    cv::Mat magSq_;

    // Reusable apply() buffers, to avoid allocations per call.
    mutable std::vector<unsigned char>  mapBuf_;
    mutable std::vector<unsigned char*> stackBuf_;
};

template <class F>
void Canny::buildGrayMapNms(cv::Mat& grayResult, F valueFunc) const
{
    if (magSq_.empty()) { grayResult.release(); return; }
    const int rows = magSq_.rows;
    const int cols = magSq_.cols;
    const int total = rows * cols;

    // Histogram of integer magnitudes M = round(sqrt(magSq)).
    // Max M ≈ sqrt(2)*4*255 ≈ 1442. 2048 is a safe upper bound.
    constexpr int MAX_MAG = 2048;
    std::vector<int> hist(MAX_MAG + 1, 0);
    cv::Mat magInt(rows, cols, CV_16U);
    for (int y = 0; y < rows; ++y) {
        const int* mr = magSq_.ptr<int>(y);
        unsigned short* dr = magInt.ptr<unsigned short>(y);
        for (int x = 0; x < cols; ++x) {
            int v = mr[x];
            if (v == 0) { dr[x] = 0; continue; }
            int m = static_cast<int>(std::lround(std::sqrt(static_cast<double>(v))));
            if (m > MAX_MAG) m = MAX_MAG;
            dr[x] = static_cast<unsigned short>(m);
            ++hist[m];
        }
    }
    // Top-down CDF: cdf[m] = number of pixels with mag ≥ m.
    std::vector<int> cdf(MAX_MAG + 1, 0);
    int cum = 0;
    for (int m = MAX_MAG; m >= 0; --m) {
        cum += hist[m];
        cdf[m] = cum;
    }

    // Each pixel gets a value derived from its magnitude.
    // A pixel with magnitude M is an edge for high ≤ M, so the percentage
    // at threshold = M is decisive: pct = cdf[M] / total.
    // Cache: value_lookup[M] for M=0..MAX_MAG.
    std::vector<uchar> valLut(MAX_MAG + 1, 0);
    for (int m = 1; m <= MAX_MAG; ++m) {
        if (cdf[m] == 0) { valLut[m] = 0; continue; }
        double pct = 100.0 * static_cast<double>(cdf[m]) /
                              static_cast<double>(total);
        valLut[m] = valueFunc(pct);
    }
    valLut[0] = 0;

    grayResult.create(rows, cols, CV_8UC1);
    for (int y = 0; y < rows; ++y) {
        const unsigned short* mr = magInt.ptr<unsigned short>(y);
        uchar* dr = grayResult.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            dr[x] = valLut[mr[x]];
        }
    }
}