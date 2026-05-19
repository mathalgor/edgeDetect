#include "McViewWidget.h"

#include <QKeyEvent>
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

void McViewWidget::buildDefaultPresets()
{
    presets_ = {
        {"Result only",       Bg::Plain,    true,  false},
        {"Original",          Bg::Original, false, false},
        {"Original + Result", Bg::Original, true,  false},
    };
    presetIndex_ = 0;
}

void McViewWidget::setPresetIndex(int i)
{
    if (i < 0 || i >= static_cast<int>(presets_.size())) return;
    if (i == presetIndex_) return;
    presetIndex_ = i;
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
            int size = 0;
            std::uint64_t sumR = 0;
            q.push({x, y});
            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                ++size;
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
            labelSize_.push_back(size);
            labelGray_.push_back(g);
            labelAvgR_.push_back(static_cast<uchar>(
                std::min<std::uint64_t>(255, sumR / std::max(1, size))));
        }
    }
}

void McViewWidget::rebuildVisualization()
{
    if (srcGray_.empty()) { vis_ = QImage(); return; }
    const int H = srcGray_.rows, W = srcGray_.cols;
    vis_ = QImage(W, H, QImage::Format_RGBA8888);

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
                const int v = 255 - rowG[x];
                br = bg = bb = v;
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
                br = 0; bg = 200; bb = 0;
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
    polyVerts_.clear();
    polyOpen_ = false;
    polyHoverValid_ = false;
    update();
    emit polygonFinished();
}

void McViewWidget::cancelPolygon()
{
    polyVerts_.clear();
    polyOpen_ = false;
    polyHoverValid_ = false;
    polyMask_.release();
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

int McViewWidget::filterCountIf(FilterMode mode, FilterAction action,
                                int gMax, int rMax) const
{
    if (polyMask_.empty() || labels_.empty()) return 0;
    const int H = srcGray_.rows, W = srcGray_.cols;
    const int nL = static_cast<int>(labelSize_.size());

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
        if (labelGray_[L] > gMax) continue;
        if (labelAvgR_[L] > rMax) continue;
        eligible[L] = 1;
    }

    int n = 0;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        const uchar* rowM = polyMask_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0 || !eligible[L]) continue;
            if (mode == FilterMode::Touching && !rowM[x]) continue;
            const bool isIn = rowOut[x] == 255;
            if (action == FilterAction::Add && !isIn) ++n;
            else if (action == FilterAction::Remove && isIn) ++n;
        }
    }
    return n;
}

int McViewWidget::setFilterPreview(FilterMode mode, FilterAction action,
                                   int gMax, int rMax)
{
    if (polyMask_.empty() || labels_.empty()) {
        clearFilterPreview();
        return 0;
    }
    const int H = srcGray_.rows, W = srcGray_.cols;
    const int nL = static_cast<int>(labelSize_.size());

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
        if (labelGray_[L] > gMax) continue;
        if (labelAvgR_[L] > rMax) continue;
        eligible[L] = 1;
    }

    previewMask_ = cv::Mat::zeros(srcGray_.size(), CV_8UC1);
    previewIsAdd_ = (action == FilterAction::Add);
    int n = 0;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        const uchar* rowM = polyMask_.ptr<uchar>(y);
        uchar* rowP = previewMask_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0 || !eligible[L]) continue;
            if (mode == FilterMode::Touching && !rowM[x]) continue;
            const bool isIn = rowOut[x] == 255;
            if (action == FilterAction::Add && !isIn) { rowP[x] = 255; ++n; }
            else if (action == FilterAction::Remove && isIn) { rowP[x] = 255; ++n; }
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
                                int gMax, int rMax)
{
    if (polyMask_.empty() || labels_.empty()) { cancelPolygon(); return; }
    const int H = srcGray_.rows, W = srcGray_.cols;
    const int nL = static_cast<int>(labelSize_.size());

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
        if (labelGray_[L] > gMax) continue;
        if (labelAvgR_[L] > rMax) continue;
        eligible[L] = 1;
    }

    std::vector<cv::Point> pts;
    for (int y = 0; y < H; ++y) {
        const int* rowL = labels_.ptr<int>(y);
        const uchar* rowOut = outResult_.ptr<uchar>(y);
        const uchar* rowM = polyMask_.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            const int L = rowL[x];
            if (L == 0 || !eligible[L]) continue;
            if (mode == FilterMode::Touching && !rowM[x]) continue;
            const bool isIn = rowOut[x] == 255;
            if (action == FilterAction::Add && !isIn) pts.emplace_back(x, y);
            else if (action == FilterAction::Remove && isIn) pts.emplace_back(x, y);
        }
    }

    polyMask_.release();
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
        // Captured polygon (yellow fill) + optional preview pixels overlay
        // (cyan = will be added, magenta = will be removed).
        QImage overlay(polyMask_.cols, polyMask_.rows, QImage::Format_RGBA8888);
        overlay.fill(0);
        const bool havePrev = !previewMask_.empty();
        const QRgb prevCol = previewIsAdd_
            ? qRgba(0, 230, 230, 230)        // cyan
            : qRgba(230, 0, 230, 230);       // magenta
        for (int y = 0; y < polyMask_.rows; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
            const uchar* m = polyMask_.ptr<uchar>(y);
            const uchar* pv = havePrev ? previewMask_.ptr<uchar>(y) : nullptr;
            for (int x = 0; x < polyMask_.cols; ++x) {
                if (pv && pv[x]) row[x] = prevCol;
                else if (m[x])   row[x] = qRgba(255, 200, 0, 50);
            }
        }
        p.drawImage(0, 0, overlay);
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
    if (e->button() == Qt::MiddleButton ||
        (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier))) {
        panning_ = true;
        lastMousePos_ = e->pos();
        panOffsetAtPress_ = panOffset_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (editLocked_) { emit editBlocked(); return; }
        const QPointF ip = widgetToImage(e->pos());
        const int ix = static_cast<int>(std::floor(ip.x()));
        const int iy = static_cast<int>(std::floor(ip.y()));
        if (ix < 0 || iy < 0 || ix >= srcGray_.cols || iy >= srcGray_.rows) return;
        if (!polyOpen_) {
            polyMask_.release();
            polyVerts_.clear();
            polyOpen_ = true;
        }
        polyVerts_.emplace_back(ix, iy);
        update();
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
    QWidget::keyPressEvent(e);
}

void McViewWidget::leaveEvent(QEvent*)
{
    polyHoverValid_ = false;
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
