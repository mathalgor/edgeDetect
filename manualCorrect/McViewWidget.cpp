#include "McViewWidget.h"

#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QPixmap>

#include "ComponentExtent.h"
#include "CursorUtils.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <queue>

static constexpr int kMinPolyVerts = 3;

McViewWidget::McViewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    buildDefaultPresets();
}

void McViewWidget::updateCursorForMods(Qt::KeyboardModifiers m)
{
    if (editLocked_ || vis_.isNull()) { setCursor(Qt::ArrowCursor); return; }
    if (m & Qt::ControlModifier) {
        static const QCursor eraser = makePickCursor(8);
        setCursor(eraser);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

int McViewWidget::pickEraseLabelNear(int cx, int cy, int radius) const
{
    if (labels_.empty() || outResult_.empty()) return 0;
    const int H = labels_.rows, W = labels_.cols;
    int best = 0;
    int bestD2 = std::numeric_limits<int>::max();
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= H) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= W) continue;
            const int d2 = dx * dx + dy * dy;
            if (d2 > r2 || d2 >= bestD2) continue;
            const int L = labels_.at<int>(y, x);
            if (L == 0) continue;
            if (outResult_.at<uchar>(y, x) != 255) continue;
            best = L;
            bestD2 = d2;
        }
    }
    return best;
}

void McViewWidget::eraseLabel(int L)
{
    if (L <= 0 || labels_.empty()) return;
    std::vector<cv::Point> pts;
    const int H = labels_.rows, W = labels_.cols;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            if (rowL[x] == L && rowOut[x] == 255)
                pts.emplace_back(x, y);
        }
    }
    if (pts.empty()) return;
    applyOp(pts, false);
    emit editOp(std::move(pts), false);
}

void McViewWidget::penLabel(int L)
{
    if (L <= 0 || labels_.empty()) return;
    std::vector<cv::Point> pts;
    const int H = labels_.rows, W = labels_.cols;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            if (rowL[x] == L && rowOut[x] == 0)
                pts.emplace_back(x, y);
        }
    }
    if (pts.empty()) return;
    applyOp(pts, true);
    emit editOp(std::move(pts), true);
}

McViewWidget::EditPick
McViewWidget::pickEditTargetNear(int cx, int cy, int radius,
                                 bool allowPen) const
{
    EditPick pick;
    if (labels_.empty() || outResult_.empty()) return pick;
    const int H = labels_.rows, W = labels_.cols;
    const int r2 = radius * radius;

    // Pass 1: prefer the closest pixel that's already in the result. Erase
    // always wins over pen, since pen is a fallback for blank areas.
    int bestD2 = std::numeric_limits<int>::max();
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= H) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= W) continue;
            const int d2 = dx * dx + dy * dy;
            if (d2 > r2 || d2 >= bestD2) continue;
            if (outResult_.at<uchar>(y, x) != 255) continue;
            pick.label    = labels_.at<int>(y, x);
            pick.inResult = true;
            bestD2 = d2;
        }
    }
    if (pick.inResult) return pick;
    if (!allowPen) return pick;

    // Pass 2: no result pixel near — closest gray-segment pixel for pen.
    bestD2 = std::numeric_limits<int>::max();
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= H) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= W) continue;
            const int d2 = dx * dx + dy * dy;
            if (d2 > r2 || d2 >= bestD2) continue;
            const int L = labels_.at<int>(y, x);
            if (L == 0) continue;
            pick.label    = L;
            pick.inResult = false;
            bestD2 = d2;
        }
    }
    return pick;
}

void McViewWidget::buildDefaultPresets()
{
    presets_ = {
        {"Result only",       Bg::Plain,    true,  false},
        {"Original",          Bg::Original, false, false},
        {"Original + Result", Bg::Original, true,  false},
        {"Gray + Result",     Bg::Gray,     true,  false},
        {"Gray-red + Result", Bg::GrayRed,  true,  false},
    };
    presetIndex_ = 0;
    prevPresetIndex_ = 0;
}

void McViewWidget::setPresetIndex(int i)
{
    if (i < 0 || i >= static_cast<int>(presets_.size())) return;
    if (i == presetIndex_) return;
    prevPresetIndex_ = presetIndex_;
    presetIndex_ = i;
    rebuildVisualization();
    update();
    emit presetChanged(i);
}

void McViewWidget::swapWithPrevPreset()
{
    if (prevPresetIndex_ == presetIndex_) return;
    setPresetIndex(prevPresetIndex_);
}

