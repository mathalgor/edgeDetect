#include "OcViewWidget.h"
#include "CursorUtils.h"
#include "EditColors.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <climits>
#include <vector>

OcViewWidget::OcViewWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    buildDefaultPresets();
}

void OcViewWidget::setData(const cv::Mat& srcGray,
                           const cv::Mat& o1FileFmt,
                           const cv::Mat& o2FileFmt,
                           const cv::Mat& outFileFmt,
                           const cv::Mat& original)
{
    src_ = srcGray.clone();
    auto invert = [&](const cv::Mat& fileFmt) -> cv::Mat {
        cv::Mat m;
        if (fileFmt.empty()) {
            m = cv::Mat::zeros(src_.size(), CV_8UC1);
        } else {
            cv::Mat r = fileFmt;
            if (r.size() != src_.size()) cv::resize(r, r, src_.size(), 0, 0, cv::INTER_NEAREST);
            cv::bitwise_not(r, m);
            cv::threshold(m, m, 127, 255, cv::THRESH_BINARY);
        }
        return m;
    };
    o1_ = invert(o1FileFmt);
    o2_ = invert(o2FileFmt);
    if (outFileFmt.empty()) {
        // No saved result yet — output starts from the intersection of o1 and o2.
        cv::bitwise_and(o1_, o2_, out_);
    } else {
        out_ = invert(outFileFmt);
    }

    // Build original as RGBA at src_ size; empty if not provided.
    originalRgba_.release();
    if (!original.empty()) {
        cv::Mat o = original;
        if (o.size() != src_.size()) {
            cv::Mat r;
            cv::resize(o, r, src_.size(), 0, 0, cv::INTER_AREA);
            o = r;
        }
        if (o.channels() == 1)      cv::cvtColor(o, originalRgba_, cv::COLOR_GRAY2RGBA);
        else if (o.channels() == 3) cv::cvtColor(o, originalRgba_, cv::COLOR_BGR2RGBA);
        else if (o.channels() == 4) cv::cvtColor(o, originalRgba_, cv::COLOR_BGRA2RGBA);
    }

    dirty_ = false;
    emit dirtyChanged(false);
    pendingFit_ = true;
    analyzeComponents();
    rebuildVisualization();
    if (width() > 0 && height() > 0) fitToWindow();
    update();
}

void OcViewWidget::analyzeComponents()
{
    labels_.release();
    labelSize_.clear();
    labelValue_.clear();
    if (src_.empty()) return;

    const int rows = src_.rows, cols = src_.cols;
    labels_ = cv::Mat::zeros(src_.size(), CV_32S);
    cv::Mat visited = cv::Mat::zeros(src_.size(), CV_8UC1);
    labelSize_.push_back(0);    // [0] = background
    labelValue_.push_back(255); // sentinel

    static constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    static constexpr int dx4[4] = { 0, -1, 1, 0};
    static constexpr int dy4[4] = {-1,  0, 0, 1};
    const int nNb = conn8_ ? 8 : 4;
    const int* dxN = conn8_ ? dx8 : dx4;
    const int* dyN = conn8_ ? dy8 : dy4;

    std::vector<cv::Point> comp, stack;
    int nextLabel = 1;
    for (int y0 = 0; y0 < rows; ++y0) {
        const uchar* srcRow = src_.ptr<uchar>(y0);
        const uchar* visRow = visited.ptr<uchar>(y0);
        for (int x0 = 0; x0 < cols; ++x0) {
            if (visRow[x0]) continue;
            const uchar target = srcRow[x0];
            if (target >= 255) continue;
            comp.clear();
            stack.clear();
            stack.emplace_back(x0, y0);
            visited.at<uchar>(y0, x0) = 1;
            while (!stack.empty()) {
                const cv::Point p = stack.back();
                stack.pop_back();
                comp.push_back(p);
                for (int k = 0; k < nNb; ++k) {
                    const int nx = p.x + dxN[k], ny = p.y + dyN[k];
                    if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
                    if (visited.at<uchar>(ny, nx)) continue;
                    if (src_.at<uchar>(ny, nx) != target) continue;
                    visited.at<uchar>(ny, nx) = 1;
                    stack.emplace_back(nx, ny);
                }
            }
            const int L = nextLabel++;
            labelSize_.push_back(int(comp.size()));
            labelValue_.push_back(target);
            for (const auto& q : comp) labels_.at<int>(q) = L;
        }
    }
}

bool OcViewWidget::grayCandidateAvailable() const
{
    if (!allowGrayEdit_) return false;
    if (presetIndex_ < 0 || presetIndex_ >= int(presets_.size())) return false;
    return presets_[presetIndex_].bg == ViewPreset::Background::GraySource;
}

