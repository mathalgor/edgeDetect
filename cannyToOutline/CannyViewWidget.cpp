#include "CannyViewWidget.h"
#include "CursorUtils.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QPixmap>
#include <QPolygon>

CannyViewWidget::CannyViewWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::OpenHandCursor);
}

void CannyViewWidget::setSource(const cv::Mat& gray)
{
    CV_Assert(gray.type() == CV_8UC1);
    src_ = gray.clone();
    selMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    outline_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    selDirty_ = true;
    outlineDirty_ = true;
    pendingFit_ = true;
    analyzeComponents();
    rebuildDisplay();
    update();
    emit analysisReady();
}

void CannyViewWidget::setOutlineMask(const cv::Mat& mask)
{
    if (src_.empty() || mask.empty()) return;
    CV_Assert(mask.type() == CV_8UC1);
    cv::Mat m = mask;
    if (m.size() != src_.size()) {
        cv::Mat r;
        cv::resize(m, r, src_.size(), 0, 0, cv::INTER_NEAREST);
        m = r;
    }
    // file convention: 0=line, 255=background → internally reversed
    cv::bitwise_not(m, outline_);
    // ensure binarization (e.g. for JPG-like artifacts)
    cv::threshold(outline_, outline_, 127, 255, cv::THRESH_BINARY);
    outlineDirty_ = true;
    if (!showSource_ && !showOutline_) rebuildDisplay();
    update();
}