void McViewWidget::setFadePercent(int p)
{
    p = std::clamp(p, 0, 100);
    if (p == fadePercent_) return;
    fadePercent_ = p;
    rebuildVisualization();
    update();
}

void McViewWidget::setData(const cv::Mat& inOutlineFileFmt,
                           const cv::Mat& dbgrg,
                           const cv::Mat& originalBgr,
                           const cv::Mat& outFileFmt)
{
    if (inOutlineFileFmt.empty() || dbgrg.empty()) {
        outIn_.release();
        outResult_.release();
        srcGray_.release();
        probR_.release();
        originalRgba_.release();
        vis_ = QImage();
        labels_.release();
        labelSize_.clear();
        labelGray_.clear();
        labelAvgR_.clear();
        polyVerts_.clear();
        polyOpen_ = false;
        polyMask_.release();
        dirty_ = false;
        pendingFit_ = true;
        update();
        return;
    }

    // Internal format: 255 = line.
    cv::Mat inGray;
    if (inOutlineFileFmt.channels() == 1) inGray = inOutlineFileFmt;
    else cv::cvtColor(inOutlineFileFmt, inGray, cv::COLOR_BGR2GRAY);
    cv::bitwise_not(inGray, outIn_);

    if (outFileFmt.empty()) {
        outResult_ = outIn_.clone();
    } else {
        cv::Mat o;
        if (outFileFmt.channels() == 1) o = outFileFmt;
        else cv::cvtColor(outFileFmt, o, cv::COLOR_BGR2GRAY);
        cv::bitwise_not(o, outResult_);
    }

    // dbgrg: BGR with B=0, G=gray, R=prob.
    cv::Mat ch[3];
    cv::split(dbgrg, ch);
    srcGray_ = ch[1].clone();   // G
    probR_   = ch[2].clone();   // R

    // Align all to srcGray_ size (in practice they match).
    if (outResult_.size() != srcGray_.size())
        cv::resize(outResult_, outResult_, srcGray_.size(), 0, 0, cv::INTER_NEAREST);

    // Original (optional) → RGBA at data size.
    if (!originalBgr.empty()) {
        cv::Mat o = originalBgr;
        if (o.size() != srcGray_.size())
            cv::resize(o, o, srcGray_.size(), 0, 0, cv::INTER_AREA);
        if (o.channels() == 1)
            cv::cvtColor(o, originalRgba_, cv::COLOR_GRAY2RGBA);
        else if (o.channels() == 3)
            cv::cvtColor(o, originalRgba_, cv::COLOR_BGR2RGBA);
        else if (o.channels() == 4)
            cv::cvtColor(o, originalRgba_, cv::COLOR_BGRA2RGBA);
    } else {
        originalRgba_.release();
    }

    polyVerts_.clear();
    polyOpen_ = false;
    polyMask_.release();
    dirty_ = false;
    pendingFit_ = true;

    analyzeComponents();
    rebuildVisualization();
    update();
}

void McViewWidget::setConn8(bool on)
{
    if (conn8_ == on) return;
    conn8_ = on;
    if (!srcGray_.empty()) {
        cancelPolygon();
        analyzeComponents();
    }
}


void McViewWidget::setEditLocked(bool on)
{
    if (editLocked_ == on) return;
    editLocked_ = on;
    if (on) cancelPolygon();
    update();
}

cv::Mat McViewWidget::outputFileFmt() const
{
    if (outResult_.empty()) return {};
    cv::Mat o;
    cv::bitwise_not(outResult_, o);
    return o;
}