std::vector<cv::Point> OcViewWidget::stripCorners(
        const QPoint& p1, const QPoint& p2, const QPoint& cursor) const
{
    const double ax = p2.x() - p1.x();
    const double ay = p2.y() - p1.y();
    const double len = std::hypot(ax, ay);
    std::vector<cv::Point> out;
    if (len < 1e-6) return out;
    const double ux = ax / len, uy = ay / len;
    const double px = -uy,      py = ux;
    const double cx = cursor.x() - p1.x();
    const double cy = cursor.y() - p1.y();
    const double u  = cx * px + cy * py;
    const double wx = px * u, wy = py * u;
    out.reserve(4);
    out.emplace_back(p1.x(), p1.y());
    out.emplace_back(p2.x(), p2.y());
    out.emplace_back(int(std::round(p2.x() + wx)), int(std::round(p2.y() + wy)));
    out.emplace_back(int(std::round(p1.x() + wx)), int(std::round(p1.y() + wy)));
    return out;
}

void OcViewWidget::finishRectFromCurrent()
{
    if (src_.empty()) return;
    const int x0 = std::max(0, std::min(rectStart_.x(), rectEnd_.x()));
    const int y0 = std::max(0, std::min(rectStart_.y(), rectEnd_.y()));
    const int x1 = std::min(src_.cols - 1, std::max(rectStart_.x(), rectEnd_.x()));
    const int y1 = std::min(src_.rows - 1, std::max(rectStart_.y(), rectEnd_.y()));
    if (x1 < x0 || y1 < y0) return;
    lastPolyMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    cv::rectangle(lastPolyMask_, cv::Point(x0, y0), cv::Point(x1, y1),
                  cv::Scalar(255), cv::FILLED);
    rectDragging_ = false;
    update();
    emit rectSelectionFinished();
}

void OcViewWidget::finishStrip(const QPoint& widthRef)
{
    if (src_.empty()) return;
    const auto corners = stripCorners(stripP1_, stripP2_, widthRef);
    stripPhase_ = StripPhase::None;
    if (corners.size() != 4) { update(); return; }
    lastPolyMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> polys{corners};
    cv::fillPoly(lastPolyMask_, polys, cv::Scalar(255));
    update();
    emit rectSelectionFinished();
}

void OcViewWidget::cancelStripInProgress()
{
    stripPhase_ = StripPhase::None;
    update();
}

void OcViewWidget::cancelRectSelection()
{
    lastPolyMask_.release();
    previewOrange_.release();
    previewYellow_.release();
    previewImage_ = {};
    previewActive_ = false;
    rectDragging_ = false;
    stripPhase_ = StripPhase::None;
    update();
}

void OcViewWidget::setRectPreview(int threshold, CandMode mode, CandColor color)
{
    if (lastPolyMask_.empty() || labels_.empty() || src_.empty()) {
        previewActive_ = false;
        previewImage_ = {};
        update();
        return;
    }
    const int rows = src_.rows, cols = src_.cols;

    // Count pixels of each label inside the polygon.
    std::vector<int> countIn(labelSize_.size(), 0);
    for (int y = 0; y < rows; ++y) {
        const uchar* pm = lastPolyMask_.ptr<uchar>(y);
        const int*   lr = labels_.ptr<int>(y);
        for (int x = 0; x < cols; ++x) {
            if (!pm[x]) continue;
            const int L = lr[x];
            if (L > 0 && L < int(countIn.size())) ++countIn[L];
        }
    }
    // includeBySpatial[L] = 1 if eligible by Touching/Inside (no threshold yet).
    std::vector<uchar> includeBySpatial(labelSize_.size(), 0);
    for (int L = 1; L < int(labelSize_.size()); ++L) {
        if (countIn[L] == 0) continue;
        if (mode == CandMode::Touching || countIn[L] == labelSize_[L]) {
            includeBySpatial[L] = 1;
        }
    }
    previewOrange_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    previewYellow_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    for (int y = 0; y < rows; ++y) {
        const int*   lr = labels_.ptr<int>(y);
        const uchar* o1 = o1_.ptr<uchar>(y);
        const uchar* o2 = o2_.ptr<uchar>(y);
        const uchar* oo = out_.ptr<uchar>(y);
        uchar* orng = previewOrange_.ptr<uchar>(y);
        uchar* yelw = previewYellow_.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            const int L = lr[x];
            if (L <= 0 || !includeBySpatial[L]) continue;
            if (oo[x]) continue;
            bool match = false;
            switch (color) {
                case CandColor::Red:   match =  o2[x] && !o1[x]; break;
                case CandColor::Green: match =  o1[x] && !o2[x]; break;
                case CandColor::Gray:  match = !o1[x] && !o2[x]; break;
            }
            if (!match) continue;
            if (labelValue_[L] <= threshold) orng[x] = 255;
            else                              yelw[x] = 255;
        }
    }
    // Compose RGBA image.
    cv::Mat rgba(rows, cols, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    const QColor co = edit_colors::candidateOrange();
    const QColor cy = edit_colors::candidateYellow();
    const cv::Vec4b vo(co.red(), co.green(), co.blue(), co.alpha());
    const cv::Vec4b vy(cy.red(), cy.green(), cy.blue(), cy.alpha());
    for (int y = 0; y < rows; ++y) {
        const uchar* orng = previewOrange_.ptr<uchar>(y);
        const uchar* yelw = previewYellow_.ptr<uchar>(y);
        cv::Vec4b* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            if      (orng[x]) dr[x] = vo;
            else if (yelw[x]) dr[x] = vy;
        }
    }
    previewImage_ = QImage(rgba.data, cols, rows, int(rgba.step),
                           QImage::Format_RGBA8888).copy();
    previewActive_ = true;
    update();
}