void CannyViewWidget::setRange(int lo, int hi)
{
    lo_ = std::clamp(lo, 0, 254);
    hi_ = std::clamp(hi, 0, 254);
    if (lo_ > hi_) std::swap(lo_, hi_);
    if (filter_) {
        rebuildDisplay();
        if (filterOutline_) outlineDirty_ = true;
        update();
    }
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::setFilter(bool on)
{
    if (filter_ == on) return;
    filter_ = on;
    rebuildDisplay();
    outlineDirty_ = true;
    update();
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::setFilterOutline(bool on)
{
    if (filterOutline_ == on) return;
    filterOutline_ = on;
    outlineDirty_ = true;
    update();
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::setOriginal(const cv::Mat& img)
{
    if (img.empty()) {
        original_.release();
        originalImage_ = {};
        update();
        return;
    }
    original_ = img.clone();
    // resize to match src_ if it differs
    if (!src_.empty() && original_.size() != src_.size()) {
        cv::Mat r;
        cv::resize(original_, r, src_.size(), 0, 0, cv::INTER_AREA);
        original_ = r;
    }
    cv::Mat rgba;
    if (original_.channels() == 1) {
        cv::cvtColor(original_, rgba, cv::COLOR_GRAY2RGBA);
    } else if (original_.channels() == 3) {
        cv::cvtColor(original_, rgba, cv::COLOR_BGR2RGBA);
    } else if (original_.channels() == 4) {
        cv::cvtColor(original_, rgba, cv::COLOR_BGRA2RGBA);
    } else {
        original_.release();
        originalImage_ = {};
        update();
        return;
    }
    originalImage_ = QImage(rgba.data, rgba.cols, rgba.rows,
                            int(rgba.step), QImage::Format_RGBA8888).copy();
    update();
}

void CannyViewWidget::setShowOriginal(bool on)
{
    if (showOriginalMode_ == on) return;
    showOriginalMode_ = on;
    update();
}

void CannyViewWidget::setShowSource(bool on)
{
    if (showSource_ == on) return;
    showSource_ = on;
    rebuildDisplay();
    update();
    if (thresholdMode_ && thresholdFinal_) recomputeThresholdMasks();
}

void CannyViewWidget::setShowOutline(bool on)
{
    if (showOutline_ == on) return;
    showOutline_ = on;
    rebuildDisplay();
    update();
    if (thresholdMode_ && thresholdFinal_) recomputeThresholdMasks();
}

void CannyViewWidget::setBlackMode(bool on)
{
    if (blackMode_ == on) return;
    blackMode_ = on;
    rebuildDisplay();
    update();
    if (thresholdMode_ && thresholdFinal_) recomputeThresholdMasks();
}

void CannyViewWidget::setHideDone(bool on)
{
    if (hideDone_ == on) return;
    hideDone_ = on;
    rebuildDisplay();
    update();
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::setJoinTol(int tol)
{
    joinTol_ = std::max(0, tol);
}

void CannyViewWidget::setAllDarker(bool on)
{
    allDarker_ = on;
}

void CannyViewWidget::setConn8(bool on)
{
    if (conn8_ == on) return;
    conn8_ = on;
    // rebuild component analysis (size/extent/labels)
    analyzeComponents();
    if (filter_) { rebuildDisplay(); }
    update();
    emit analysisReady();
}

void CannyViewWidget::setMinSize(int n)
{
    n = std::max(0, n);
    if (minSize_ == n) return;
    minSize_ = n;
    if (filterOutline_) outlineDirty_ = true;
    if (filter_) { rebuildDisplay(); update(); }
    else if (filterOutline_) update();
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::setMinExtent(double d)
{
    d = std::max(0.0, d);
    if (std::abs(minExtent_ - float(d)) < 1e-6f) return;
    minExtent_ = float(d);
    if (filterOutline_) outlineDirty_ = true;
    if (filter_) { rebuildDisplay(); update(); }
    else if (filterOutline_) update();
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::analyzeComponents()
{
    labels_.release();
    labelSize_.clear();
    sizeMap_.release();
    extentMap_.release();
    maxSize_ = 0;
    maxExtent_ = 0.0f;
    if (src_.empty()) return;

    const int rows = src_.rows, cols = src_.cols;
    sizeMap_   = cv::Mat::zeros(src_.size(), CV_32S);
    extentMap_ = cv::Mat::zeros(src_.size(), CV_32F);
    labels_    = cv::Mat::zeros(src_.size(), CV_32S);
    cv::Mat visited = cv::Mat::zeros(src_.size(), CV_8UC1);
    int nextLabel = 1;
    labelSize_.push_back(0);   // [0] = background

    static constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    static constexpr int dx4[4] = { 0, -1, 1, 0};
    static constexpr int dy4[4] = {-1,  0, 0, 1};
    const int nNb = conn8_ ? 8 : 4;
    const int* dxN = conn8_ ? dx8 : dx4;
    const int* dyN = conn8_ ? dy8 : dy4;

    std::vector<cv::Point> comp;
    std::vector<cv::Point> stack;
    std::vector<cv::Point> hull;
    comp.reserve(256); stack.reserve(256);

    for (int y0 = 0; y0 < rows; ++y0) {
        const uchar* srcRow = src_.ptr<uchar>(y0);
        const uchar* visRow = visited.ptr<uchar>(y0);
        for (int x0 = 0; x0 < cols; ++x0) {
            if (visRow[x0]) continue;
            const uchar target = srcRow[x0];
            if (target >= 255) { continue; }   // white background — skip

            comp.clear();
            stack.clear();
            stack.emplace_back(x0, y0);
            visited.at<uchar>(y0, x0) = 1;
            while (!stack.empty()) {
                const cv::Point p = stack.back();
                stack.pop_back();
                comp.push_back(p);
                for (int k = 0; k < nNb; ++k) {
                    const int nx = p.x + dxN[k];
                    const int ny = p.y + dyN[k];
                    if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
                    if (visited.at<uchar>(ny, nx)) continue;
                    if (src_.at<uchar>(ny, nx) != target) continue;
                    visited.at<uchar>(ny, nx) = 1;
                    stack.emplace_back(nx, ny);
                }
            }

            const int sz = int(comp.size());
            float diam = 0.0f;
            if (sz >= 2) {
                cv::convexHull(comp, hull);
                double best = 0;
                for (size_t i = 0; i + 1 < hull.size(); ++i) {
                    for (size_t j = i + 1; j < hull.size(); ++j) {
                        const double dxh = hull[i].x - hull[j].x;
                        const double dyh = hull[i].y - hull[j].y;
                        const double d2 = dxh*dxh + dyh*dyh;
                        if (d2 > best) best = d2;
                    }
                }
                diam = float(std::sqrt(best));
            }

            const int L = nextLabel++;
            labelSize_.push_back(sz);
            for (const auto& q : comp) {
                sizeMap_.at<int>(q)   = sz;
                extentMap_.at<float>(q) = diam;
                labels_.at<int>(q)    = L;
            }
            if (sz > maxSize_) maxSize_ = sz;
            if (diam > maxExtent_) maxExtent_ = diam;
        }
    }
}

void CannyViewWidget::rebuildDisplay()
{
    if (src_.empty()) { display_ = {}; return; }

    cv::Mat out;
    if (!showSource_) {
        out = cv::Mat(src_.size(), CV_8UC1, cv::Scalar(255));
        // when outline is also hidden: show outline as black on white
        if (!showOutline_ && !outline_.empty()) out.setTo(0, outline_);
    } else if (!filter_) {
        if (blackMode_) {
            // src<255 -> 0, inaczej 255
            cv::threshold(src_, out, 254, 255, cv::THRESH_BINARY);
        } else {
            out = src_.clone();
        }
    } else {
        // filter ON: pixels satisfying conditions -> keep original shade,
        //            rest -> 255 (white / hidden)
        out = cv::Mat(src_.size(), CV_8UC1, cv::Scalar(255));
        const bool useSize = (minSize_ > 0) && !sizeMap_.empty();
        const bool useExt  = (minExtent_ > 0) && !extentMap_.empty();
        const int rows = src_.rows, cols = src_.cols;
        for (int y = 0; y < rows; ++y) {
            const uchar* s = src_.ptr<uchar>(y);
            const int*   sz = useSize ? sizeMap_.ptr<int>(y)   : nullptr;
            const float* ex = useExt  ? extentMap_.ptr<float>(y) : nullptr;
            uchar* d = out.ptr<uchar>(y);
            for (int x = 0; x < cols; ++x) {
                const int v = s[x];
                if (v < lo_ || v > hi_) continue;
                if (sz && sz[x] < minSize_) continue;
                if (ex && ex[x] < minExtent_) continue;
                d[x] = blackMode_ ? 0 : uchar(v);
            }
        }
    }
    if (hideDone_ && !outline_.empty()) {
        out.setTo(255, outline_);   // hide done (white)
    }
    display_ = QImage(out.data, out.cols, out.rows,
                      int(out.step), QImage::Format_Grayscale8).copy();
}

void CannyViewWidget::fitToWindow()
{
    if (display_.isNull()) return;
    if (width() < 50 || height() < 50) { pendingFit_ = true; return; }
    const double sx = double(width())  / display_.width();
    const double sy = double(height()) / display_.height();
    scale_ = std::min(sx, sy);
    panOffset_ = QPointF(
        (width()  - display_.width()  * scale_) * 0.5,
        (height() - display_.height() * scale_) * 0.5
    );
    pendingFit_ = false;
    update();
}

void CannyViewWidget::zoomOneToOne()
{
    if (display_.isNull()) return;
    const QPoint c(width()/2, height()/2);
    const QPointF imgC = widgetToImage(c);
    scale_ = 1.0;
    panOffset_ = QPointF(c.x() - imgC.x() * scale_,
                         c.y() - imgC.y() * scale_);
    update();
}

void CannyViewWidget::resizeEvent(QResizeEvent*)
{
    if (display_.isNull()) return;
    if (pendingFit_) fitToWindow();
}

QPointF CannyViewWidget::widgetToImage(const QPoint& p) const
{
    return QPointF((p.x() - panOffset_.x()) / scale_,
                   (p.y() - panOffset_.y()) / scale_);
}

QPointF CannyViewWidget::imageToWidget(const QPointF& p) const
{
    return QPointF(p.x() * scale_ + panOffset_.x(),
                   p.y() * scale_ + panOffset_.y());
}

void CannyViewWidget::applyZoomAt(const QPoint& anchor, double factor)
{
    if (display_.isNull()) return;
    const QPointF imgPt = widgetToImage(anchor);
    scale_ = std::clamp(scale_ * factor, 0.05, 64.0);
    panOffset_ = QPointF(anchor.x() - imgPt.x() * scale_,
                         anchor.y() - imgPt.y() * scale_);
    update();
}

void CannyViewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 40));
    const bool useOrig = showOriginalMode_ && !originalImage_.isNull();
    const bool thrFinal = thresholdMode_ && thresholdFinal_
                          && !thresholdDisplayImage_.isNull();
    const QImage& base = useOrig ? originalImage_
                                 : (thrFinal ? thresholdDisplayImage_ : display_);
    if (base.isNull()) return;

    QRectF dst(panOffset_.x(), panOffset_.y(),
               base.width()  * scale_,
               base.height() * scale_);

    // during pan: no smoothing, fast preview
    p.setRenderHint(QPainter::SmoothPixmapTransform, !panning_ && scale_ < 1.0);
    p.drawImage(dst, base);

    if (fitMode_ && !fitGreen_.empty() && cv::countNonZero(fitGreen_) > 0) {
        if (fitGreenDirty_) rebuildFitGreenImage();
        if (!fitGreenImage_.isNull()) p.drawImage(dst, fitGreenImage_);
    }
    if (thrFinal) {
        if (showOutline_ && !hideDone_ && !thresholdOutlineFinalImage_.isNull())
            p.drawImage(dst, thresholdOutlineFinalImage_);
    } else if (showOutline_ && !hideDone_ && !outline_.empty()
               && cv::countNonZero(outline_) > 0) {
        if (outlineDirty_) rebuildOutlineImage();
        if (!outlineImage_.isNull()) p.drawImage(dst, outlineImage_);
    }
    if (thresholdMode_ && !thresholdFinal_ && !thresholdOverlayImage_.isNull()) {
        p.drawImage(dst, thresholdOverlayImage_);
    }
    // threshold region overlay (accepted rectangle / strip)
    if (thresholdMode_ && thresholdLimitRegion_ && !thresholdRegionMask_.empty()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 220, 255), 2, Qt::DashLine));
        if (!thrLastPoly_.empty()) {
            QPolygonF qp;
            for (const auto& c : thrLastPoly_)
                qp << imageToWidget(QPointF(c.x + 0.5, c.y + 0.5));
            p.drawPolygon(qp);
        } else {
            const QPointF a = imageToWidget(QPointF(thrRectStartImg_));
            const QPointF b = imageToWidget(QPointF(thrRectEndImg_));
            QRectF rb(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                      QPointF(std::max(a.x(), b.x()) + scale_, std::max(a.y(), b.y()) + scale_));
            p.drawRect(rb);
        }
    }
    // threshold rubber-band while drawing
    if (thrDragging_) {
        const QPointF a = imageToWidget(QPointF(thrRectStartImg_));
        const QPointF b = imageToWidget(QPointF(thrRectEndImg_));
        QRectF rb(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                  QPointF(std::max(a.x(), b.x()) + 1, std::max(a.y(), b.y()) + 1));
        p.setPen(QPen(QColor(0, 220, 255), 1, Qt::DashLine));
        p.setBrush(QColor(0, 220, 255, 40));
        p.drawRect(rb);
    } else if (thrStripPhase_ == ThrStrip::P1Set) {
        const QPointF a = imageToWidget(QPointF(thrStripP1_) + QPointF(0.5, 0.5));
        const QPointF b = imageToWidget(QPointF(thrStripCursor_) + QPointF(0.5, 0.5));
        p.setPen(QPen(QColor(0, 220, 255), 2));
        p.setBrush(QColor(0, 220, 255));
        p.drawLine(a, b);
        p.drawEllipse(a, 3, 3);
    } else if (thrStripPhase_ == ThrStrip::P2Set) {
        const auto poly = stripCorners(thrStripP1_, thrStripP2_, thrStripCursor_);
        if (!poly.empty()) {
            QPolygonF qp;
            for (const auto& c : poly)
                qp << imageToWidget(QPointF(c.x + 0.5, c.y + 0.5));
            p.setPen(QPen(QColor(0, 220, 255), 1, Qt::DashLine));
            p.setBrush(QColor(0, 220, 255, 40));
            p.drawPolygon(qp);
            p.setPen(QPen(QColor(0, 220, 255), 1));
            p.drawLine(imageToWidget(QPointF(thrStripP1_) + QPointF(0.5, 0.5)),
                       imageToWidget(QPointF(thrStripP2_) + QPointF(0.5, 0.5)));
        }
    }
    if (fitMode_ && !fitPurple_.empty() && cv::countNonZero(fitPurple_) > 0) {
        if (fitPurpleDirty_) rebuildFitPurpleImage();
        if (!fitPurpleImage_.isNull()) p.drawImage(dst, fitPurpleImage_);
    }
    if (!selMask_.empty() && cv::countNonZero(selMask_) > 0) {
        if (selDirty_) rebuildSelectionImage();
        if (!selImage_.isNull()) p.drawImage(dst, selImage_);
    }

    if (rectShowing_) {
        if (rectOverlayDirty_) rebuildRectOverlay();
        if (!rectOverlayImage_.isNull()) p.drawImage(dst, rectOverlayImage_);
    }

    if (rectDragging_) {
        // rubber-band rect
        const QPointF a = imageToWidget(QPointF(rectStartImg_));
        const QPointF b = imageToWidget(QPointF(rectEndImg_));
        QRectF rb(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                  QPointF(std::max(a.x(), b.x()) + 1, std::max(a.y(), b.y()) + 1));
        p.setPen(QPen(QColor(0, 200, 255), 1, Qt::DashLine));
        p.setBrush(QColor(0, 200, 255, 40));
        p.drawRect(rb);
    }

    // oriented strip preview
    if (stripPhase_ == StripPhase::P1Set) {
        const QPointF a = imageToWidget(QPointF(stripP1_) + QPointF(0.5, 0.5));
        const QPointF b = imageToWidget(QPointF(stripCursor_) + QPointF(0.5, 0.5));
        p.setPen(QPen(QColor(0, 200, 255), 2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(a, b);
        p.setBrush(QColor(0, 200, 255));
        p.drawEllipse(a, 3, 3);
    } else if (stripPhase_ == StripPhase::P2Set) {
        const auto poly = stripCorners(stripP1_, stripP2_, stripCursor_);
        if (!poly.empty()) {
            QPolygonF qp;
            for (const auto& c : poly) {
                qp << imageToWidget(QPointF(c.x + 0.5, c.y + 0.5));
            }
            p.setPen(QPen(QColor(0, 200, 255), 1, Qt::DashLine));
            p.setBrush(QColor(0, 200, 255, 40));
            p.drawPolygon(qp);
            // axis inside
            p.setPen(QPen(QColor(0, 200, 255), 1));
            p.drawLine(imageToWidget(QPointF(stripP1_) + QPointF(0.5, 0.5)),
                       imageToWidget(QPointF(stripP2_) + QPointF(0.5, 0.5)));
        }
    }
}

void CannyViewWidget::enterFitMode(const cv::Mat& fitRef, bool conn8,
                                   int coveragePct, int tol, bool append)
{
    if (src_.empty() || fitRef.empty()) return;
    fitMode_ = true;
    fitConn8_ = conn8;
    fitCoveragePct_ = std::clamp(coveragePct, 0, 100);
    fitTol_ = std::max(0, tol);
    fitAppend_ = append;

    // snapshot of our outline (for counting green/purple); editing blocked during fit mode
    fitOutlineSnap_ = outline_.empty()
        ? cv::Mat::zeros(src_.size(), CV_8UC1)
        : outline_.clone();

    // fitRef: white strokes on black background → internally 255=line (threshold ≥128)
    cv::Mat ref = fitRef;
    if (ref.size() != src_.size()) {
        cv::Mat r;
        cv::resize(ref, r, src_.size(), 0, 0, cv::INTER_NEAREST);
        ref = r;
    }
    cv::threshold(ref, fitRef_, 127, 255, cv::THRESH_BINARY);

    // distance transform: distance to the nearest fitRef_ pixel
    cv::Mat inv;
    cv::bitwise_not(fitRef_, inv);
    if (cv::countNonZero(fitRef_) == 0) {
        fitDist_ = cv::Mat(src_.size(), CV_32F, cv::Scalar(1e9f));
    } else {
        cv::distanceTransform(inv, fitDist_, cv::DIST_L2, cv::DIST_MASK_3);
    }
    recomputeFitSegments();
    recomputeFitCoverage();
    rebuildFitMask();
    fitGreenDirty_ = true;
    fitPurpleDirty_ = true;
    update();
}

void CannyViewWidget::setFitAppend(bool on)
{
    if (fitAppend_ == on) return;
    fitAppend_ = on;
    if (!fitMode_) return;
    rebuildFitMask();
    fitGreenDirty_ = true;
    fitPurpleDirty_ = true;
    update();
}

void CannyViewWidget::setFitConn8(bool on)
{
    if (fitConn8_ == on) return;
    fitConn8_ = on;
    if (!fitMode_) return;
    recomputeFitSegments();
    recomputeFitCoverage();
    rebuildFitMask();
    fitGreenDirty_ = true;
    fitPurpleDirty_ = true;
    update();
}

void CannyViewWidget::setFitCoverage(int pct)
{
    pct = std::clamp(pct, 0, 100);
    if (fitCoveragePct_ == pct) return;
    fitCoveragePct_ = pct;
    if (!fitMode_) return;
    rebuildFitMask();
    fitGreenDirty_ = true;
    fitPurpleDirty_ = true;
    update();
}

void CannyViewWidget::setFitTol(int tol)
{
    tol = std::max(0, tol);
    if (fitTol_ == tol) return;
    fitTol_ = tol;
    if (!fitMode_) return;
    recomputeFitCoverage();
    rebuildFitMask();
    fitGreenDirty_ = true;
    fitPurpleDirty_ = true;
    update();
}

void CannyViewWidget::setFitMinGreen(int n)
{
    n = std::max(0, n);
    if (fitMinGreen_ == n) return;
    fitMinGreen_ = n;
    if (!fitMode_) return;
    rebuildFitMask();
    fitGreenDirty_ = true;
    fitPurpleDirty_ = true;
    update();
}

void CannyViewWidget::exitFitMode()
{
    if (!fitMode_) return;
    fitMode_ = false;
    fitOutlineSnap_.release();
    fitRef_.release();
    fitDist_.release();
    fitLabels_.release();
    fitLabelSize_.clear();
    fitLabelCovered_.clear();
    fitMask_.release();
    fitGreen_.release();
    fitPurple_.release();
    fitGreenImage_ = {};
    fitPurpleImage_ = {};
    fitGreenDirty_ = true;
    fitPurpleDirty_ = true;
    update();
}

void CannyViewWidget::commitFitMode()
{
    if (!fitMode_) return;
    cv::Mat green = fitGreen_.empty() ? cv::Mat() : fitGreen_.clone();
    cv::Mat purple = fitPurple_.empty() ? cv::Mat() : fitPurple_.clone();
    bool changed = false;
    if (!green.empty() && cv::countNonZero(green) > 0) {
        cv::bitwise_or(outline_, green, outline_);
        emit outlineBulkOp(green, true);
        changed = true;
    }
    if (!purple.empty() && cv::countNonZero(purple) > 0) {
        cv::Mat keep;
        cv::bitwise_not(purple, keep);
        cv::bitwise_and(outline_, keep, outline_);
        emit outlineBulkOp(purple, false);
        changed = true;
    }
    exitFitMode();
    if (changed) {
        outlineDirty_ = true;
        if (!showSource_ && !showOutline_) rebuildDisplay();
        update();
    }
}

void CannyViewWidget::recomputeFitSegments()
{
    fitLabels_.release();
    fitLabelSize_.clear();
    fitLabelCovered_.clear();
    if (src_.empty()) return;

    const int rows = src_.rows, cols = src_.cols;
    fitLabels_ = cv::Mat::zeros(src_.size(), CV_32S);
    cv::Mat visited = cv::Mat::zeros(src_.size(), CV_8UC1);
    int nextLabel = 1;
    fitLabelSize_.push_back(0);
    fitLabelCovered_.push_back(0);

    static constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    static constexpr int dx4[4] = { 0, -1, 1, 0};
    static constexpr int dy4[4] = {-1,  0, 0, 1};
    const int nNb = fitConn8_ ? 8 : 4;
    const int* dxN = fitConn8_ ? dx8 : dx4;
    const int* dyN = fitConn8_ ? dy8 : dy4;

    std::vector<cv::Point> stack;
    stack.reserve(256);

    for (int y0 = 0; y0 < rows; ++y0) {
        const uchar* srcRow = src_.ptr<uchar>(y0);
        const uchar* visRow = visited.ptr<uchar>(y0);
        for (int x0 = 0; x0 < cols; ++x0) {
            if (visRow[x0]) continue;
            const uchar target = srcRow[x0];
            if (target >= 255) continue;

            stack.clear();
            stack.emplace_back(x0, y0);
            visited.at<uchar>(y0, x0) = 1;
            const int L = nextLabel++;
            int sz = 0;
            while (!stack.empty()) {
                const cv::Point p = stack.back();
                stack.pop_back();
                fitLabels_.at<int>(p) = L;
                ++sz;
                for (int k = 0; k < nNb; ++k) {
                    const int nx = p.x + dxN[k];
                    const int ny = p.y + dyN[k];
                    if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
                    if (visited.at<uchar>(ny, nx)) continue;
                    if (src_.at<uchar>(ny, nx) != target) continue;
                    visited.at<uchar>(ny, nx) = 1;
                    stack.emplace_back(nx, ny);
                }
            }
            fitLabelSize_.push_back(sz);
            fitLabelCovered_.push_back(0);
        }
    }
}

void CannyViewWidget::recomputeFitCoverage()
{
    if (fitLabels_.empty()) return;
    std::fill(fitLabelCovered_.begin(), fitLabelCovered_.end(), 0);
    if (fitDist_.empty()) return;
    const float tol = static_cast<float>(fitTol_);
    const int rows = fitLabels_.rows, cols = fitLabels_.cols;
    for (int y = 0; y < rows; ++y) {
        const int*   lr = fitLabels_.ptr<int>(y);
        const float* dr = fitDist_.ptr<float>(y);
        for (int x = 0; x < cols; ++x) {
            const int L = lr[x];
            if (L <= 0 || L >= static_cast<int>(fitLabelCovered_.size())) continue;
            if (dr[x] <= tol) ++fitLabelCovered_[L];
        }
    }
}

void CannyViewWidget::rebuildFitMask()
{
    fitMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    fitGreen_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    fitPurple_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    if (fitLabels_.empty()) return;

    std::vector<uchar> ok(fitLabelSize_.size(), 0);
    for (size_t L = 1; L < fitLabelSize_.size(); ++L) {
        if (fitLabelSize_[L] <= 0) continue;
        if (fitLabelSize_[L] <= fitMinGreen_) continue;   // segment too small
        if (static_cast<long long>(fitLabelCovered_[L]) * 100
            >= static_cast<long long>(fitCoveragePct_) * fitLabelSize_[L]) {
            ok[L] = 1;
        }
    }
    const int rows = fitLabels_.rows, cols = fitLabels_.cols;
    for (int y = 0; y < rows; ++y) {
        const int* lr = fitLabels_.ptr<int>(y);
        uchar* mr = fitMask_.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            const int L = lr[x];
            if (L > 0 && L < static_cast<int>(ok.size()) && ok[L]) mr[x] = 255;
        }
    }

    // green = M \ snapshot(outline)
    if (!fitOutlineSnap_.empty()) {
        cv::Mat notSnap;
        cv::bitwise_not(fitOutlineSnap_, notSnap);
        cv::bitwise_and(fitMask_, notSnap, fitGreen_);
    } else {
        fitGreen_ = fitMask_.clone();
    }

    // purple = snapshot(outline) \ dilate_L2(M, tol)  (when !append)
    if (!fitAppend_ && !fitOutlineSnap_.empty()) {
        cv::Mat dil;
        if (cv::countNonZero(fitMask_) == 0) {
            dil = cv::Mat::zeros(src_.size(), CV_8UC1);
        } else if (fitTol_ <= 0) {
            dil = fitMask_;
        } else {
            cv::Mat invM, distM;
            cv::bitwise_not(fitMask_, invM);
            cv::distanceTransform(invM, distM, cv::DIST_L2, cv::DIST_MASK_3);
            // dil = (distM <= tol)
            cv::threshold(distM, distM, static_cast<double>(fitTol_), 255, cv::THRESH_BINARY_INV);
            distM.convertTo(dil, CV_8UC1);
        }
        cv::Mat notDil;
        cv::bitwise_not(dil, notDil);
        cv::bitwise_and(fitOutlineSnap_, notDil, fitPurple_);
    }
}

void CannyViewWidget::rebuildFitGreenImage()
{
    if (fitGreen_.empty()) { fitGreenImage_ = {}; fitGreenDirty_ = false; return; }
    cv::Mat rgba(fitGreen_.size(), CV_8UC4, cv::Scalar(0,0,0,0));
    const int rows = fitGreen_.rows, cols = fitGreen_.cols;
    for (int y = 0; y < rows; ++y) {
        const uchar* sr = fitGreen_.ptr<uchar>(y);
        auto* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            if (sr[x]) dr[x] = cv::Vec4b(0, 220, 0, 200);
        }
    }
    fitGreenImage_ = QImage(rgba.data, cols, rows, static_cast<int>(rgba.step),
                            QImage::Format_RGBA8888).copy();
    fitGreenDirty_ = false;
}