void McViewWidget::analyzeComponents()
{
    labels_ = cv::Mat::zeros(srcGray_.size(), CV_32S);
    labelSize_.assign(1, 0);
    labelGray_.assign(1, 255);
    labelAvgR_.assign(1, 0);
    labelExtent_.assign(1, 0);
    if (srcGray_.empty()) return;

    const int H = srcGray_.rows, W = srcGray_.cols;
    const int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    const int dx4[4] = {-1, 1, 0, 0};
    const int dy4[4] = { 0, 0,-1, 1};
    const int nN = conn8_ ? 8 : 4;
    const int* dx = conn8_ ? dx8 : dx4;
    const int* dy = conn8_ ? dy8 : dy4;

    int nextLabel = 1;
    std::queue<std::pair<int,int>> q;
    for (int y = 0; y < H; ++y) {
        const uchar* rowG = srcGray_.ptr<uchar>(y);
        int* rowL = labels_.ptr<int>(y);
        for (int x = 0; x < W; ++x) {
            if (rowL[x] != 0) continue;
            const uchar g = rowG[x];
            if (g == 255) continue;
            const int L = nextLabel++;
            rowL[x] = L;
            std::vector<cv::Point> comp;
            std::uint64_t sumR = 0;
            q.push({x, y});
            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                comp.emplace_back(cx, cy);
                sumR += probR_.at<uchar>(cy, cx);
                for (int k = 0; k < nN; ++k) {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    if (labels_.at<int>(ny, nx) != 0) continue;
                    if (srcGray_.at<uchar>(ny, nx) != g) continue;
                    labels_.at<int>(ny, nx) = L;
                    q.push({nx, ny});
                }
            }
            const int size = static_cast<int>(comp.size());
            labelSize_.push_back(size);
            labelGray_.push_back(g);
            labelAvgR_.push_back(static_cast<uchar>(
                std::min<std::uint64_t>(255, sumR / std::max(1, size))));
            labelExtent_.push_back(
                static_cast<int>(std::ceil(componentExtent(comp))));
        }
    }
}

void McViewWidget::rebuildVisualization()
{
    if (srcGray_.empty()) { vis_ = QImage(); return; }
    const int H = srcGray_.rows, W = srcGray_.cols;
    vis_ = QImage(W, H, QImage::Format_ARGB32);

    const Preset& pr = presets_[presetIndex_];
    const Bg bgMode = pr.bg;
    const bool showResult = pr.showResult;
    const bool showIn = pr.showInputOutline && !outIn_.empty();

    for (int y = 0; y < H; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(vis_.scanLine(y));
        const uchar* rowG = srcGray_.ptr<uchar>(y);
        const uchar* rowR = probR_.ptr<uchar>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        const uchar* rowIn  = showIn ? outIn_.ptr<uchar>(y) : nullptr;
        for (int x = 0; x < W; ++x) {
            int br = 255, bg = 255, bb = 255;
            switch (bgMode) {
            case Bg::Plain: break;
            case Bg::Original:
                if (!originalRgba_.empty()) {
                    const cv::Vec4b& p = originalRgba_.at<cv::Vec4b>(y, x);
                    br = p[0]; bg = p[1]; bb = p[2];
                } else {
                    const int v = 255 - rowG[x];
                    br = bg = bb = v;
                }
                break;
            case Bg::Gray: {
                // G=255 = bg (white), G=0 = strong edge (black) — no inversion.
                const int v = rowG[x];
                br = bg = bb = v;
                break;
            }
            case Bg::GrayRed: {
                // Any edge pixel (G<255) renders as flat red so weak edges
                // (G≈245) are as visible as strong ones.
                if (rowG[x] == 255) { br = bg = bb = 255; }
                else { br = 255; bg = 0; bb = 0; }
                break;
            }
            case Bg::Prob: {
                const int r = 255 - rowR[x];
                const int b = rowR[x];
                br = r; bg = 0; bb = b;
                break;
            }
            }
            if (showIn && rowIn && rowIn[x] == 255 && rowOut[x] != 255) {
                br = 40; bg = 80; bb = 230;
            }
            if (showResult && rowOut[x] == 255) {
                br = 0; bg = 0; bb = 0;
            }
            if (fadePercent_ < 100) {
                const int target = (bgMode == Bg::Plain) ? 255 : 0;
                const int a = fadePercent_;          // 0..100
                br = (target * (100 - a) + br * a) / 100;
                bg = (target * (100 - a) + bg * a) / 100;
                bb = (target * (100 - a) + bb * a) / 100;
            }
            row[x] = qRgba(br, bg, bb, 255);
        }
    }
}

cv::Mat McViewWidget::rasterizePolygon(const std::vector<cv::Point>& verts) const
{
    cv::Mat m = cv::Mat::zeros(srcGray_.size(), CV_8UC1);
    if (verts.size() < 3) return m;
    std::vector<std::vector<cv::Point>> polys{verts};
    cv::fillPoly(m, polys, cv::Scalar(255));
    return m;
}

void McViewWidget::closePolygonAndEmit()
{
    if (polyVerts_.size() < kMinPolyVerts) return;
    polyMask_ = rasterizePolygon(polyVerts_);
    closedPolyVerts_ = polyVerts_;
    lastPolyVerts_ = polyVerts_;
    polyVerts_.clear();
    polyOpen_ = false;
    polyHoverValid_ = false;
    update();
    emit polygonFinished();
}

