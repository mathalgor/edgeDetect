#include "Canny.h"

#include <opencv2/imgproc.hpp>
#include <vector>
#include <cstdlib>


void Canny::prepare(const cv::Mat& src8u)
{
    CV_Assert(src8u.type() == CV_8UC1);
    const int rows = src8u.rows;
    const int cols = src8u.cols;

    // Sobel CV_16S, ksize=3 (same default as cv::Canny).
    cv::Mat gx, gy;
    cv::Sobel(src8u, gx, CV_16S, 1, 0, 3);
    cv::Sobel(src8u, gy, CV_16S, 0, 1, 3);

    // 1. Squared L2 magnitude as CV_32S.
    cv::Mat mag(rows, cols, CV_32S);
    for (int y = 0; y < rows; ++y) {
        const short* gxr = gx.ptr<short>(y);
        const short* gyr = gy.ptr<short>(y);
        int* mr = mag.ptr<int>(y);
        for (int x = 0; x < cols; ++x) {
            int xx = gxr[x], yy = gyr[x];
            mr[x] = xx * xx + yy * yy;
        }
    }

    // 2. NMS without atan2: ay*1000 vs ax*414/2414 (tan 22.5°/67.5°).
    //    Result = magSq_; non-max pixels are zeroed. Borders skipped.
    magSq_ = cv::Mat::zeros(rows, cols, CV_32S);
    for (int y = 1; y < rows - 1; ++y) {
        const int*   mP = mag.ptr<int>(y);
        const int*   mU = mag.ptr<int>(y - 1);
        const int*   mD = mag.ptr<int>(y + 1);
        const short* gxR = gx.ptr<short>(y);
        const short* gyR = gy.ptr<short>(y);
        int*         oP  = magSq_.ptr<int>(y);
        for (int x = 1; x < cols - 1; ++x) {
            int v = mP[x];
            if (v == 0) continue;
            int dx = gxR[x], dy = gyR[x];
            int ax = std::abs(dx);
            int ay = std::abs(dy);
            int ax414  = ax * 414;
            int ax2414 = ax * 2414;
            int ay1000 = ay * 1000;
            int n1, n2;
            if (ay1000 < ax414) {                    // ~horizontal
                n1 = mP[x - 1];   n2 = mP[x + 1];
            } else if (ay1000 < ax2414) {            // diagonal
                if ((dx ^ dy) >= 0) {                // same sign
                    n1 = mU[x - 1]; n2 = mD[x + 1];
                } else {
                    n1 = mD[x - 1]; n2 = mU[x + 1];
                }
            } else {                                 // ~vertical
                n1 = mU[x];       n2 = mD[x];
            }
            if (v >= n1 && v >= n2) oP[x] = v;
        }
    }
}

namespace {
constexpr uchar M_NONE   = 0;
constexpr uchar M_MAYBE  = 1;
constexpr uchar M_EDGE   = 2;
}

int Canny::apply(double low, double high, cv::Mat& dst) const
{
    if (magSq_.empty()) { dst.release(); return 0; }
    if (low  < 0) low  = 0;
    if (high < low) std::swap(low, high);

    double lowD  = std::min(32767.0, low);
    double highD = std::min(32767.0, high);
    long long lowSq  = static_cast<long long>(lowD  * lowD);
    long long highSq = static_cast<long long>(highD * highD);

    const int rows = magSq_.rows;
    const int cols = magSq_.cols;
    const int mw = cols + 2;
    const size_t mapBytes = static_cast<size_t>(rows + 2) * mw;
    if (mapBuf_.size() != mapBytes) mapBuf_.assign(mapBytes, M_NONE);
    else {
        // Zero only the FRAME (interior is overwritten explicitly below).
        // Top/bottom rows:
        std::fill_n(mapBuf_.data(),                          mw, M_NONE);
        std::fill_n(mapBuf_.data() + (rows + 1) * mw,        mw, M_NONE);
        // Left/right columns (one byte per interior row):
        for (int y = 1; y <= rows; ++y) {
            mapBuf_[y * mw]         = M_NONE;
            mapBuf_[y * mw + cols + 1] = M_NONE;
        }
    }
    uchar* mapData = mapBuf_.data();

    stackBuf_.clear();
    stackBuf_.reserve(static_cast<size_t>(rows) * 64);
    auto& stack = stackBuf_;
    for (int y = 0; y < rows; ++y) {
        const int* mr = magSq_.ptr<int>(y);
        uchar* row = mapData + (y + 1) * mw + 1;
        for (int x = 0; x < cols; ++x) {
            int v = mr[x];
            if (v >= highSq) {
                row[x] = M_EDGE;
                stack.push_back(row + x);
            } else if (v >= lowSq) {
                row[x] = M_MAYBE;
            } else {
                row[x] = M_NONE;
            }
        }
    }

    while (!stack.empty()) {
        uchar* m = stack.back();
        stack.pop_back();
        uchar* nb[8] = {
            m - mw - 1, m - mw, m - mw + 1,
            m - 1,             m + 1,
            m + mw - 1, m + mw, m + mw + 1
        };
        for (uchar* p : nb) {
            if (*p == M_MAYBE) { *p = M_EDGE; stack.push_back(p); }
        }
    }

    // Output + edge count in a single pass.
    dst.create(rows, cols, CV_8UC1);
    int count = 0;
    for (int y = 0; y < rows; ++y) {
        const uchar* row = mapData + (y + 1) * mw + 1;
        uchar* d = dst.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            uchar v = (row[x] == M_EDGE) ? 255 : 0;
            d[x] = v;
            count += (v != 0);
        }
    }
    return count;
}

void Canny::applyThresholdOnly(double high, cv::Mat& dst) const
{
    if (magSq_.empty()) { dst.release(); return; }
    if (high < 0) high = 0;
    double hD = std::min(32767.0, high);
    long long hSq = static_cast<long long>(hD * hD);

    dst.create(magSq_.size(), CV_8UC1);
    for (int y = 0; y < magSq_.rows; ++y) {
        const int* mr = magSq_.ptr<int>(y);
        uchar* d = dst.ptr<uchar>(y);
        for (int x = 0; x < magSq_.cols; ++x) {
            d[x] = (mr[x] >= hSq) ? 255 : 0;
        }
    }
}