void CannyViewWidget::rebuildFitPurpleImage()
{
    if (fitPurple_.empty()) { fitPurpleImage_ = {}; fitPurpleDirty_ = false; return; }
    cv::Mat rgba(fitPurple_.size(), CV_8UC4, cv::Scalar(0,0,0,0));
    const int rows = fitPurple_.rows, cols = fitPurple_.cols;
    for (int y = 0; y < rows; ++y) {
        const uchar* sr = fitPurple_.ptr<uchar>(y);
        auto* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            if (sr[x]) dr[x] = cv::Vec4b(0, 80, 255, 230);
        }
    }
    fitPurpleImage_ = QImage(rgba.data, cols, rows, static_cast<int>(rgba.step),
                             QImage::Format_RGBA8888).copy();
    fitPurpleDirty_ = false;
}

void CannyViewWidget::rebuildOutlineImage()
{
    if (outline_.empty()) { outlineImage_ = {}; return; }
    cv::Mat rgba(outline_.size(), CV_8UC4, cv::Scalar(0,0,0,0));
    const int rows = outline_.rows, cols = outline_.cols;
    const bool useFilter = filterOutline_ && filter_ && !src_.empty()
                           && src_.size() == outline_.size();
    const bool useSize = useFilter && minSize_ > 0 && !sizeMap_.empty();
    const bool useExt  = useFilter && minExtent_ > 0 && !extentMap_.empty();
    for (int y = 0; y < rows; ++y) {
        const uchar* sr  = outline_.ptr<uchar>(y);
        const uchar* gr  = useFilter ? src_.ptr<uchar>(y) : nullptr;
        const int*   szr = useSize ? sizeMap_.ptr<int>(y) : nullptr;
        const float* exr = useExt  ? extentMap_.ptr<float>(y) : nullptr;
        cv::Vec4b* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            if (!sr[x]) continue;
            if (gr) { const int v = gr[x]; if (v < lo_ || v > hi_) continue; }
            if (szr && szr[x] < minSize_) continue;
            if (exr && exr[x] < minExtent_) continue;
            dr[x] = cv::Vec4b(255, 0, 0, 230);
        }
    }
    outlineImage_ = QImage(rgba.data, cols, rows, int(rgba.step),
                           QImage::Format_RGBA8888).copy();
    outlineDirty_ = false;
}