void McViewWidget::selectWhole()
{
    if (srcGray_.empty()) return;
    polyVerts_.clear();
    polyOpen_ = false;
    polyHoverValid_ = false;
    polyMask_ = cv::Mat(srcGray_.size(), CV_8UC1, cv::Scalar(255));
    const int W = srcGray_.cols, H = srcGray_.rows;
    closedPolyVerts_ = { {0, 0}, {W - 1, 0}, {W - 1, H - 1}, {0, H - 1} };
    previewMask_.release();
    update();
    emit polygonFinished();
}

void McViewWidget::selectNone()
{
    cancelPolygon();
}

void McViewWidget::restoreLastPolygon()
{
    if (lastPolyVerts_.size() < kMinPolyVerts || srcGray_.empty()) return;
    polyVerts_.clear();
    polyOpen_ = false;
    polyHoverValid_ = false;
    polyMask_ = rasterizePolygon(lastPolyVerts_);
    closedPolyVerts_ = lastPolyVerts_;
    previewMask_.release();
    update();
    emit polygonFinished();
}

void McViewWidget::cancelPolygon()
{
    polyVerts_.clear();
    polyOpen_ = false;
    polyHoverValid_ = false;
    polyMask_.release();
    closedPolyVerts_.clear();
    previewMask_.release();
    update();
}

void McViewWidget::applyOp(const std::vector<cv::Point>& pts, bool add)
{
    if (outResult_.empty() || pts.empty()) return;
    const uchar val = add ? 255 : 0;
    for (const auto& p : pts) {
        if (p.x < 0 || p.y < 0 || p.x >= outResult_.cols || p.y >= outResult_.rows) continue;
        outResult_.at<uchar>(p.y, p.x) = val;
    }
    rebuildVisualization();
    if (!dirty_) { dirty_ = true; emit dirtyChanged(true); }
    update();
}

void McViewWidget::computeResultBlobs(cv::Mat& blobLabels,
                                      std::vector<int>& blobSize,
                                      std::vector<int>& blobExtent) const
{
    blobLabels = cv::Mat::zeros(outResult_.size(), CV_32S);
    blobSize.assign(1, 0);
    blobExtent.assign(1, 0);
    if (outResult_.empty()) return;

    const int H = outResult_.rows, W = outResult_.cols;
    const int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    const int dx4[4] = {-1, 1, 0, 0};
    const int dy4[4] = { 0, 0,-1, 1};
    const int nN = conn8_ ? 8 : 4;
    const int* dx = conn8_ ? dx8 : dx4;
    const int* dy = conn8_ ? dy8 : dy4;

    int next = 1;
    std::queue<std::pair<int,int>> q;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (blobLabels.at<int>(y, x) != 0) continue;
            if (outResult_.at<uchar>(y, x) != 255) continue;
            const int B = next++;
            blobLabels.at<int>(y, x) = B;
            std::vector<cv::Point> comp;
            q.push({x, y});
            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                comp.emplace_back(cx, cy);
                for (int k = 0; k < nN; ++k) {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    if (blobLabels.at<int>(ny, nx) != 0) continue;
                    if (outResult_.at<uchar>(ny, nx) != 255) continue;
                    blobLabels.at<int>(ny, nx) = B;
                    q.push({nx, ny});
                }
            }
            blobSize.push_back(static_cast<int>(comp.size()));
            blobExtent.push_back(static_cast<int>(std::ceil(componentExtent(comp))));
        }
    }
}

