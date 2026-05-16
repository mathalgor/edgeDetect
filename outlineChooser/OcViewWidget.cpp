#include "OcViewWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <vector>

OcViewWidget::OcViewWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void OcViewWidget::setData(const cv::Mat& srcGray,
                           const cv::Mat& o1FileFmt,
                           const cv::Mat& o2FileFmt,
                           const cv::Mat& outFileFmt)
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
    dirty_ = false;
    emit dirtyChanged(false);
    pendingFit_ = true;
    rebuildVisualization();
    if (width() > 0 && height() > 0) fitToWindow();
    update();
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

void OcViewWidget::rebuildVisualization()
{
    if (src_.empty()) { vis_ = {}; return; }
    const int rows = src_.rows, cols = src_.cols;
    cv::Mat rgba(rows, cols, CV_8UC4);
    for (int y = 0; y < rows; ++y) {
        const uchar* sr = src_.ptr<uchar>(y);
        const uchar* o1 = o1_.ptr<uchar>(y);
        const uchar* o2 = o2_.ptr<uchar>(y);
        const uchar* oo = out_.ptr<uchar>(y);
        cv::Vec4b* dr = rgba.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            const bool in1 = o1[x], in2 = o2[x], inOut = oo[x];
            cv::Vec4b c;
            if (inOut)            c = cv::Vec4b(0,   0,   0,   255);  // black
            else if (in1 && in2)  c = cv::Vec4b(180, 180, 0,   220);  // dark yellow
            else if (in1)         c = cv::Vec4b(0,   180, 0,   255);  // green
            else if (in2)         c = cv::Vec4b(220, 0,   0,   255);  // red
            else {
                // white background with a slight gray hint (to see the outlines)
                const uchar g = sr[x];
                c = cv::Vec4b(g, g, g, 255);
            }
            dr[x] = c;
        }
    }
    vis_ = QImage(rgba.data, cols, rows, int(rgba.step),
                  QImage::Format_RGBA8888).copy();
}

void OcViewWidget::updateVisualizationAt(const std::vector<cv::Point>& pts)
{
    if (vis_.isNull()) return;
    for (const auto& p : pts) {
        const int x = p.x, y = p.y;
        const bool in1 = o1_.at<uchar>(y, x);
        const bool in2 = o2_.at<uchar>(y, x);
        const bool inOut = out_.at<uchar>(y, x);
        QRgb c;
        if (inOut)            c = qRgba(0,   0,   0,   255);
        else if (in1 && in2)  c = qRgba(180, 180, 0,   220);
        else if (in1)         c = qRgba(0,   180, 0,   255);
        else if (in2)         c = qRgba(220, 0,   0,   255);
        else {
            const uchar g = src_.at<uchar>(y, x);
            c = qRgba(g, g, g, 255);
        }
        vis_.setPixel(x, y, c);
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
    if (e->button() != Qt::LeftButton) return;
    if (vis_.isNull()) return;
    const QPointF ip = widgetToImage(e->pos());
    const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
    if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) {
        panning_ = true;
        lastMousePos_ = e->pos();
        pressPos_ = e->pos();
        panOffsetAtPress_ = panOffset_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    // potential click on a colored pixel — we'll check on release
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
    }
    emitHud(e->pos());
}

void OcViewWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const bool wasPanning = panning_;
    panning_ = false;
    setCursor(Qt::ArrowCursor);
    // click without movement (>3 px = pan)
    const QPoint d = e->pos() - pressPos_;
    if (d.x()*d.x() + d.y()*d.y() > 9) { update(); return; }
    (void)wasPanning;

    if (vis_.isNull()) return;
    const QPointF ip = widgetToImage(e->pos());
    const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
    if (ix < 0 || iy < 0 || ix >= src_.cols || iy >= src_.rows) return;

    const int c = colorAt(ix, iy);
    if (c == 0) return;  // white — do nothing
    std::vector<cv::Point> pts;
    if (c == 4) {
        // black → deselect the WHOLE same-value segment (those pixels which are in out)
        floodAnyColor(ix, iy, pts);
        for (const auto& p : pts) {
            if (out_.at<uchar>(p.y, p.x)) out_.at<uchar>(p.y, p.x) = 0;
        }
    } else {
        // green/red/yellow → add segment-pixels of the same color
        floodSegment(ix, iy, c, pts);
        for (const auto& p : pts) out_.at<uchar>(p.y, p.x) = 255;
    }
    if (!pts.empty()) {
        updateVisualizationAt(pts);
        if (!dirty_) { dirty_ = true; emit dirtyChanged(true); }
        update();
    }
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