void OcViewWidget::commitRectSelection(int threshold, CandMode mode, CandColor color)
{
    if (lastPolyMask_.empty() || labels_.empty() || src_.empty()) {
        cancelRectSelection();
        return;
    }
    const int rows = src_.rows, cols = src_.cols;

    // Count how many pixels of each label fall inside the polygon.
    std::vector<int> countIn(labelSize_.size(), 0);
    for (int y = 0; y < rows; ++y) {
        const uchar* pm = lastPolyMask_.ptr<uchar>(y);
        const int*   lr = labels_.ptr<int>(y);
        for (int x = 0; x < cols; ++x) {
            if (!pm[x]) continue;
            const int L = lr[x];
            if (L > 0 && L < int(countIn.size())) ++countIn[L];
        }
    }
    // Select labels by spatial mode + value threshold.
    std::vector<uchar> include(labelSize_.size(), 0);
    for (int L = 1; L < int(labelSize_.size()); ++L) {
        if (countIn[L] == 0) continue;
        if (mode == CandMode::Touching) {
            // touching = any pixel in the polygon
        } else {
            if (countIn[L] != labelSize_[L]) continue;
        }
        if (labelValue_[L] > threshold) continue;
        include[L] = 1;
    }
    // Walk pixels of included labels, filter by color, add to out_.
    std::vector<cv::Point> changed;
    for (int y = 0; y < rows; ++y) {
        const int*   lr = labels_.ptr<int>(y);
        const uchar* o1 = o1_.ptr<uchar>(y);
        const uchar* o2 = o2_.ptr<uchar>(y);
        uchar*       oo = out_.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            const int L = lr[x];
            if (L <= 0 || !include[L]) continue;
            if (oo[x]) continue;       // already in result
            bool match = false;
            switch (color) {
                case CandColor::Red:   match =  o2[x] && !o1[x]; break;
                case CandColor::Green: match =  o1[x] && !o2[x]; break;
                case CandColor::Gray:  match = !o1[x] && !o2[x]; break;
            }
            if (!match) continue;
            oo[x] = 255;
            changed.emplace_back(x, y);
        }
    }
    lastPolyMask_.release();
    previewOrange_.release();
    previewYellow_.release();
    previewImage_ = {};
    previewActive_ = false;
    if (!changed.empty()) {
        updateVisualizationAt(changed);
        if (!dirty_) { dirty_ = true; emit dirtyChanged(true); }
        emit editOp(changed, /*add=*/true);
    }
    update();
}

void OcViewWidget::setPresetIndex(int i)
{
    if (i < 0 || i >= int(presets_.size()) || i == presetIndex_) return;
    prevPresetIndex_ = presetIndex_;
    presetIndex_ = i;
    rebuildVisualization();
    update();
    emit presetChanged(presetIndex_);
}

void OcViewWidget::swapWithPrevPreset()
{
    if (prevPresetIndex_ == presetIndex_) return;
    setPresetIndex(prevPresetIndex_);
}