int McViewWidget::filterCountIf(FilterMode mode, FilterAction action,
                                bool useG, int gMax, bool useR, int rMax,
                                bool useNum, int numThr,
                                bool useExt, int extThr,
                                bool useResultBlobs) const
{
    if (polyMask_.empty() || labels_.empty()) return 0;
    const int H = srcGray_.rows, W = srcGray_.cols;
    const int nL = static_cast<int>(labelSize_.size());

    const bool blobMode = useResultBlobs && action == FilterAction::Remove;
    cv::Mat blobLabels;
    std::vector<int> blobSize, blobExtent;
    if (blobMode && (useNum || useExt))
        computeResultBlobs(blobLabels, blobSize, blobExtent);

    std::vector<int> labelInMask(nL, 0);   // pixels of label inside polyMask
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowM = polyMask_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0) continue;
            if (rowM[x]) ++labelInMask[L];
        }
    }

    std::vector<uchar> eligible(nL, 0);
    for (int L = 1; L < nL; ++L) {
        if (labelInMask[L] == 0) continue;
        const bool fullyInside = labelInMask[L] == labelSize_[L];
        const bool wantInside = (mode == FilterMode::Inside);
        if (wantInside && !fullyInside) continue;
        // Add picks confident-edge segments (low G, low R = strong edge);
        // Remove picks weak/uncertain segments (high G, high R = bg-like).
        // In blob mode (Remove only) num/ext compare per result-blob, not
        // per G-segment, so they're skipped in this label-level filter.
        if (action == FilterAction::Add) {
            if (useG   && labelGray_[L]   > gMax)   continue;
            if (useR   && labelAvgR_[L]   > rMax)   continue;
            if (useNum && labelSize_[L]   < numThr) continue;
            if (useExt && labelExtent_[L] < extThr) continue;
        } else {
            if (useG && labelGray_[L] < gMax) continue;
            if (useR && labelAvgR_[L] < rMax) continue;
            if (!blobMode) {
                if (useNum && labelSize_[L]   > numThr) continue;
                if (useExt && labelExtent_[L] > extThr) continue;
            }
        }
        eligible[L] = 1;
    }

    int n = 0;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        const int* rowB = (blobMode && (useNum || useExt))
            ? blobLabels.ptr<int>(y) : nullptr;
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0 || !eligible[L]) continue;
            // Touching: take the whole eligible component, even pixels
            // outside the polygon. Inside mode is already enforced by the
            // per-label eligibility (label is fully inside the polygon).
            const bool isIn = rowOut[x] == 255;
            if (action == FilterAction::Add && !isIn) ++n;
            else if (action == FilterAction::Remove && isIn) {
                if (rowB) {
                    const int B = rowB[x];
                    if (useNum && blobSize[B]   > numThr) continue;
                    if (useExt && blobExtent[B] > extThr) continue;
                }
                ++n;
            }
        }
    }
    return n;
}

int McViewWidget::setFilterPreview(FilterMode mode, FilterAction action,
                                   bool useG, int gMax, bool useR, int rMax,
                                   bool useNum, int numThr,
                                   bool useExt, int extThr,
                                   bool useResultBlobs)
{
    if (polyMask_.empty() || labels_.empty()) {
        clearFilterPreview();
        return 0;
    }
    const int H = srcGray_.rows, W = srcGray_.cols;
    const int nL = static_cast<int>(labelSize_.size());

    const bool blobMode = useResultBlobs && action == FilterAction::Remove;
    cv::Mat blobLabels;
    std::vector<int> blobSize, blobExtent;
    if (blobMode && (useNum || useExt))
        computeResultBlobs(blobLabels, blobSize, blobExtent);

    std::vector<int> labelInMask(nL, 0);
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowM = polyMask_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0) continue;
            if (rowM[x]) ++labelInMask[L];
        }
    }
    std::vector<uchar> eligible(nL, 0);
    for (int L = 1; L < nL; ++L) {
        if (labelInMask[L] == 0) continue;
        const bool fullyInside = labelInMask[L] == labelSize_[L];
        const bool wantInside = (mode == FilterMode::Inside);
        if (wantInside && !fullyInside) continue;
        // Add picks confident-edge segments (low G, low R = strong edge);
        // Remove picks weak/uncertain segments (high G, high R = bg-like).
        // In blob mode (Remove only) num/ext compare per result-blob, not
        // per G-segment, so they're skipped in this label-level filter.
        if (action == FilterAction::Add) {
            if (useG   && labelGray_[L]   > gMax)   continue;
            if (useR   && labelAvgR_[L]   > rMax)   continue;
            if (useNum && labelSize_[L]   < numThr) continue;
            if (useExt && labelExtent_[L] < extThr) continue;
        } else {
            if (useG && labelGray_[L] < gMax) continue;
            if (useR && labelAvgR_[L] < rMax) continue;
            if (!blobMode) {
                if (useNum && labelSize_[L]   > numThr) continue;
                if (useExt && labelExtent_[L] > extThr) continue;
            }
        }
        eligible[L] = 1;
    }

    previewMask_ = cv::Mat::zeros(srcGray_.size(), CV_8UC1);
    previewIsAdd_ = (action == FilterAction::Add);
    int n = 0;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        uchar* rowP = previewMask_.ptr<uchar>(y);
        const int* rowB = (blobMode && (useNum || useExt))
            ? blobLabels.ptr<int>(y) : nullptr;
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0 || !eligible[L]) continue;
            // Touching: take the whole eligible component (see filterCountIf).
            const bool isIn = rowOut[x] == 255;
            if (action == FilterAction::Add && !isIn) { rowP[x] = 255; ++n; }
            else if (action == FilterAction::Remove && isIn) {
                if (rowB) {
                    const int B = rowB[x];
                    if (useNum && blobSize[B]   > numThr) continue;
                    if (useExt && blobExtent[B] > extThr) continue;
                }
                rowP[x] = 255; ++n;
            }
        }
    }
    update();
    return n;
}