// --- threshold add/remove tool ----------------------------------------------

void CannyViewWidget::enterThresholdMode(bool addOn, int addVal,
                                         bool rmOn, int rmVal, bool finalPv)
{
    thresholdMode_ = true;
    thresholdAddOn_ = addOn;
    thresholdAddVal_ = addVal;
    thresholdRemoveOn_ = rmOn;
    thresholdRemoveVal_ = rmVal;
    thresholdFinal_ = finalPv;
    recomputeThresholdMasks();
    update();
}

void CannyViewWidget::exitThresholdMode()
{
    if (!thresholdMode_) return;
    thresholdMode_ = false;
    thresholdAdd_.release();
    thresholdRemove_.release();
    thresholdOverlayImage_ = {};
    thresholdDisplayImage_ = {};
    thresholdOutlineFinalImage_ = {};
    thresholdLimitRegion_ = false;
    thresholdRegionMask_.release();
    thresholdEligibleMask_.release();
    thrLastPoly_.clear();
    thrStripPhase_ = ThrStrip::None;
    thrDragging_ = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void CannyViewWidget::setThresholdAdd(bool on, int v)
{
    thresholdAddOn_ = on;
    thresholdAddVal_ = v;
    if (thresholdMode_) { recomputeThresholdMasks(); update(); }
}

void CannyViewWidget::setThresholdRemove(bool on, int v)
{
    thresholdRemoveOn_ = on;
    thresholdRemoveVal_ = v;
    if (thresholdMode_) { recomputeThresholdMasks(); update(); }
}

void CannyViewWidget::setThresholdFinal(bool on)
{
    if (thresholdFinal_ == on) return;
    thresholdFinal_ = on;
    if (thresholdMode_) { recomputeThresholdMasks(); update(); }
}

void CannyViewWidget::setThresholdLimitRegion(bool on)
{
    if (thresholdLimitRegion_ == on) return;
    thresholdLimitRegion_ = on;
    if (!on) {
        thresholdRegionMask_.release();
        thresholdEligibleMask_.release();
        thrLastPoly_.clear();
        thrStripPhase_ = ThrStrip::None;
        thrDragging_ = false;
        setCursor(Qt::ArrowCursor);
    } else {
        recomputeThresholdEligible();
        setCursor(Qt::CrossCursor);
    }
    if (thresholdMode_) { recomputeThresholdMasks(); update(); }
}

void CannyViewWidget::setThresholdRegionMode(int m)
{
    m = std::clamp(m, 0, 1);
    if (thresholdRegionMode_ == m) return;
    thresholdRegionMode_ = m;
    if (thresholdLimitRegion_ && !thresholdRegionMask_.empty()) {
        recomputeThresholdEligible();
        if (thresholdMode_) { recomputeThresholdMasks(); update(); }
    }
}

void CannyViewWidget::clearThresholdRegion()
{
    thresholdRegionMask_.release();
    thresholdEligibleMask_.release();
    thrLastPoly_.clear();
    thrStripPhase_ = ThrStrip::None;
    thrDragging_ = false;
    if (thresholdMode_) { recomputeThresholdMasks(); update(); }
}

void CannyViewWidget::finishThrRectRegion()
{
    if (src_.empty()) return;
    const int x0 = std::clamp(std::min(thrRectStartImg_.x(), thrRectEndImg_.x()), 0, src_.cols - 1);
    const int y0 = std::clamp(std::min(thrRectStartImg_.y(), thrRectEndImg_.y()), 0, src_.rows - 1);
    const int x1 = std::clamp(std::max(thrRectStartImg_.x(), thrRectEndImg_.x()), 0, src_.cols - 1);
    const int y1 = std::clamp(std::max(thrRectStartImg_.y(), thrRectEndImg_.y()), 0, src_.rows - 1);
    if (x1 <= x0 || y1 <= y0) return;
    thresholdRegionMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    cv::rectangle(thresholdRegionMask_, cv::Point(x0, y0), cv::Point(x1, y1),
                  cv::Scalar(255), cv::FILLED);
    thrLastPoly_.clear();
    recomputeThresholdEligible();
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::finishThrStripRegion(const QPoint& widthRef)
{
    if (src_.empty()) return;
    const auto poly = stripCorners(thrStripP1_, thrStripP2_, widthRef);
    thrStripPhase_ = ThrStrip::None;
    if (poly.size() < 3) return;
    thresholdRegionMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> contours{poly};
    cv::fillPoly(thresholdRegionMask_, contours, cv::Scalar(255));
    thrLastPoly_ = poly;
    recomputeThresholdEligible();
    if (thresholdMode_) recomputeThresholdMasks();
}

void CannyViewWidget::recomputeThresholdEligible()
{
    thresholdEligibleMask_.release();
    if (thresholdRegionMask_.empty() || src_.empty()) return;
    if (thresholdRegionMode_ == 0 /*Touching*/ && labels_.empty()) {
        // without labels — take the region itself as eligible
        thresholdEligibleMask_ = thresholdRegionMask_.clone();
        return;
    }
    const int rows = src_.rows, cols = src_.cols;
    if (thresholdRegionMode_ == 1 /*Inside*/) {
        // segments entirely contained in the region
        if (labels_.empty()) {
            thresholdEligibleMask_ = thresholdRegionMask_.clone();
            return;
        }
        // count segment pixels in the region and compare with labelSize_
        std::vector<int> countIn(labelSize_.size(), 0);
        for (int y = 0; y < rows; ++y) {
            const uchar* rm = thresholdRegionMask_.ptr<uchar>(y);
            const int*   lr = labels_.ptr<int>(y);
            for (int x = 0; x < cols; ++x) {
                if (!rm[x]) continue;
                const int L = lr[x];
                if (L > 0 && L < int(countIn.size())) ++countIn[L];
            }
        }
        std::vector<uchar> include(labelSize_.size(), 0);
        for (size_t L = 1; L < labelSize_.size(); ++L) {
            if (countIn[L] > 0 && countIn[L] == labelSize_[L]) include[L] = 1;
        }
        thresholdEligibleMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
        for (int y = 0; y < rows; ++y) {
            const int*   lr = labels_.ptr<int>(y);
            uchar* dr = thresholdEligibleMask_.ptr<uchar>(y);
            for (int x = 0; x < cols; ++x) {
                const int L = lr[x];
                if (L > 0 && L < int(include.size()) && include[L]) dr[x] = 255;
            }
        }
    } else {
        // Touching: every segment with at least one pixel in the region
        std::vector<uchar> include(labelSize_.size(), 0);
        for (int y = 0; y < rows; ++y) {
            const uchar* rm = thresholdRegionMask_.ptr<uchar>(y);
            const int*   lr = labels_.ptr<int>(y);
            for (int x = 0; x < cols; ++x) {
                if (!rm[x]) continue;
                const int L = lr[x];
                if (L > 0 && L < int(include.size())) include[L] = 1;
            }
        }
        thresholdEligibleMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
        for (int y = 0; y < rows; ++y) {
            const int*   lr = labels_.ptr<int>(y);
            uchar* dr = thresholdEligibleMask_.ptr<uchar>(y);
            for (int x = 0; x < cols; ++x) {
                const int L = lr[x];
                if (L > 0 && L < int(include.size()) && include[L]) dr[x] = 255;
            }
        }
    }
}

void CannyViewWidget::commitThreshold()
{
    if (!thresholdMode_) return;
    cv::Mat addM = thresholdAdd_.clone();
    cv::Mat rmM  = thresholdRemove_.clone();
    const bool hasAdd = !addM.empty() && cv::countNonZero(addM) > 0;
    const bool hasRm  = !rmM.empty()  && cv::countNonZero(rmM)  > 0;
    exitThresholdMode();
    if (hasAdd) {
        applyBulkOp(addM, true);
        emit outlineBulkOp(addM, true);
    }
    if (hasRm) {
        applyBulkOp(rmM, false);
        emit outlineBulkOp(rmM, false);
    }
}

void CannyViewWidget::recomputeThresholdMasks()
{
    if (src_.empty()) {
        thresholdAdd_.release();
        thresholdRemove_.release();
        emit thresholdCountsChanged(0, 0);
        return;
    }
    const int rows = src_.rows, cols = src_.cols;
    thresholdAdd_  = cv::Mat::zeros(src_.size(), CV_8UC1);
    thresholdRemove_ = cv::Mat::zeros(src_.size(), CV_8UC1);

    const bool haveOutline = !outline_.empty() && outline_.size() == src_.size();
    const bool useFinalFilter = thresholdFinal_;
    const bool addFilter = useFinalFilter && filter_ && filterOutline_;
    const bool rmFilter  = useFinalFilter && filter_;
    const bool useRegion = thresholdLimitRegion_;
    const bool haveEligible = useRegion && !thresholdEligibleMask_.empty();

    for (int i = 0; i < 256; ++i) { thrAddHist_[i] = 0; thrRmHist_[i] = 0; }
    int addCount = 0, rmCount = 0;
    if (useRegion && !haveEligible) {
        // region required but not drawn — everything 0
        emit thresholdCountsChanged(0, 0);
        if (thresholdFinal_) rebuildThresholdFinal();
        else rebuildThresholdOverlay();
        return;
    }
    for (int y = 0; y < rows; ++y) {
        const uchar* sr = src_.ptr<uchar>(y);
        const uchar* orl = haveOutline ? outline_.ptr<uchar>(y) : nullptr;
        const uchar* er  = haveEligible ? thresholdEligibleMask_.ptr<uchar>(y) : nullptr;
        uchar* ar = thresholdAdd_.ptr<uchar>(y);
        uchar* rr = thresholdRemove_.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            if (er && !er[x]) continue;
            const int v = sr[x];
            const bool inOutline = (orl && orl[x]);
            // eligibility for histograms (independent of threshold)
            if (!inOutline && v < 255 && (!addFilter || passesFilter(x, y))) {
                ++thrAddHist_[v];
            }
            if (inOutline && (!rmFilter || passesFilter(x, y))) {
                ++thrRmHist_[v];
            }
            if (thresholdAddOn_ && v <= thresholdAddVal_ && v < 255 && !inOutline) {
                if (!addFilter || passesFilter(x, y)) {
                    ar[x] = 255;
                    ++addCount;
                }
            }
            if (thresholdRemoveOn_ && v >= thresholdRemoveVal_ && inOutline) {
                if (!rmFilter || passesFilter(x, y)) {
                    rr[x] = 255;
                    ++rmCount;
                }
            }
        }
    }
    emit thresholdCountsChanged(addCount, rmCount);

    if (thresholdFinal_) rebuildThresholdFinal();
    else rebuildThresholdOverlay();
}

int CannyViewWidget::thresholdAddCountIf(int v) const
{
    v = std::clamp(v, 0, 255);
    int s = 0;
    for (int i = 0; i <= v; ++i) s += thrAddHist_[i];
    return s;
}

int CannyViewWidget::thresholdRemoveCountIf(int v) const
{
    v = std::clamp(v, 0, 255);
    int s = 0;
    for (int i = v; i <= 255; ++i) s += thrRmHist_[i];
    return s;
}

void CannyViewWidget::rebuildThresholdOverlay()
{
    if (thresholdAdd_.empty() && thresholdRemove_.empty()) {
        thresholdOverlayImage_ = {};
        return;
    }
    const int rows = src_.rows, cols = src_.cols;
    cv::Mat rgba(rows, cols, CV_8UC4, cv::Scalar(0,0,0,0));
    for (int y = 0; y < rows; ++y) {
        const uchar* ar = thresholdAdd_.ptr<uchar>(y);
        const uchar* rr = thresholdRemove_.ptr<uchar>(y);
        cv::Vec4b* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            if (rr[x])      dr[x] = cv::Vec4b(180, 180, 0, 220);  // dark yellow
            else if (ar[x]) dr[x] = cv::Vec4b(255, 140, 0, 220);  // orange
        }
    }
    thresholdOverlayImage_ = QImage(rgba.data, cols, rows, int(rgba.step),
                                    QImage::Format_RGBA8888).copy();
}