void OcViewWidget::buildDefaultPresets()
{
    presets_.clear();
    auto add = [&](const char* name, ViewPreset::Background bg,
                   QColor c0, QColor c1, QColor c2, QColor c3,
                   QColor c4, QColor c5, QColor c6, QColor c7) {
        ViewPreset p;
        p.name = QString::fromUtf8(name);
        p.bg = bg;
        p.cells[0]=c0; p.cells[1]=c1; p.cells[2]=c2; p.cells[3]=c3;
        p.cells[4]=c4; p.cells[5]=c5; p.cells[6]=c6; p.cells[7]=c7;
        presets_.push_back(std::move(p));
    };
    const QColor T(0, 0, 0, 0);  // transparent
    const QColor BLK  (0,   0,   0,   255);
    const QColor RED  (220, 0,   0,   255);
    const QColor GRN  (0,   180, 0,   255);
    const QColor YEL  (180, 180, 0,   220);
    const QColor RSEM (220, 0,   0,   180);

    // Cell index = (in1<<2)|(in2<<1)|out
    //              0      1      2      3      4      5      6      7
    add("Standard",                ViewPreset::Background::White,
        T,     BLK,   RED,   BLK,   GRN,   BLK,   YEL,   BLK);
    add("Original + result red",   ViewPreset::Background::Original,
        T,     RSEM,  T,     RSEM,  T,     RSEM,  T,     RSEM);
    add("Original + diff",         ViewPreset::Background::Original,
        T,     T,     RED,   RED,   GRN,   GRN,   YEL,   YEL);
    add("Gray source + diff",      ViewPreset::Background::GraySource,
        T,     T,     RED,   RED,   GRN,   GRN,   YEL,   YEL);
    add("Result only",             ViewPreset::Background::White,
        T,     BLK,   T,     BLK,   T,     BLK,   T,     BLK);
    // Result + the non-accepted candidates from one outline. Useful when one
    // candidate is much smaller but consistently better — you can see in
    // isolation what it would add.
    add("Result + outline 1 (green)", ViewPreset::Background::White,
        T,     BLK,   T,     BLK,   GRN,   BLK,   GRN,   BLK);
    add("Result + outline 2 (red)",   ViewPreset::Background::White,
        T,     BLK,   RED,   BLK,   T,     BLK,   RED,   BLK);
    // Standard palette but over the gray multi-canny background. This is
    // the only preset that supports "click on gray" advanced editing,
    // since only here can the user actually see the gray edge pixels.
    add("Gray + result + diff",     ViewPreset::Background::GraySource,
        T,     BLK,   RED,   BLK,   GRN,   BLK,   YEL,   BLK);
}

void OcViewWidget::setConn8(bool on)
{
    conn8_ = on;
}

cv::Mat OcViewWidget::outputFileFmt() const
{
    if (out_.empty()) return {};
    cv::Mat r;
    cv::bitwise_not(out_, r);  // → 0=line, 255=bg
    return r;
}

int OcViewWidget::colorAt(int x, int y) const
{
    const bool in1  = o1_.at<uchar>(y, x);
    const bool in2  = o2_.at<uchar>(y, x);
    const bool inOut = out_.at<uchar>(y, x);
    if (inOut) return 4;             // black
    if (in1 && in2) return 3;        // yellow (was common, deselected)
    if (in1) return 1;               // green (only O1)
    if (in2) return 2;               // red   (only O2)
    return 0;                        // white (none)
}

int OcViewWidget::cellAt(int x, int y) const
{
    const int in1 = o1_.at<uchar>(y, x) ? 1 : 0;
    const int in2 = o2_.at<uchar>(y, x) ? 1 : 0;
    const int out = out_.at<uchar>(y, x) ? 1 : 0;
    return (in1 << 2) | (in2 << 1) | out;
}

QRgb OcViewWidget::composePixel(int x, int y) const
{
    const ViewPreset& p = presets_[presetIndex_];
    // Background pixel.
    int br = 255, bg = 255, bb = 255;
    switch (p.bg) {
        case ViewPreset::Background::White:
            br = bg = bb = 255; break;
        case ViewPreset::Background::Black:
            br = bg = bb = 0;   break;
        case ViewPreset::Background::GraySource: {
            const uchar g = src_.at<uchar>(y, x);
            br = bg = bb = g;   break;
        }
        case ViewPreset::Background::Original:
            if (!originalRgba_.empty()) {
                const cv::Vec4b& v = originalRgba_.at<cv::Vec4b>(y, x);
                br = v[0]; bg = v[1]; bb = v[2];
            } else {
                const uchar g = src_.at<uchar>(y, x);
                br = bg = bb = g;
            }
            break;
    }
    // Foreground from cell table.
    const QColor& fg = p.cells[cellAt(x, y)];
    const int fa = fg.alpha();
    if (fa == 0) return qRgba(br, bg, bb, 255);
    const int fr = fg.red(), fgr = fg.green(), fbl = fg.blue();
    const int r = (fr * fa + br * (255 - fa)) / 255;
    const int g = (fgr * fa + bg * (255 - fa)) / 255;
    const int b = (fbl * fa + bb * (255 - fa)) / 255;
    return qRgba(r, g, b, 255);
}