void McViewWidget::clearFilterPreview()
{
    if (previewMask_.empty()) return;
    previewMask_.release();
    update();
}

void McViewWidget::commitFilter(FilterMode mode, FilterAction action,
                                bool useG, int gMax, bool useR, int rMax,
                                bool useNum, int numThr,
                                bool useExt, int extThr,
                                bool useResultBlobs)
{
    if (polyMask_.empty() || labels_.empty()) { cancelPolygon(); return; }
    const int H = srcGray_.rows, W = srcGray_.cols;
    const int nL = static_cast<int>(labelSize_.size());

    const bool blobMode = useResultBlobs && action == FilterAction::Remove;
    cv::Mat blobLabels;
    std::vector<int> blobSize, blobExtent;
    if (blobMode && (useNum || useExt))
        computeResultBlobs(blobLabels, blobSize, blobExtent);

    std::vector<int> labelInMask(nL, 0);
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowM = polyMask_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0) continue;
            if (rowM[x]) ++labelInMask[L];
        }
    }
    std::vector<uchar> eligible(nL, 0);
    for (int L = 1; L < nL; ++L) {
        if (labelInMask[L] == 0) continue;
        const bool fullyInside = labelInMask[L] == labelSize_[L];
        const bool wantInside = (mode == FilterMode::Inside);
        if (wantInside && !fullyInside) continue;
        // Add picks confident-edge segments (low G, low R = strong edge);
        // Remove picks weak/uncertain segments (high G, high R = bg-like).
        // In blob mode (Remove only) num/ext compare per result-blob, not
        // per G-segment, so they're skipped in this label-level filter.
        if (action == FilterAction::Add) {
            if (useG   && labelGray_[L]   > gMax)   continue;
            if (useR   && labelAvgR_[L]   > rMax)   continue;
            if (useNum && labelSize_[L]   < numThr) continue;
            if (useExt && labelExtent_[L] < extThr) continue;
        } else {
            if (useG && labelGray_[L] < gMax) continue;
            if (useR && labelAvgR_[L] < rMax) continue;
            if (!blobMode) {
                if (useNum && labelSize_[L]   > numThr) continue;
                if (useExt && labelExtent_[L] > extThr) continue;
            }
        }
        eligible[L] = 1;
    }

    std::vector<cv::Point> pts;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        const int* rowB = (blobMode && (useNum || useExt))
            ? blobLabels.ptr<int>(y) : nullptr;
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0 || !eligible[L]) continue;
            // Touching: take the whole eligible component, even pixels
            // outside the polygon. Inside mode is already enforced by the
            // per-label eligibility (label is fully inside the polygon).
            const bool isIn = rowOut[x] == 255;
            if (action == FilterAction::Add && !isIn) pts.emplace_back(x, y);
            else if (action == FilterAction::Remove && isIn) {
                if (rowB) {
                    const int B = rowB[x];
                    if (useNum && blobSize[B]   > numThr) continue;
                    if (useExt && blobExtent[B] > extThr) continue;
                }
                pts.emplace_back(x, y);
            }
        }
    }

    polyMask_.release();
    closedPolyVerts_.clear();
    previewMask_.release();
    update();
    if (pts.empty()) return;
    applyOp(pts, action == FilterAction::Add);
    emit editOp(std::move(pts), action == FilterAction::Add);
}

// View transforms -----------------------------------------------------------

QPointF McViewWidget::widgetToImage(const QPoint& p) const
{
    if (vis_.isNull()) return {};
    return {(p.x() - panOffset_.x()) / scale_,
            (p.y() - panOffset_.y()) / scale_};
}