void CannyViewWidget::rebuildThresholdFinal()
{
    // Syntetyczny outline = (outline ∪ add) \ remove. Renderujemy display
    // i outline tak jak normalnie, ale z podmienionym outline_.
    cv::Mat synth;
    if (!outline_.empty()) synth = outline_.clone();
    else                   synth = cv::Mat::zeros(src_.size(), CV_8UC1);
    if (!thresholdAdd_.empty())    cv::bitwise_or(synth, thresholdAdd_, synth);
    if (!thresholdRemove_.empty()) synth.setTo(0, thresholdRemove_);

    cv::Mat saved = outline_;
    outline_ = synth;
    rebuildDisplay();
    thresholdDisplayImage_ = display_;
    rebuildOutlineImage();
    thresholdOutlineFinalImage_ = outlineImage_;
    outline_ = saved;
    // restore normal images
    rebuildDisplay();
    outlineDirty_ = true;
}

void CannyViewWidget::rebuildSelectionImage()
{
    if (selMask_.empty()) { selImage_ = {}; return; }
    cv::Mat rgba(selMask_.size(), CV_8UC4, cv::Scalar(0,0,0,0));
    const int rows = selMask_.rows, cols = selMask_.cols;
    for (int y = 0; y < rows; ++y) {
        const uchar* sr = selMask_.ptr<uchar>(y);
        cv::Vec4b* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            if (sr[x]) dr[x] = cv::Vec4b(255, 255, 0, 200);
        }
    }
    selImage_ = QImage(rgba.data, cols, rows, int(rgba.step),
                       QImage::Format_RGBA8888).copy();
    selDirty_ = false;
}