void OcViewWidget::rebuildVisualization()
{
    if (src_.empty()) { vis_ = {}; return; }
    const int rows = src_.rows, cols = src_.cols;
    QImage img(cols, rows, QImage::Format_ARGB32);
    for (int y = 0; y < rows; ++y) {
        QRgb* dr = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < cols; ++x) {
            dr[x] = composePixel(x, y);
        }
    }
    vis_ = img;
}

void OcViewWidget::updateVisualizationAt(const std::vector<cv::Point>& pts)
{
    if (vis_.isNull()) return;
    for (const auto& p : pts) {
        vis_.setPixel(p.x, p.y, composePixel(p.x, p.y));
    }
}

// flood fill over pixels with color == wantedColor (1,2,3,4 colors), same-value gray, conn8/4
void OcViewWidget::floodSegment(int x0, int y0, int wantedColor,
                                std::vector<cv::Point>& out) const
{
    out.clear();
    if (src_.empty()) return;
    const int rows = src_.rows, cols = src_.cols;
    const uchar target = src_.at<uchar>(y0, x0);
    cv::Mat seen = cv::Mat::zeros(src_.size(), CV_8UC1);
    static constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    static constexpr int dx4[4] = { 0,-1, 1, 0};
    static constexpr int dy4[4] = {-1, 0, 0, 1};
    const int nNb = conn8_ ? 8 : 4;
    const int* dxN = conn8_ ? dx8 : dx4;
    const int* dyN = conn8_ ? dy8 : dy4;
    std::vector<cv::Point> stack;
    stack.emplace_back(x0, y0);
    seen.at<uchar>(y0, x0) = 1;
    while (!stack.empty()) {
        const cv::Point p = stack.back();
        stack.pop_back();
        if (colorAt(p.x, p.y) == wantedColor) out.push_back(p);
        for (int k = 0; k < nNb; ++k) {
            const int nx = p.x + dxN[k], ny = p.y + dyN[k];
            if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
            if (seen.at<uchar>(ny, nx)) continue;
            if (src_.at<uchar>(ny, nx) != target) continue;
            seen.at<uchar>(ny, nx) = 1;
            stack.emplace_back(nx, ny);
        }
    }
}

void OcViewWidget::floodAnyColor(int x0, int y0, std::vector<cv::Point>& out) const
{
    out.clear();
    if (src_.empty()) return;
    const int rows = src_.rows, cols = src_.cols;
    const uchar target = src_.at<uchar>(y0, x0);
    cv::Mat seen = cv::Mat::zeros(src_.size(), CV_8UC1);
    static constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    static constexpr int dx4[4] = { 0,-1, 1, 0};
    static constexpr int dy4[4] = {-1, 0, 0, 1};
    const int nNb = conn8_ ? 8 : 4;
    const int* dxN = conn8_ ? dx8 : dx4;
    const int* dyN = conn8_ ? dy8 : dy4;
    std::vector<cv::Point> stack;
    stack.emplace_back(x0, y0);
    seen.at<uchar>(y0, x0) = 1;
    while (!stack.empty()) {
        const cv::Point p = stack.back();
        stack.pop_back();
        out.push_back(p);
        for (int k = 0; k < nNb; ++k) {
            const int nx = p.x + dxN[k], ny = p.y + dyN[k];
            if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
            if (seen.at<uchar>(ny, nx)) continue;
            if (src_.at<uchar>(ny, nx) != target) continue;
            seen.at<uchar>(ny, nx) = 1;
            stack.emplace_back(nx, ny);
        }
    }
}

void OcViewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 40));
    if (vis_.isNull()) return;
    QRectF dst(panOffset_.x(), panOffset_.y(),
               vis_.width() * scale_, vis_.height() * scale_);
    p.setRenderHint(QPainter::SmoothPixmapTransform, !panning_ && scale_ < 1.0);
    p.drawImage(dst, vis_);

    // Live candidate preview overlay (orange = will be added, yellow =
    // eligible but threshold rejects). Same colors as cannyToOutline.
    if (previewActive_ && !previewImage_.isNull()) {
        p.drawImage(dst, previewImage_);
    }

    // Rubber-band rect (during drag).
    if (rectDragging_) {
        const QPointF a = imageToWidget(QPointF(rectStart_.x(), rectStart_.y()));
        const QPointF b = imageToWidget(QPointF(rectEnd_.x() + 1, rectEnd_.y() + 1));
        QRectF r(a, b);
        p.setPen(QPen(edit_colors::rubberBand(), 1, Qt::DashLine));
        p.setBrush(edit_colors::rubberBandFill());
        p.drawRect(r.normalized());
    }
    // Oriented strip (P1 set / P2 set).
    if (stripPhase_ == StripPhase::P1Set || stripPhase_ == StripPhase::P2Set) {
        const QPointF a = imageToWidget(QPointF(stripP1_.x() + 0.5, stripP1_.y() + 0.5));
        const QPointF b = (stripPhase_ == StripPhase::P1Set)
            ? imageToWidget(QPointF(stripCursor_.x() + 0.5, stripCursor_.y() + 0.5))
            : imageToWidget(QPointF(stripP2_.x() + 0.5, stripP2_.y() + 0.5));
        p.setPen(QPen(edit_colors::rubberBand(), 1, Qt::DashLine));
        p.drawLine(a, b);
        if (stripPhase_ == StripPhase::P2Set) {
            const auto corners = stripCorners(stripP1_, stripP2_, stripCursor_);
            if (corners.size() == 4) {
                QPolygonF poly;
                for (const auto& c : corners) {
                    poly << imageToWidget(QPointF(c.x + 0.5, c.y + 0.5));
                }
                p.setBrush(edit_colors::rubberBandFill());
                p.drawPolygon(poly);
            }
        }
    }
}

void OcViewWidget::resizeEvent(QResizeEvent*)
{
    if (vis_.isNull()) return;
    if (pendingFit_) fitToWindow();
}

QPointF OcViewWidget::widgetToImage(const QPoint& p) const
{
    return QPointF((p.x() - panOffset_.x()) / scale_,
                   (p.y() - panOffset_.y()) / scale_);
}

QPointF OcViewWidget::imageToWidget(const QPointF& p) const
{
    return QPointF(p.x() * scale_ + panOffset_.x(),
                   p.y() * scale_ + panOffset_.y());
}

void OcViewWidget::applyZoomAt(const QPoint& anchor, double factor)
{
    if (vis_.isNull()) return;
    const QPointF imgPt = widgetToImage(anchor);
    scale_ = std::clamp(scale_ * factor, 0.05, 64.0);
    panOffset_ = QPointF(anchor.x() - imgPt.x() * scale_,
                         anchor.y() - imgPt.y() * scale_);
    update();
}

void OcViewWidget::wheelEvent(QWheelEvent* e)
{
    const double f = (e->angleDelta().y() > 0) ? 1.2 : (1.0 / 1.2);
    applyZoomAt(e->position().toPoint(), f);
}

void OcViewWidget::fitToWindow()
{
    if (vis_.isNull()) return;
    if (width() < 50 || height() < 50) { pendingFit_ = true; return; }
    const double sx = double(width())  / vis_.width();
    const double sy = double(height()) / vis_.height();
    scale_ = std::min(sx, sy);
    panOffset_ = QPointF(
        (width()  - vis_.width()  * scale_) * 0.5,
        (height() - vis_.height() * scale_) * 0.5);
    pendingFit_ = false;
    update();
}

void OcViewWidget::zoomOneToOne()
{
    if (vis_.isNull()) return;
    const QPoint c(width()/2, height()/2);
    const QPointF imgC = widgetToImage(c);
    scale_ = 1.0;
    panOffset_ = QPointF(c.x() - imgC.x() * scale_,
                         c.y() - imgC.y() * scale_);
    update();
}

void OcViewWidget::mousePressEvent(QMouseEvent* e)
{
    setFocus();

    // Right-click cancels an in-progress strip / pending rect.
    if (e->button() == Qt::RightButton) {
        if (stripPhase_ != StripPhase::None) { cancelStripInProgress(); return; }
        if (!lastPolyMask_.empty()) { cancelRectSelection(); return; }
    }
    if (e->button() != Qt::LeftButton) return;
    if (vis_.isNull()) return;

    const auto mods = e->modifiers();
    const QPointF ip = widgetToImage(e->pos());
    const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));

    // Mid-strip clicks always advance phase, even without Shift.
    if (stripPhase_ == StripPhase::P1Set || stripPhase_ == StripPhase::P2Set) {
        if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) return;
        if (stripPhase_ == StripPhase::P1Set) {
            stripP2_ = QPoint(ix, iy);
            stripCursor_ = stripP2_;
            stripPhase_ = StripPhase::P2Set;
            update();
        } else {
            finishStrip(QPoint(ix, iy));
        }
        return;
    }

    // Shift starts a tentative rect/strip selection (only when editable).
    if (mods & Qt::ShiftModifier) {
        const ViewPreset& preset = presets_[presetIndex_];
        if (!preset.isEditable()) return;
        if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) return;
        cancelRectSelection();
        stripPhase_ = StripPhase::Tentative;
        stripP1_ = QPoint(ix, iy);
        stripCursor_ = stripP1_;
        stripPressWidget_ = e->pos();
        rectStart_ = QPoint(ix, iy);
        rectEnd_ = rectStart_;
        rectDragging_ = false;
        update();
        return;
    }

    panning_ = true;
    lastMousePos_ = e->pos();
    pressPos_ = e->pos();
    panOffsetAtPress_ = panOffset_;
    setCursor(Qt::ClosedHandCursor);
}

void OcViewWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (panning_) {
        const QPoint d = e->pos() - lastMousePos_;
        panOffset_ += QPointF(d);
        lastMousePos_ = e->pos();
        update();
        emitHud(e->pos());
        return;
    }
    // Tentative phase: > 4 widget pixels of drag flips us into rect-drag.
    if (stripPhase_ == StripPhase::Tentative) {
        const QPoint d = e->pos() - stripPressWidget_;
        if (d.x()*d.x() + d.y()*d.y() > 16) {
            rectDragging_ = true;
            stripPhase_ = StripPhase::None;
        }
    }
    if (rectDragging_) {
        const QPointF ip = widgetToImage(e->pos());
        rectEnd_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        update();
    } else if (stripPhase_ == StripPhase::P1Set || stripPhase_ == StripPhase::P2Set) {
        const QPointF ip = widgetToImage(e->pos());
        stripCursor_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        update();
    } else {
        updateCursorForMods(e->modifiers());
    }
    emitHud(e->pos());
}

void OcViewWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;

    // Finish a rect drag.
    if (rectDragging_) {
        rectDragging_ = false;
        finishRectFromCurrent();
        return;
    }
    // Tentative with no drag = first click of an oriented strip.
    if (stripPhase_ == StripPhase::Tentative) {
        stripPhase_ = StripPhase::P1Set;
        update();
        return;
    }

    panning_ = false;
    updateCursorForMods(e->modifiers());
    // click without movement (>3 px = pan)
    const QPoint d = e->pos() - pressPos_;
    if (d.x()*d.x() + d.y()*d.y() > 9) { update(); return; }

    // Edits require Ctrl. A bare click is purely for panning and must
    // never modify the result — too easy to fire accidentally otherwise.
    if (!(e->modifiers() & Qt::ControlModifier)) return;

    if (vis_.isNull()) return;
    const ViewPreset& preset = presets_[presetIndex_];
    if (!preset.isEditable()) return;   // current preset is display-only
    const QPointF ip = widgetToImage(e->pos());
    int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
    if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) return;

    // If the click landed on white background, snap to the nearest seedable
    // pixel within a radius of 8 widget pixels (in image pixels at current
    // zoom). In a GraySource preset, gray-edge pixels (src<255) also count
    // as candidates so the gray-edit branch can be reached via the tolerance
    // ring instead of jumping to a stray outline pixel.
    if (colorAt(ix, iy) == 0) {
        const bool includeGray =
            (preset.bg == ViewPreset::Background::GraySource);
        const int r = std::max(1, int(std::round(8.0 / std::max(scale_, 0.01))));
        const cv::Point near = pickClosestNear(ix, iy, r, includeGray);
        if (near.x < 0) return;
        ix = near.x; iy = near.y;
    }

    const int c = colorAt(ix, iy);
    if (c == 0) {
        // Cell 0 — no outline pixel here. Offer the "click on gray"
        // advanced edit only when the preset uses the gray source as its
        // background (so the user actually sees what they're clicking).
        if (preset.bg != ViewPreset::Background::GraySource) return;
        if (src_.empty() || src_.at<uchar>(iy, ix) >= 255) return;
        if (!allowGrayEdit_) { emit grayEditRequested(ix, iy); return; }
        performGrayEditAt(ix, iy);
        return;
    }
    std::vector<cv::Point> pts;
    std::vector<cv::Point> changed;
    const bool add = (c != 4);
    if (c == 4) {
        // black → deselect the WHOLE same-value segment, but only pixels
        // currently in out_ actually change.
        floodAnyColor(ix, iy, pts);
        changed.reserve(pts.size());
        for (const auto& p : pts) {
            if (out_.at<uchar>(p.y, p.x)) {
                out_.at<uchar>(p.y, p.x) = 0;
                changed.push_back(p);
            }
        }
    } else {
        // green/red/yellow → add segment-pixels of the same color. All
        // such pixels are currently out_=0 (colorAt returns 4 if inOut),
        // so every flooded point is a real change.
        floodSegment(ix, iy, c, pts);
        changed = pts;
        for (const auto& p : changed) out_.at<uchar>(p.y, p.x) = 255;
    }
    if (!changed.empty()) {
        updateVisualizationAt(changed);
        if (!dirty_) { dirty_ = true; emit dirtyChanged(true); }
        update();
        emit editOp(changed, add);
    }
}