QPointF McViewWidget::imageToWidget(const QPointF& p) const
{
    return {p.x() * scale_ + panOffset_.x(),
            p.y() * scale_ + panOffset_.y()};
}

void McViewWidget::fitToWindow()
{
    if (vis_.isNull()) return;
    const double sw = static_cast<double>(width())  / vis_.width();
    const double sh = static_cast<double>(height()) / vis_.height();
    scale_ = std::max(0.01, std::min(sw, sh));
    panOffset_ = QPointF((width()  - vis_.width()  * scale_) / 2.0,
                         (height() - vis_.height() * scale_) / 2.0);
    pendingFit_ = false;
    update();
}

void McViewWidget::zoomOneToOne()
{
    if (vis_.isNull()) return;
    scale_ = 1.0;
    panOffset_ = QPointF((width()  - vis_.width()) / 2.0,
                         (height() - vis_.height()) / 2.0);
    update();
}

void McViewWidget::applyZoomAt(const QPoint& anchor, double factor)
{
    if (vis_.isNull()) return;
    const QPointF before = widgetToImage(anchor);
    scale_ = std::clamp(scale_ * factor, 0.05, 64.0);
    panOffset_ = QPointF(anchor.x() - before.x() * scale_,
                         anchor.y() - before.y() * scale_);
    update();
}

// Events --------------------------------------------------------------------

void McViewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 40));
    if (vis_.isNull()) return;
    if (pendingFit_) fitToWindow();

    p.translate(panOffset_);
    p.scale(scale_, scale_);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(0, 0, vis_);

    // Polygon overlay.
    if (polyOpen_ && !polyVerts_.empty()) {
        QPen pen(QColor(255, 200, 0));
        pen.setCosmetic(true);
        pen.setWidth(2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        for (size_t i = 1; i < polyVerts_.size(); ++i) {
            p.drawLine(QPointF(polyVerts_[i-1].x + 0.5, polyVerts_[i-1].y + 0.5),
                       QPointF(polyVerts_[i].x   + 0.5, polyVerts_[i].y   + 0.5));
        }
        if (polyHoverValid_) {
            QPen dash = pen; dash.setStyle(Qt::DashLine); p.setPen(dash);
            const auto& last = polyVerts_.back();
            p.drawLine(QPointF(last.x + 0.5, last.y + 0.5),
                       QPointF(polyHover_.x() + 0.5, polyHover_.y() + 0.5));
        }
        p.setPen(pen);
        for (const auto& v : polyVerts_) {
            p.drawEllipse(QPointF(v.x + 0.5, v.y + 0.5), 2.0 / scale_, 2.0 / scale_);
        }
    } else if (!polyMask_.empty()) {
        // Preview pixels (cyan = will be added, magenta = will be removed).
        if (!previewMask_.empty()) {
            QImage overlay(previewMask_.cols, previewMask_.rows, QImage::Format_ARGB32);
            overlay.fill(0);
            const QRgb prevCol = previewIsAdd_
                ? qRgba(0, 230, 230, 230)
                : qRgba(230, 0, 230, 230);
            for (int y = 0; y < previewMask_.rows; ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
                const uchar* pv = previewMask_.ptr<uchar>(y);
                for (int x = 0; x < previewMask_.cols; ++x)
                    if (pv[x]) row[x] = prevCol;
            }
            p.drawImage(0, 0, overlay);
        }
        // Polygon outline + vertex dots.
        if (!closedPolyVerts_.empty()) {
            QPen pen(QColor(255, 200, 0));
            pen.setCosmetic(true);
            pen.setWidth(2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            for (size_t i = 0; i < closedPolyVerts_.size(); ++i) {
                const auto& a = closedPolyVerts_[i];
                const auto& b = closedPolyVerts_[(i + 1) % closedPolyVerts_.size()];
                p.drawLine(QPointF(a.x + 0.5, a.y + 0.5),
                           QPointF(b.x + 0.5, b.y + 0.5));
            }
            for (const auto& v : closedPolyVerts_)
                p.drawEllipse(QPointF(v.x + 0.5, v.y + 0.5),
                              2.0 / scale_, 2.0 / scale_);
        }
    }
}

void McViewWidget::resizeEvent(QResizeEvent*)
{
    if (pendingFit_) fitToWindow();
}

void McViewWidget::wheelEvent(QWheelEvent* e)
{
    const double f = std::pow(1.0015, e->angleDelta().y());
    applyZoomAt(e->position().toPoint(), f);
}

void McViewWidget::mousePressEvent(QMouseEvent* e)
{
    if (vis_.isNull()) return;
    if (e->button() == Qt::RightButton) {
        emit contextMenuRequested(e->globalPosition().toPoint());
        return;
    }
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        if (editLocked_) { emit editBlocked(); return; }
        const QPointF ip = widgetToImage(e->pos());
        const int ix = static_cast<int>(std::floor(ip.x()));
        const int iy = static_cast<int>(std::floor(ip.y()));
        if (ix < 0 || iy < 0 || ix >= srcGray_.cols || iy >= srcGray_.rows) return;
        const Bg bg = presets_[presetIndex_].bg;
        const bool grayShown = (bg == Bg::Gray || bg == Bg::GrayRed);
        const EditPick pick = pickEditTargetNear(ix, iy, 6, grayShown);
        if (pick.inResult) {
            const int L = pick.label > 0 ? pick.label
                                          : pickEraseLabelNear(ix, iy, 6);
            if (L > 0) eraseLabel(L);
        } else if (pick.label > 0 && grayShown) {
            penLabel(pick.label);
        }
        return;
    }
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)) {
        if (editLocked_) { emit editBlocked(); return; }
        const QPointF ip = widgetToImage(e->pos());
        const int ix = static_cast<int>(std::floor(ip.x()));
        const int iy = static_cast<int>(std::floor(ip.y()));
        if (ix < 0 || iy < 0 || ix >= srcGray_.cols || iy >= srcGray_.rows) return;
        if (!polyOpen_) {
            polyMask_.release();
            closedPolyVerts_.clear();
            polyVerts_.clear();
            polyOpen_ = true;
        }
        polyVerts_.emplace_back(ix, iy);
        update();
        return;
    }
    if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton) {
        panning_ = true;
        lastMousePos_ = e->pos();
        panOffsetAtPress_ = panOffset_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
}

void McViewWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && polyOpen_ &&
        polyVerts_.size() >= kMinPolyVerts) {
        closePolygonAndEmit();
    }
}

void McViewWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (panning_) {
        const QPoint d = e->pos() - lastMousePos_;
        panOffset_ = panOffsetAtPress_ + QPointF(d);
        update();
        return;
    }
    updateCursorForMods(e->modifiers());
    if (polyOpen_) {
        const QPointF ip = widgetToImage(e->pos());
        polyHover_ = QPoint(static_cast<int>(std::floor(ip.x())),
                            static_cast<int>(std::floor(ip.y())));
        polyHoverValid_ = true;
        update();
    }
    emitHud(e->pos());
}

void McViewWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (panning_ && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        panning_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void McViewWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (polyOpen_ || !polyMask_.empty()) { cancelPolygon(); return; }
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
        polyOpen_ && polyVerts_.size() >= kMinPolyVerts) {
        closePolygonAndEmit();
        return;
    }
    if (e->key() == Qt::Key_Control)
        updateCursorForMods(e->modifiers() | Qt::ControlModifier);
    QWidget::keyPressEvent(e);
}

void McViewWidget::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Control)
        updateCursorForMods(e->modifiers() & ~Qt::ControlModifier);
    QWidget::keyReleaseEvent(e);
}

void McViewWidget::enterEvent(QEnterEvent*)
{
    updateCursorForMods(QApplication::keyboardModifiers());
}

void McViewWidget::leaveEvent(QEvent*)
{
    polyHoverValid_ = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void McViewWidget::emitHud(const QPoint& widgetPos)
{
    if (srcGray_.empty()) return;
    const QPointF ip = widgetToImage(widgetPos);
    const int ix = static_cast<int>(std::floor(ip.x()));
    const int iy = static_cast<int>(std::floor(ip.y()));
    if (ix < 0 || iy < 0 || ix >= srcGray_.cols || iy >= srcGray_.rows) {
        emit hudUpdate({});
        return;
    }
    const int g = srcGray_.at<uchar>(iy, ix);
    const int r = probR_.at<uchar>(iy, ix);
    const int L = labels_.empty() ? 0 : labels_.at<int>(iy, ix);
    QString seg;
    if (L > 0)
        seg = QString("  seg #%1 (size %2, avgR %3)").arg(L)
                  .arg(labelSize_[L]).arg(labelAvgR_[L]);
    emit hudUpdate(QString("x=%1 y=%2  G=%3  R=%4%5")
                       .arg(ix).arg(iy).arg(g).arg(r).arg(seg));
}