void CannyViewWidget::clearSelection()
{
    if (!selMask_.empty()) selMask_.setTo(0);
    selDirty_ = true;
}

bool CannyViewWidget::passesFilter(int x, int y) const
{
    if (src_.empty()) return false;
    if (x < 0 || y < 0 || x >= src_.cols || y >= src_.rows) return false;
    if (hideDone_ && !outline_.empty() && outline_.at<uchar>(y, x)) return false;
    if (!filter_) return true;
    const int v = src_.at<uchar>(y, x);
    if (v < lo_ || v > hi_) return false;
    if (minSize_ > 0 && !sizeMap_.empty()
        && sizeMap_.at<int>(y, x) < minSize_) return false;
    if (minExtent_ > 0 && !extentMap_.empty()
        && extentMap_.at<float>(y, x) < minExtent_) return false;
    return true;
}

cv::Point CannyViewWidget::pickClosestNear(const cv::Point& p, int radius) const
{
    if (src_.empty()) return {-1, -1};
    int bestD2 = INT_MAX;
    int bestV  = 256;
    cv::Point r(-1, -1);
    const int y0 = std::max(0, p.y - radius), y1 = std::min(src_.rows - 1, p.y + radius);
    const int x0 = std::max(0, p.x - radius), x1 = std::min(src_.cols - 1, p.x + radius);
    for (int y = y0; y <= y1; ++y) {
        const uchar* row = src_.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const int v = row[x];
            if (v >= 255) continue;
            if (!passesFilter(x, y)) continue;
            const int d2 = (x - p.x)*(x - p.x) + (y - p.y)*(y - p.y);
            if (d2 < bestD2 || (d2 == bestD2 && v < bestV)) {
                bestD2 = d2;
                bestV  = v;
                r = cv::Point(x, y);
            }
        }
    }
    return r;
}

cv::Point CannyViewWidget::pickSeedNear(const cv::Point& p, int radius) const
{
    return pickClosestNear(p, radius);
}

bool CannyViewWidget::hasOutline() const
{
    return !outline_.empty() && cv::countNonZero(outline_) > 0;
}

bool CannyViewWidget::saveOutline(const QString& path) const
{
    if (outline_.empty()) return false;
    cv::Mat out;
    cv::bitwise_not(outline_, out);   // 0=line, 255=background
    return cv::imwrite(path.toStdString(), out,
        {cv::IMWRITE_PNG_COMPRESSION, 8, cv::IMWRITE_PNG_BILEVEL, 1});
}

void CannyViewWidget::applyBulkOp(const cv::Mat& mask, bool add)
{
    if (mask.empty() || outline_.empty() || mask.size() != outline_.size()) return;
    if (add) {
        cv::bitwise_or(outline_, mask, outline_);
    } else {
        cv::Mat keep;
        cv::bitwise_not(mask, keep);
        cv::bitwise_and(outline_, keep, outline_);
    }
    outlineDirty_ = true;
    if (!showSource_ && !showOutline_) rebuildDisplay();
    update();
}

void CannyViewWidget::setCandidateMode(int m)
{
    auto cm = static_cast<CandMode>(std::clamp(m, 0, 1));
    if (candidateMode_ == cm) return;
    candidateMode_ = cm;
    if (rectShowing_ && !lastPolyMask_.empty()) {
        rebuildCandidatesFromPoly();
        if (cv::countNonZero(candMask_) == 0) {
            cancelRectSelection();
        } else {
            splitByThreshold();
            rectOverlayDirty_ = true;
        }
        update();
    }
}

void CannyViewWidget::setRectThreshold(int t)
{
    t = std::clamp(t, 0, 254);
    if (rectThreshold_ == t) return;
    rectThreshold_ = t;
    if (rectShowing_) {
        splitByThreshold();
        rectOverlayDirty_ = true;
        update();
    }
}

void CannyViewWidget::commitRectSelection()
{
    if (!rectShowing_) return;
    cv::Mat added = orangeMask_.clone();
    rectShowing_ = false;
    rectDragging_ = false;
    candMask_.release();
    yellowMask_.release();
    orangeMask_.release();
    rectOverlayDirty_ = true;
    if (!added.empty() && cv::countNonZero(added) > 0) {
        cv::bitwise_or(outline_, added, outline_);
        outlineDirty_ = true;
        if (!showSource_ && !showOutline_) rebuildDisplay();
        emit outlineBulkOp(added, true);
    }
    update();
}

QVector<int> CannyViewWidget::uniqueCandidateValues() const
{
    QVector<int> out;
    if (candMask_.empty()) return out;
    std::array<bool, 256> seen{};
    const int rows = candMask_.rows, cols = candMask_.cols;
    for (int y = 0; y < rows; ++y) {
        const uchar* cr = candMask_.ptr<uchar>(y);
        const uchar* sr = src_.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            if (cr[x]) seen[sr[x]] = true;
        }
    }
    for (int v = 0; v < 256; ++v) if (seen[v]) out.push_back(v);
    return out;
}

QPoint CannyViewWidget::rectAnchorGlobal() const
{
    const int x = std::max(rectStartImg_.x(), rectEndImg_.x()) + 1;
    const int y = std::max(rectStartImg_.y(), rectEndImg_.y()) + 1;
    const QPointF w = imageToWidget(QPointF(x, y));
    return mapToGlobal(QPoint(int(w.x()), int(w.y())));
}

void CannyViewWidget::cancelRectSelection()
{
    rectShowing_ = false;
    rectDragging_ = false;
    candMask_.release();
    yellowMask_.release();
    orangeMask_.release();
    rectOverlayDirty_ = true;
    update();
}

void CannyViewWidget::computeCandidates()
{
    lastPolyMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    const int x1 = std::max(0, std::min(rectStartImg_.x(), rectEndImg_.x()));
    const int y1 = std::max(0, std::min(rectStartImg_.y(), rectEndImg_.y()));
    const int x2 = std::min(src_.cols - 1, std::max(rectStartImg_.x(), rectEndImg_.x()));
    const int y2 = std::min(src_.rows - 1, std::max(rectStartImg_.y(), rectEndImg_.y()));
    if (x2 < x1 || y2 < y1) { candMask_ = lastPolyMask_; return; }
    lastPolyMask_(cv::Rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1)).setTo(255);
    rebuildCandidatesFromPoly();
}

void CannyViewWidget::computeCandidatesFromPoly(const std::vector<cv::Point>& poly)
{
    lastPolyMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    candMask_     = cv::Mat::zeros(src_.size(), CV_8UC1);
    if (poly.size() < 3 || src_.empty()) return;
    cv::fillConvexPoly(lastPolyMask_, poly.data(), int(poly.size()), cv::Scalar(255));
    rebuildCandidatesFromPoly();
}

void CannyViewWidget::rebuildCandidatesFromPoly()
{
    candMask_ = cv::Mat::zeros(src_.size(), CV_8UC1);
    if (lastPolyMask_.empty() || src_.empty()) return;

    const int rows = src_.rows, cols = src_.cols;
    const bool haveOutline = !outline_.empty();

    if (labels_.empty()) return;

    // Component modes — count pixels of each component inside the figure
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
    // select components
    std::vector<uchar> include(labelSize_.size(), 0);
    for (int L = 1; L < int(labelSize_.size()); ++L) {
        if (countIn[L] == 0) continue;
        if (candidateMode_ == CandMode::Touching) {
            include[L] = 1;
        } else if (candidateMode_ == CandMode::Inside) {
            if (countIn[L] == labelSize_[L]) include[L] = 1;
        }
    }
    // candMask = pixels of included labels, minus outline, src<255
    for (int y = 0; y < rows; ++y) {
        const int*   lr = labels_.ptr<int>(y);
        const uchar* sr = src_.ptr<uchar>(y);
        const uchar* orl = haveOutline ? outline_.ptr<uchar>(y) : nullptr;
        uchar* cr = candMask_.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            const int L = lr[x];
            if (L <= 0 || !include[L]) continue;
            if (sr[x] >= 255) continue;
            if (orl && orl[x]) continue;
            if (!passesFilter(x, y)) continue;
            cr[x] = 255;
        }
    }
}