void OcViewWidget::performGrayEditAt(int x0, int y0)
{
    if (src_.empty()) return;
    if (x0 < 0 || y0 < 0 || x0 >= src_.cols || y0 >= src_.rows) return;
    if (src_.at<uchar>(y0, x0) >= 255) return;
    std::vector<cv::Point> pts;
    floodAnyColor(x0, y0, pts);
    std::vector<cv::Point> changed;
    changed.reserve(pts.size());
    for (const auto& p : pts) {
        if (!out_.at<uchar>(p.y, p.x)) {
            out_.at<uchar>(p.y, p.x) = 255;
            changed.push_back(p);
        }
    }
    if (changed.empty()) return;
    updateVisualizationAt(changed);
    if (!dirty_) { dirty_ = true; emit dirtyChanged(true); }
    update();
    emit editOp(changed, /*add=*/true);
}

void OcViewWidget::applyOp(const std::vector<cv::Point>& pts, bool add)
{
    if (out_.empty() || pts.empty()) return;
    const uchar v = add ? uchar(255) : uchar(0);
    for (const auto& p : pts) {
        out_.at<uchar>(p.y, p.x) = v;
    }
    updateVisualizationAt(pts);
    if (!dirty_) { dirty_ = true; emit dirtyChanged(true); }
    update();
}

void OcViewWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Control) updateCursorForMods(e->modifiers() | Qt::ControlModifier);
    if (e->key() == Qt::Key_Escape) {
        if (stripPhase_ != StripPhase::None) { cancelStripInProgress(); return; }
        if (!lastPolyMask_.empty()) { cancelRectSelection(); return; }
    }
    QWidget::keyPressEvent(e);
}

void OcViewWidget::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Control) updateCursorForMods(e->modifiers() & ~Qt::ControlModifier);
    QWidget::keyReleaseEvent(e);
}

void OcViewWidget::updateCursorForMods(Qt::KeyboardModifiers m)
{
    if (panning_) { setCursor(Qt::ClosedHandCursor); return; }
    if (m & Qt::ControlModifier) setCursor(makePickCursor());
    else setCursor(Qt::ArrowCursor);
}

cv::Point OcViewWidget::pickClosestNear(int cx, int cy, int radius,
                                        bool includeGray) const
{
    if (src_.empty()) return {-1, -1};
    const int y0 = std::max(0, cy - radius), y1 = std::min(src_.rows - 1, cy + radius);
    const int x0 = std::max(0, cx - radius), x1 = std::min(src_.cols - 1, cx + radius);
    int bestD2 = INT_MAX;
    cv::Point r(-1, -1);
    const int r2 = radius * radius;
    for (int y = y0; y <= y1; ++y) {
        const uchar* o1 = o1_.ptr<uchar>(y);
        const uchar* o2 = o2_.ptr<uchar>(y);
        const uchar* s  = src_.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            bool match = o1[x] || o2[x];
            if (!match && includeGray) match = (s[x] < 255);
            if (!match) continue;
            const int d2 = (x - cx)*(x - cx) + (y - cy)*(y - cy);
            if (d2 > r2) continue;
            if (d2 < bestD2) { bestD2 = d2; r = cv::Point(x, y); }
        }
    }
    return r;
}

void OcViewWidget::leaveEvent(QEvent*)
{
    emit hudUpdate({});
}

void OcViewWidget::emitHud(const QPoint& widgetPos)
{
    if (vis_.isNull()) { emit hudUpdate({}); return; }
    const QPointF ip = widgetToImage(widgetPos);
    const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
    QString s;
    if (ix >= 0 && iy >= 0 && ix < src_.cols && iy < src_.rows) {
        const int g = src_.at<uchar>(iy, ix);
        const int col = colorAt(ix, iy);
        const char* cn = "white";
        switch (col) {
            case 1: cn = "green"; break;
            case 2: cn = "red";   break;
            case 3: cn = "yellow";break;
            case 4: cn = "black"; break;
        }
        s = QString("X=%1 Y=%2  gray=%3  %4  zoom=%5%")
              .arg(ix).arg(iy).arg(g).arg(cn).arg(int(scale_ * 100));
    }
    emit hudUpdate(s);
}