std::vector<cv::Point> CannyViewWidget::stripCorners(
        const QPoint& p1, const QPoint& p2, const QPoint& cursor) const
{
    const double ax = p2.x() - p1.x();
    const double ay = p2.y() - p1.y();
    const double len = std::hypot(ax, ay);
    std::vector<cv::Point> out;
    if (len < 1e-6) return out;
    const double ux = ax / len, uy = ay / len;
    const double px = -uy,      py = ux;      // perp (90° CCW)
    const double cx = cursor.x() - p1.x();
    const double cy = cursor.y() - p1.y();
    const double u  = cx * px + cy * py;       // signed perpendicular projection
    const double wx = px * u,   wy = py * u;
    out.reserve(4);
    out.emplace_back(p1.x(),                p1.y());
    out.emplace_back(p2.x(),                p2.y());
    out.emplace_back(int(std::round(p2.x() + wx)), int(std::round(p2.y() + wy)));
    out.emplace_back(int(std::round(p1.x() + wx)), int(std::round(p1.y() + wy)));
    return out;
}

void CannyViewWidget::cancelStrip()
{
    stripPhase_ = StripPhase::None;
    update();
}

void CannyViewWidget::finishStrip(const QPoint& widthRef)
{
    const auto poly = stripCorners(stripP1_, stripP2_, widthRef);
    stripPhase_ = StripPhase::None;
    if (poly.empty()) { update(); return; }
    computeCandidatesFromPoly(poly);
    if (cv::countNonZero(candMask_) == 0) {
        cancelRectSelection();
        update();
        return;
    }
    splitByThreshold();
    rectShowing_ = true;
    rectOverlayDirty_ = true;
    update();
    emit rectSelectionFinished();
}

void CannyViewWidget::splitByThreshold()
{
    if (candMask_.empty()) return;
    yellowMask_ = cv::Mat::zeros(candMask_.size(), CV_8UC1);
    orangeMask_ = cv::Mat::zeros(candMask_.size(), CV_8UC1);
    const int rows = candMask_.rows, cols = candMask_.cols;
    for (int y = 0; y < rows; ++y) {
        const uchar* cr = candMask_.ptr<uchar>(y);
        const uchar* sr = src_.ptr<uchar>(y);
        uchar* yr = yellowMask_.ptr<uchar>(y);
        uchar* orr = orangeMask_.ptr<uchar>(y);
        for (int x = 0; x < cols; ++x) {
            if (!cr[x]) continue;
            if (sr[x] <= rectThreshold_) orr[x] = 255;
            else                          yr[x]  = 255;
        }
    }
}

void CannyViewWidget::rebuildRectOverlay()
{
    if (!rectShowing_) { rectOverlayImage_ = {}; return; }
    cv::Mat rgba(orangeMask_.size(), CV_8UC4, cv::Scalar(0,0,0,0));
    const int rows = rgba.rows, cols = rgba.cols;
    for (int y = 0; y < rows; ++y) {
        const uchar* yr = yellowMask_.ptr<uchar>(y);
        const uchar* orr = orangeMask_.ptr<uchar>(y);
        cv::Vec4b* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            if (orr[x])     dr[x] = cv::Vec4b(255, 140, 0, 210);   // orange
            else if (yr[x]) dr[x] = cv::Vec4b(200, 200, 35, 190);  // dimmed yellow
        }
    }
    rectOverlayImage_ = QImage(rgba.data, cols, rows, int(rgba.step),
                               QImage::Format_RGBA8888).copy();
    rectOverlayDirty_ = false;
}

bool CannyViewWidget::applyOp(QPoint seedQ, bool add)
{
    return applyOp(seedQ, add, joinTol_, allDarker_, conn8_);
}

bool CannyViewWidget::applyOp(QPoint seedQ, bool add, int joinTol, bool allDarker, bool conn8)
{
    if (src_.empty()) return false;
    const int ix = seedQ.x(), iy = seedQ.y();
    if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) return false;
    if (src_.at<uchar>(iy, ix) >= 255) return false;  // white — never a seed
    floodSelectSameValue(cv::Point(ix, iy), joinTol, allDarker, conn8);
    if (cv::countNonZero(selMask_) == 0) { selDirty_ = true; return false; }
    if (add) {
        cv::bitwise_or(outline_, selMask_, outline_);
    } else {
        cv::Mat keep;
        cv::bitwise_not(selMask_, keep);
        cv::bitwise_and(outline_, keep, outline_);
    }
    selMask_.setTo(0);
    selDirty_ = true;
    outlineDirty_ = true;
    if (!showSource_ && !showOutline_) rebuildDisplay();
    update();
    return true;
}

cv::Point CannyViewWidget::pickOutlineNear(const cv::Point& p, int radius) const
{
    if (outline_.empty()) return {-1, -1};
    if (hideDone_) return {-1, -1};
    int bestD2 = INT_MAX;
    cv::Point r(-1, -1);
    const int y0 = std::max(0, p.y - radius), y1 = std::min(outline_.rows - 1, p.y + radius);
    const int x0 = std::max(0, p.x - radius), x1 = std::min(outline_.cols - 1, p.x + radius);
    for (int y = y0; y <= y1; ++y) {
        const uchar* row = outline_.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            if (!row[x]) continue;
            const int d2 = (x - p.x)*(x - p.x) + (y - p.y)*(y - p.y);
            if (d2 < bestD2) { bestD2 = d2; r = cv::Point(x, y); }
        }
    }
    return r;
}

void CannyViewWidget::floodSelectSameValue(const cv::Point& seed, int joinTol, bool allDarker, bool conn8)
{
    clearSelection();
    if (src_.empty()) return;
    if (seed.x < 0 || seed.y < 0 ||
        seed.x >= src_.cols || seed.y >= src_.rows) return;

    const int target = src_.at<uchar>(seed);
    if (target >= 255) { selDirty_ = true; return; }   // do not floodfill the white background

    // Range of accepted values:
    //   base:        v == target
    //   join_tol N:  v ∈ [target-N, target+N]
    //   all_darker:  lo = 0  (and still hi = target+N)
    const int hi = std::min(254, target + joinTol);
    const int lo = allDarker ? 0 : std::max(0, target - joinTol);

    const int rows = src_.rows, cols = src_.cols;
    std::vector<cv::Point> stack;
    stack.reserve(256);
    stack.push_back(seed);
    selMask_.at<uchar>(seed) = 255;
    static constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy8[8] = {-1,-1,-1,  0, 0,  1, 1, 1};
    static constexpr int dx4[4] = { 0, -1, 1, 0};
    static constexpr int dy4[4] = {-1,  0, 0, 1};
    const int nNb = conn8 ? 8 : 4;
    const int* dxN = conn8 ? dx8 : dx4;
    const int* dyN = conn8 ? dy8 : dy4;
    while (!stack.empty()) {
        const cv::Point p = stack.back();
        stack.pop_back();
        for (int k = 0; k < nNb; ++k) {
            const int nx = p.x + dxN[k];
            const int ny = p.y + dyN[k];
            if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
            if (selMask_.at<uchar>(ny, nx)) continue;
            const int v = src_.at<uchar>(ny, nx);
            if (v < lo || v > hi) continue;
            selMask_.at<uchar>(ny, nx) = 255;
            stack.emplace_back(nx, ny);
        }
    }
    selDirty_ = true;
}

void CannyViewWidget::updateCursorForMods(Qt::KeyboardModifiers m)
{
    if (panning_) { setCursor(Qt::ClosedHandCursor); return; }
    if (!editEnabled_) { setCursor(Qt::OpenHandCursor); return; }
    if (m & Qt::ControlModifier) setCursor(makePickCursor());
    else if (m & Qt::ShiftModifier) setCursor(Qt::CrossCursor);
    else setCursor(Qt::OpenHandCursor);
}

void CannyViewWidget::setEditEnabled(bool on)
{
    if (editEnabled_ == on) return;
    editEnabled_ = on;
    if (!on) {
        // Cancel any in-progress selections so a later re-enable starts clean.
        cancelStrip();
        cancelRectSelection();
    }
    updateCursorForMods(Qt::NoModifier);
    update();
}

void CannyViewWidget::wheelEvent(QWheelEvent* e)
{
    const double f = (e->angleDelta().y() > 0) ? 1.2 : 1.0/1.2;
    applyZoomAt(e->position().toPoint(), f);
}

void CannyViewWidget::mousePressEvent(QMouseEvent* e)
{
    setFocus();

    // Right click — cancels strip selection
    if (e->button() == Qt::RightButton) {
        if (stripPhase_ != StripPhase::None) { cancelStrip(); return; }
        if (thrStripPhase_ != ThrStrip::None) {
            thrStripPhase_ = ThrStrip::None;
            update();
            return;
        }
    }

    if (e->button() != Qt::LeftButton) return;

    // if a strip is already started (P1Set / P2Set) — each click advances the phase
    if (stripPhase_ == StripPhase::P1Set || stripPhase_ == StripPhase::P2Set) {
        if (src_.empty()) return;
        const QPointF ip = widgetToImage(e->pos());
        const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
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

    const auto mods = e->modifiers();
    if (fitMode_ && (mods & (Qt::ControlModifier | Qt::ShiftModifier))) {
        // editing blocked during Fit to others
        return;
    }
    if (thresholdMode_ && (mods & Qt::ControlModifier)) {
        // single-click editing blocked
        return;
    }
    if (thresholdMode_ && (mods & Qt::ShiftModifier)) {
        if (!thresholdLimitRegion_) return;  // Shift without limit-region: ignore
        if (src_.empty()) return;
        const QPointF ip = widgetToImage(e->pos());
        const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
        if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) return;
        // if a strip is already in progress — advance phase (P1Set→P2Set→finish)
        if (thrStripPhase_ == ThrStrip::P1Set) {
            thrStripP2_ = QPoint(ix, iy);
            thrStripCursor_ = thrStripP2_;
            thrStripPhase_ = ThrStrip::P2Set;
            update();
            return;
        }
        if (thrStripPhase_ == ThrStrip::P2Set) {
            finishThrStripRegion(QPoint(ix, iy));
            return;
        }
        // start: tentative — drag>4px → axis rect, small move → strip P1
        thrStripPhase_ = ThrStrip::Tentative;
        thrStripP1_ = QPoint(ix, iy);
        thrStripCursor_ = thrStripP1_;
        thrStripPressWidget_ = e->pos();
        thrDragging_ = false;
        update();
        return;
    }
    if (thresholdMode_) {
        // without modifier — cancel strip in progress (right click caught above)
    }
    if ((mods & (Qt::ControlModifier | Qt::ShiftModifier)) && !editEnabled_) {
        // Editing disabled in this view mode — fall through to panning.
    } else if (mods & (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (src_.empty()) return;
        const QPointF ip = widgetToImage(e->pos());
        const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
        if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) return;
        const int r = std::max(0, int(std::round(8.0 / std::max(scale_, 0.01))));

        // red (in outline) — always clickable (removal). Otherwise only
        // pixels passing the filter are available (when adding / Shift).
        if (mods & Qt::ControlModifier) {
            const cv::Point click(ix, iy);
            const bool onOutlineExact = !outline_.empty() && outline_.at<uchar>(iy, ix);
            const bool clickedOnPassing =
                !onOutlineExact && (src_.at<uchar>(iy, ix) < 255) && passesFilter(ix, iy);

            cv::Point seed(-1, -1);
            bool add = true;

            if (onOutlineExact) {
                // click exactly on a red pixel → remove
                seed = click;
                add = false;
            } else if (clickedOnPassing) {
                // Click directly on a matching pixel → add (do not search outline).
                // Always use the clicked pixel; no darkest-near search.
                seed = click;
                add = true;
            } else {
                // click on "empty" — search in radius for both candidates, pick the closer one
                cv::Point cOut  = pickOutlineNear(click, r);
                cv::Point cGray = (r > 0) ? pickSeedNear(click, r) : cv::Point(-1, -1);
                auto d2 = [&](cv::Point p) {
                    if (p.x < 0) return INT_MAX;
                    return (p.x - ix)*(p.x - ix) + (p.y - iy)*(p.y - iy);
                };
                const int dOut  = d2(cOut);
                const int dGray = d2(cGray);
                if (dGray == INT_MAX && dOut == INT_MAX) return;
                if (dGray <= dOut) { seed = cGray; add = true; }
                else               { seed = cOut;  add = false; }
            }

            const QPoint qseed(seed.x, seed.y);
            if (applyOp(qseed, add)) emit outlineOp(qseed, add, joinTol_, allDarker_, conn8_);
            return;
        }

        // Shift: start "tentative" — drag > 4 px → axis rect,
        // small move at release → oriented strip (P1 set)
        cancelRectSelection();
        cancelStrip();
        stripPhase_ = StripPhase::Tentative;
        stripP1_ = QPoint(ix, iy);
        stripCursor_ = stripP1_;
        stripPressWidget_ = e->pos();
        update();
        return;
    }

    panning_ = true;
    lastMousePos_ = e->pos();
    pressPos_ = e->pos();
    panOffsetAtPress_ = panOffset_;
    setCursor(Qt::ClosedHandCursor);
}

void CannyViewWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (panning_) {
        const QPoint d = e->pos() - lastMousePos_;
        panOffset_ += QPointF(d);
        lastMousePos_ = e->pos();
        update();
    } else if (stripPhase_ == StripPhase::Tentative) {
        // widget movement > 4 px → switch to axis-aligned rect
        const QPoint d = e->pos() - stripPressWidget_;
        if (d.x()*d.x() + d.y()*d.y() > 16) {
            stripPhase_ = StripPhase::None;
            rectDragging_ = true;
            rectStartImg_ = stripP1_;
            const QPointF ip = widgetToImage(e->pos());
            rectEndImg_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        }
        update();
    } else if (rectDragging_) {
        const QPointF ip = widgetToImage(e->pos());
        rectEndImg_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        update();
    } else if (stripPhase_ == StripPhase::P1Set || stripPhase_ == StripPhase::P2Set) {
        const QPointF ip = widgetToImage(e->pos());
        stripCursor_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        update();
    } else if (thrStripPhase_ == ThrStrip::Tentative) {
        const QPoint d = e->pos() - thrStripPressWidget_;
        if (d.x()*d.x() + d.y()*d.y() > 16) {
            thrStripPhase_ = ThrStrip::None;
            thrDragging_ = true;
            thrRectStartImg_ = thrStripP1_;
            const QPointF ip = widgetToImage(e->pos());
            thrRectEndImg_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        }
        update();
    } else if (thrDragging_) {
        const QPointF ip = widgetToImage(e->pos());
        thrRectEndImg_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        update();
    } else if (thrStripPhase_ == ThrStrip::P1Set || thrStripPhase_ == ThrStrip::P2Set) {
        const QPointF ip = widgetToImage(e->pos());
        thrStripCursor_ = QPoint(int(std::floor(ip.x())), int(std::floor(ip.y())));
        update();
    } else {
        updateCursorForMods(e->modifiers());
    }
    emitHud(e->pos());
}

void CannyViewWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    if (panning_) {
        panning_ = false;
        updateCursorForMods(e->modifiers());
        update();
        return;
    }
    if (rectDragging_) {
        rectDragging_ = false;
        computeCandidates();
        if (cv::countNonZero(candMask_) == 0) {
            cancelRectSelection();
            update();
            return;
        }
        splitByThreshold();
        rectShowing_ = true;
        rectOverlayDirty_ = true;
        update();
        emit rectSelectionFinished();
        return;
    }
    if (stripPhase_ == StripPhase::Tentative) {
        // small move — enter oriented strip mode (P1 set)
        stripPhase_ = StripPhase::P1Set;
        stripCursor_ = stripP1_;
        update();
        return;
    }
    if (thrDragging_) {
        thrDragging_ = false;
        finishThrRectRegion();
        update();
        return;
    }
    if (thrStripPhase_ == ThrStrip::Tentative) {
        thrStripPhase_ = ThrStrip::P1Set;
        thrStripCursor_ = thrStripP1_;
        update();
        return;
    }
}

void CannyViewWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (stripPhase_ != StripPhase::None) { cancelStrip(); return; }
        if (thrStripPhase_ != ThrStrip::None) {
            thrStripPhase_ = ThrStrip::None;
            update();
            return;
        }
    }
    if (e->key() == Qt::Key_Backspace) {
        if (stripPhase_ == StripPhase::P2Set) {
            stripPhase_ = StripPhase::P1Set;
            stripCursor_ = stripP1_;
            update();
            return;
        }
        if (stripPhase_ == StripPhase::P1Set) {
            cancelStrip();
            return;
        }
    }
    if (e->key() == Qt::Key_Control || e->key() == Qt::Key_Shift) {
        updateCursorForMods(e->modifiers()
                            | (e->key() == Qt::Key_Control ? Qt::ControlModifier : Qt::ShiftModifier));
        return;
    }
    QWidget::keyPressEvent(e);
}

void CannyViewWidget::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Control || e->key() == Qt::Key_Shift) {
        auto m = e->modifiers();
        if (e->key() == Qt::Key_Control) m &= ~Qt::ControlModifier;
        else m &= ~Qt::ShiftModifier;
        updateCursorForMods(m);
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void CannyViewWidget::leaveEvent(QEvent*)
{
    emit hudUpdate(QString());
}

void CannyViewWidget::emitHud(const QPoint& widgetPos)
{
    if (src_.empty()) return;
    const QPointF ip = widgetToImage(widgetPos);
    const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
    QString s;
    if (ix >= 0 && iy >= 0 && ix < src_.cols && iy < src_.rows) {
        const int v = src_.at<uchar>(iy, ix);
        const bool ol = !outline_.empty() && outline_.at<uchar>(iy, ix);
        s = QString("X=%1 Y=%2  gray=%3%4  zoom=%5%  range=[%6..%7]%8")
              .arg(ix).arg(iy).arg(v)
              .arg(ol ? "  [outline]" : "")
              .arg(int(scale_*100))
              .arg(lo_).arg(hi_)
              .arg(filter_ ? "  filter=on" : "");
    } else {
        s = QString("xy=(--,--)  zoom=%1%  range=[%2..%3]%4")
              .arg(int(scale_*100)).arg(lo_).arg(hi_)
              .arg(filter_ ? "  filter=on" : "");
    }
    emit hudUpdate(s);
}