#include "AlignViewWidget.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QtMath>
#include <QPainter>
#include <cmath>
#include <limits>

namespace {

const double minScale = 0.01;
const double maxScale = 100.0;

static QImage matToGrrayscaleQImage(const cv::Mat& src)
{
    if (src.empty())
        return QImage();

    cv::Mat grray;
    if (src.channels() == 1) {
        grray = src;
    } else {
        cv::cvtColor(src, grray, cv::COLOR_BGR2GRAY);
    }

    QImage img(grray.cols, grray.rows, QImage::Format_Grayscale8);
    for (int y = 0; y < grray.rows; ++y) {
        const uchar* srcRow = grray.ptr<uchar>(y);
        uchar* dstRow = img.scanLine(y);
        std::memcpy(dstRow, srcRow, static_cast<size_t>(grray.cols));
    }
    return img;
}

static QImage outlineToRedOverlay(const cv::Mat& src)
{
    if (src.empty())
        return QImage();

    cv::Mat grray;
    if (src.channels() == 1) {
        grray = src;
    } else {
        cv::cvtColor(src, grray, cv::COLOR_BGR2GRAY);
    }

    QImage img(grray.cols, grray.rows, QImage::Format_ARGB32);

    for (int y = 0; y < grray.rows; ++y) {
        const uchar* srcRow = grray.ptr<uchar>(y);
        QRgb* dstRow = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < grray.cols; ++x) {
            uchar v = srcRow[x];

            if (v >= 250) {
                dstRow[x] = qRgba(0, 0, 0, 0);
            } else {
                int inv = 255 - int(v);
                int alpha = inv;
                int r = 255;
                int g = 255 - inv / 2;
                int b = 255 - inv / 2;
                dstRow[x] = qRgba(r, g, b, alpha);
            }
        }
    }

    return img;
}

} // namespace

AlignViewWidget::AlignViewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    // widget expands to fill all available space
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void AlignViewWidget::setSrcImage(const cv::Mat& src)
{
    srcMat_ = src.clone();
    srcSize_ = QSize(srcMat_.cols, srcMat_.rows);
    updateSrcQImage();
   srcWarpedDirty_ = true;

    // do NOT reset view – preserve screen scale/pan between images
    // call resetView() to force a reset

    update();
    emit parametersChanged();
}

void AlignViewWidget::setOutlineImage(const cv::Mat& outline)
{
    outlineMat_ = outline.clone();
    outlineSize_ = QSize(outlineMat_.cols, outlineMat_.rows);
    updateOutlineQImage();
    update();
    emit parametersChanged();
}

void AlignViewWidget::setShowOutline(bool show)
{
    if (showOutline_ == show)
        return;
    showOutline_ = show;
    update();
}

void AlignViewWidget::setShowSrc(bool show)
{
    if (showSrc_ == show)
        return;
    showSrc_ = show;
    update();
}

void AlignViewWidget::setShowUncertainty(bool show)
{
    if (showUncertainty_ == show)
        return;
    showUncertainty_ = show;
    update();
}

void AlignViewWidget::resetView()
{
    autoFit_ = true;
    panOffset_ = QPointF(0, 0);
    update();
}

void AlignViewWidget::setDelta(double dx, double dy)
{
    deltaX_ = dx;
    deltaY_ = dy;
    update();
    emit parametersChanged();
}

void AlignViewWidget::setSrcScale(double sx, double sy)
{
    if (sx < 1.0) sx = 1.0;
    if (sy < 1.0) sy = 1.0;
    if (scaleX_ != sx || scaleY_ != sy) {
        scaleX_ = sx;
        scaleY_ = sy;
       srcWarpedDirty_ = true;
    }
    update();
    emit parametersChanged();
}

void AlignViewWidget::setPinsToDraw(const QVector<DrawPin>& pins)
{
    pinsToDraw_ = pins;
    update();
}

void AlignViewWidget::setQuadX(double q)
{
    if (quadX_ == q)
        return;
    quadX_ = q;
   srcWarpedDirty_ = true;
    update();
    emit parametersChanged();
}

void AlignViewWidget::setQuadY(double q)
{
    if (quadY_ == q)
        return;
    quadY_ = q;
   srcWarpedDirty_ = true;
    update();
    emit parametersChanged();
}

void AlignViewWidget::setRotXY(double v)
{
    if (rotXY_ == v) return;
    rotXY_ = v;
   srcWarpedDirty_ = true;
    update();
    emit parametersChanged();
}

void AlignViewWidget::setRotYX(double v)
{
    if (rotYX_ == v) return;
    rotYX_ = v;
   srcWarpedDirty_ = true;
    update();
    emit parametersChanged();
}

void AlignViewWidget::setCrossXY(double v)
{
    if (crossXY_ == v) return;
    crossXY_ = v;
   srcWarpedDirty_ = true;
    update();
    emit parametersChanged();
}

void AlignViewWidget::setCrossYX(double v)
{
    if (crossYX_ == v) return;
    crossYX_ = v;
   srcWarpedDirty_ = true;
    update();
    emit parametersChanged();
}

void AlignViewWidget::fitSrcToOutline()
{
    // Fit src to outline preserving aspect ratio
    if (srcImage_.isNull() || outlineOverlay_.isNull())
        return;

    double srcW = srcImage_.width();
    double srcH = srcImage_.height();
    double outW  = outlineOverlay_.width();
    double outH  = outlineOverlay_.height();

    if (srcW <= 0 || srcH <= 0)
        return;

    double srcAspect = srcW / srcH;
    double outAspect  = outW / outH;

    if (srcAspect > outAspect) {
        // src is wider – fit to outline width
        scaleX_ = outW;
        scaleY_ = outW / srcAspect;
    } else {
        // src is taller or equal – fit to outline height
        scaleY_ = outH;
        scaleX_ = outH * srcAspect;
    }

    deltaX_ = 0.0;
    deltaY_ = 0.0;

    update();
    emit parametersChanged();
}
template <typename>
constexpr auto AlignViewWidget::qt_create_metaobjectdata() {}

void AlignViewWidget::updateSrcQImage()
{
    srcImage_ = matToGrrayscaleQImage(srcMat_);
}

void AlignViewWidget::updateOutlineQImage()
{
    outlineOverlay_ = outlineToRedOverlay(outlineMat_);
}

bool AlignViewWidget::hasBothImages() const
{
    return !srcImage_.isNull() && !outlineOverlay_.isNull();
}

double AlignViewWidget::computeFitScale() const
{
    // Compute view scale so outline fits on widget
    if (outlineOverlay_.isNull())
        return 1.0;

    const QSize widgetSize = size();
    const QSize imgSize = outlineOverlay_.size();

    if (imgSize.isEmpty() || widgetSize.isEmpty())
        return 1.0;

    // fit outline to widget
    double sx = double(widgetSize.width()) / imgSize.width();
    double sy = double(widgetSize.height()) / imgSize.height();
    return std::min(sx, sy);
}

// Anchor swap: src is "anchored" to widget center (+pan), outline is positioned
// relative to src: outline.center = src.center - delta*viewScale.
// Ctrl+drag interpreted as "I'm moving the outline" – delta sign flipped in mouseMove.

QRectF AlignViewWidget::srcRectOnWidget() const
{
    if (srcImage_.isNull())
        return QRectF();

    double viewScale = autoFit_ ? computeFitScale() : viewScale_;

    double w = scaleX_ * viewScale;
    double h = scaleY_ * viewScale;

    QPointF srcCenter(width() / 2.0, height() / 2.0);
    if (!autoFit_)
        srcCenter += panOffset_;

    double x = srcCenter.x() - w * 0.5;
    double y = srcCenter.y() - h * 0.5;

    return QRectF(x, y, w, h);
}

QRectF AlignViewWidget::outlineRectOnWidget() const
{
    if (outlineOverlay_.isNull())
        return QRectF();

    double viewScale = autoFit_ ? computeFitScale() : viewScale_;

    double w = outlineOverlay_.width() * viewScale;
    double h = outlineOverlay_.height() * viewScale;

    QPointF srcCenter(width() / 2.0, height() / 2.0);
    if (!autoFit_)
        srcCenter += panOffset_;

    double dxScreen = deltaX_ * viewScale;
    double dyScreen = deltaY_ * viewScale;

    // outline.center = src.center - delta*viewScale (z anchor swap)
    QPointF outCenter = srcCenter - QPointF(dxScreen, dyScreen);

    double x = outCenter.x() - w * 0.5;
    double y = outCenter.y() - h * 0.5;

    return QRectF(x, y, w, h);
}

namespace {
// bbox parametrycznej o(u) = a*u + c*u^2 dla u in [-half, half] → (oMin, oMax)
inline void bboxQuad(double a, double c, double half, double& oMin, double& oMax)
{
    double oLeft  = -a * half + c * half * half;
    double oRight =  a * half + c * half * half;
    oMin = std::min(oLeft, oRight);
    oMax = std::max(oLeft, oRight);
    if (std::abs(c) > 1e-15) {
        double u_v = -a / (2.0 * c);
        if (u_v > -half && u_v < half) {
            double o_v = -a * a / (4.0 * c);
            oMin = std::min(oMin, o_v);
            oMax = std::max(oMax, o_v);
        }
    }
}

// inverse: given o find u such that a*u + c*u^2 = o; branch through 0
inline double invQuad(double o, double a, double c)
{
    double disc = a * a + 4.0 * c * o;
    if (disc < 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    double denom = a + std::sqrt(disc);
    if (std::abs(denom) < 1e-15)
        return (std::abs(c) > 1e-15) ? -a / (2.0 * c) : 0.0;
    return 2.0 * o / denom;
}
}

void AlignViewWidget::rebuildSrcWarpedIfNeeded()
{
    if (!srcWarpedDirty_)
        return;
   srcWarpedDirty_ = false;
   srcWarped_ = QImage();

    if (srcMat_.empty() || srcSize_.isEmpty() || scaleX_ <= 0 || scaleY_ <= 0)
        return;
    bool anyNonLinear =
        std::abs(quadX_)   > 1e-15 || std::abs(quadY_)   > 1e-15 ||
        std::abs(rotXY_)   > 1e-15 || std::abs(rotYX_)   > 1e-15 ||
        std::abs(crossXY_) > 1e-15 || std::abs(crossYX_) > 1e-15;
    if (!anyNonLinear)
        return;  // purely linear diagonal mode – handled by fast path

    double srcW = srcSize_.width();
    double srcH = srcSize_.height();
    double ax = scaleX_ / srcW;
    double ay = scaleY_ / srcH;
    double cx = quadX_, cy = quadY_;
    double bXY = rotXY_, bYX = rotYX_;
    double eXY = crossXY_, eYX = crossYX_;
    double halfW = srcW / 2.0;
    double halfH = srcH / 2.0;

    bool hasCross = std::abs(bXY) > 1e-15 || std::abs(bYX) > 1e-15
                 || std::abs(eXY) > 1e-15 || std::abs(eYX) > 1e-15;

    // Forward T(u, v) -> (ox, oy) w outline-centered (bez delty)
    auto forwardT = [&](double u, double v, double& ox, double& oy) {
        ox = ax*u + bXY*v + cx*u*u + eXY*u*v;
        oy = bYX*u + ay*v + cy*v*v + eYX*u*v;
    };

    // BBox: sample 4 corners + edge midpoints (8 points) + optional quadratic vertices
    double oxMin, oxMax, oyMin, oyMax;
    if (!hasCross) {
        // Separable – exact bbox per axis
        bboxQuad(ax, cx, halfW, oxMin, oxMax);
        bboxQuad(ay, cy, halfH, oyMin, oyMax);
    } else {
        // Coupled – bbox from sampling
        oxMin = oyMin =  std::numeric_limits<double>::infinity();
        oxMax = oyMax = -std::numeric_limits<double>::infinity();
        const int SAMPLES = 16;
        for (int i = 0; i <= SAMPLES; ++i) {
            double t = -1.0 + 2.0 * i / SAMPLES;
            // 4 edges
            double pts[4][2] = {
                { t * halfW, -halfH },
                { t * halfW,  halfH },
                { -halfW, t * halfH },
                {  halfW, t * halfH }
            };
            for (int k = 0; k < 4; ++k) {
                double ox, oy;
                forwardT(pts[k][0], pts[k][1], ox, oy);
                if (ox < oxMin) oxMin = ox;
                if (ox > oxMax) oxMax = ox;
                if (oy < oyMin) oyMin = oy;
                if (oy > oyMax) oyMax = oy;
            }
        }
        // Small margin
        double mw = (oxMax - oxMin) * 0.02 + 2.0;
        double mh = (oyMax - oyMin) * 0.02 + 2.0;
        oxMin -= mw; oxMax += mw;
        oyMin -= mh; oyMax += mh;
    }

    // Walidacja: NaN / Inf w bbox (singularne parametry) - przerwij
    if (!std::isfinite(oxMin) || !std::isfinite(oxMax) ||
        !std::isfinite(oyMin) || !std::isfinite(oyMax)) {
        return;
    }

    int canvasW = std::max(1, int(std::ceil(oxMax - oxMin)));
    int canvasH = std::max(1, int(std::ceil(oyMax - oyMin)));

    // Hard-cap: cv::remap requires sizes < SHRT_MAX (32767). For memory safety
    // (mapX + mapY = 2 * 4 * W * H bytes) we cap at 16384.
    // If bbox exceeds this – skip warp (src renders via linear path).
    constexpr int MAX_CANVAS = 16384;
    if (canvasW > MAX_CANVAS || canvasH > MAX_CANVAS) {
        qWarning("rebuildSrcWarpedIfNeeded: canvas %d x %d > %d - skipping warp",
                 canvasW, canvasH, MAX_CANVAS);
        return;
    }

   srcWarpedOMin_  = oxMin;
   srcWarpedOMinY_ = oyMin;

    cv::Mat mapX(canvasH, canvasW, CV_32FC1);
    cv::Mat mapY(canvasH, canvasW, CV_32FC1);

    if (!hasCross) {
        // Separable – precompute per axis (as before)
        std::vector<float> uByCx(canvasW), vByCy(canvasH);
        for (int cxp = 0; cxp < canvasW; ++cxp) {
            double o = oxMin + cxp + 0.5;
            double u = invQuad(o, ax, cx);
            uByCx[cxp] = float(u + halfW - 0.5);
        }
        for (int cyp = 0; cyp < canvasH; ++cyp) {
            double o = oyMin + cyp + 0.5;
            double v = invQuad(o, ay, cy);
            vByCy[cyp] = float(v + halfH - 0.5);
        }
        for (int cyp = 0; cyp < canvasH; ++cyp) {
            float* rowX = mapX.ptr<float>(cyp);
            float* rowY = mapY.ptr<float>(cyp);
            float vv = vByCy[cyp];
            for (int cxp = 0; cxp < canvasW; ++cxp) {
                rowX[cxp] = uByCx[cxp];
                rowY[cxp] = vv;
            }
        }
    } else {
        // Coupled – Newton per pixel
        // Initialization: linear inverse [aX bXY; bYX aY] * [u;v] = [ox;oy]
        double detLin = ax * ay - bXY * bYX;
        bool linInvOk = std::abs(detLin) > 1e-12;

        for (int cyp = 0; cyp < canvasH; ++cyp) {
            float* rowX = mapX.ptr<float>(cyp);
            float* rowY = mapY.ptr<float>(cyp);
            double oyTgt = oyMin + cyp + 0.5;
            for (int cxp = 0; cxp < canvasW; ++cxp) {
                double oxTgt = oxMin + cxp + 0.5;
                double u, v;
                if (linInvOk) {
                    u = ( ay   * oxTgt - bXY * oyTgt) / detLin;
                    v = (-bYX  * oxTgt + ax  * oyTgt) / detLin;
                } else {
                    u = oxTgt / (std::abs(ax) > 1e-12 ? ax : 1.0);
                    v = oyTgt / (std::abs(ay) > 1e-12 ? ay : 1.0);
                }
                // Newton: 6 iteracji wystarcza
                for (int it = 0; it < 6; ++it) {
                    double Fx, Fy;
                    forwardT(u, v, Fx, Fy);
                    Fx -= oxTgt; Fy -= oyTgt;
                    if (Fx*Fx + Fy*Fy < 1e-8) break;
                    double Jxx = ax + 2.0*cx*u + eXY*v;
                    double Jxy = bXY + eXY*u;
                    double Jyx = bYX + eYX*v;
                    double Jyy = ay + 2.0*cy*v + eYX*u;
                    double det = Jxx*Jyy - Jxy*Jyx;
                    if (std::abs(det) < 1e-14) break;
                    double du = ( Jyy*Fx - Jxy*Fy) / det;
                    double dv = (-Jyx*Fx + Jxx*Fy) / det;
                    u -= du;
                    v -= dv;
                }
                rowX[cxp] = float(u + halfW - 0.5);
                rowY[cxp] = float(v + halfH - 0.5);
            }
        }
    }

    cv::Mat src1ch;
    if (srcMat_.channels() == 1) src1ch = srcMat_;
    else cv::cvtColor(srcMat_, src1ch, cv::COLOR_BGR2GRAY);

    cv::Mat warped;
    cv::remap(src1ch, warped, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255));

    QImage img(warped.cols, warped.rows, QImage::Format_Grayscale8);
    for (int y = 0; y < warped.rows; ++y) {
        std::memcpy(img.scanLine(y), warped.ptr<uchar>(y), size_t(warped.cols));
    }
   srcWarped_ = img;
}

QRectF AlignViewWidget::srcWarpedRectOnWidget() const
{
    if (srcWarped_.isNull())
        return QRectF();

    double viewScale = autoFit_ ? computeFitScale() : viewScale_;

    // Canvas represents src in outline-centered space "without delta". In anchor-swap delta
    // cancels: outCenter = srcCenter - delta*vs, so
    //   canvas_origin = outCenter + (oMin) * vs + delta*vs
    //                 = srcCenter + (oMin) * vs
    QPointF srcCenter(width() / 2.0, height() / 2.0);
    if (!autoFit_)
        srcCenter += panOffset_;

    double w = srcWarped_.width()  * viewScale;
    double h = srcWarped_.height() * viewScale;

    double x = srcCenter.x() + srcWarpedOMin_  * viewScale;
    double y = srcCenter.y() + srcWarpedOMinY_ * viewScale;

    return QRectF(x, y, w, h);
}

void AlignViewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    double viewScale = autoFit_ ? computeFitScale() : viewScale_;

    // smooth for downscaling, nearest for upscaling
    p.setRenderHint(QPainter::SmoothPixmapTransform, viewScale <= 1.0);

    // draw src under outline
    if (showSrc_ && !srcImage_.isNull()) {
        bool nonLinear =
            std::abs(quadX_)   > 1e-15 || std::abs(quadY_)   > 1e-15 ||
            std::abs(rotXY_)   > 1e-15 || std::abs(rotYX_)   > 1e-15 ||
            std::abs(crossXY_) > 1e-15 || std::abs(crossYX_) > 1e-15;
        if (nonLinear) {
            rebuildSrcWarpedIfNeeded();
            if (!srcWarped_.isNull()) {
                p.drawImage(srcWarpedRectOnWidget(), srcWarped_);
            }
        } else {
            QRectF srcRect = srcRectOnWidget();
            p.drawImage(srcRect, srcImage_);
        }
    }

    // outline on top
    if (showOutline_ && !outlineOverlay_.isNull()) {
        QRectF outlineRect = outlineRectOnWidget();
        p.drawImage(outlineRect, outlineOverlay_);
    }

    // pins – blue arrows with numbers
    pinLabelRects_.clear();
    if (!pinsToDraw_.isEmpty() && !outlineOverlay_.isNull() && !outlineSize_.isEmpty()) {
        QRectF outRect = outlineRectOnWidget();
        if (!outRect.isNull()) {
            double sxFactor = outRect.width()  / double(outlineSize_.width());
            double syFactor = outRect.height() / double(outlineSize_.height());

            p.setRenderHint(QPainter::Antialiasing, true);
            QColor blue(40, 120, 255);
            QColor green(30, 170, 60);
            QFont font = p.font();
            font.setPointSize(10);
            font.setBold(true);
            p.setFont(font);

            const double arrowLen   = 32.0;
            const double headLen    = 10.0;
            const double shaftAngle = 70.0 * M_PI / 180.0;     // from horizontal (larger = more vertical)
            const double headAngle  = 28.0 * M_PI / 180.0;     // rozwarcie grotu
            const double dxShaft = arrowLen * std::cos(shaftAngle);
            const double dyShaft = arrowLen * std::sin(shaftAngle);

            // Forward T do liczenia pozycji src (gdy showUncertainty_)
            double srcW = double(srcSize_.width());
            double srcH = double(srcSize_.height());
            double outlineW = double(outlineSize_.width());
            double outlineH = double(outlineSize_.height());
            double aX = ( srcW > 0) ? (scaleX_ / srcW) : 0.0;
            double aY = (srcH > 0) ? (scaleY_ / srcH) : 0.0;

            for (int i = 0; i < pinsToDraw_.size(); ++i) {
                const DrawPin& pin = pinsToDraw_[i];
                if (!pin.active)
                    continue;
                const QColor& color = pin.confirmed ? blue : green;
                QPen shaftPen(color, 2);

                double oxScreen = outRect.left() + pin.outlineX * sxFactor;
                double oyScreen = outRect.top()  + pin.outlineY * syFactor;
                QPointF outlineTarget(oxScreen, oyScreen);

                // tail upper-right of target (source – "number box")
                QPointF tail(oxScreen + dxShaft, oyScreen - dyShaft);

                // Pozycja src pod aktualnym forward T (do "show residuals")
                bool drawTwoSegments = false;
                QPointF srcTarget;
                if (showUncertainty_ && srcW > 0 && srcH > 0) {
                    double gxc = pin.srcX - srcW / 2.0;
                    double gyc = pin.srcY - srcH / 2.0;
                    double oxc = deltaX_ + aX*gxc + rotXY_*gyc
                               + quadX_*gxc*gxc + crossXY_*gxc*gyc;
                    double oyc = deltaY_ + rotYX_*gxc + aY*gyc
                               + quadY_*gyc*gyc + crossYX_*gxc*gyc;
                    double predOX = oxc + outlineW / 2.0;
                    double predOY = oyc + outlineH / 2.0;
                    srcTarget = QPointF(outRect.left() + predOX * sxFactor,
                                         outRect.top()  + predOY * syFactor);
                    double ddx = srcTarget.x() - outlineTarget.x();
                    double ddy = srcTarget.y() - outlineTarget.y();
                    drawTwoSegments = (ddx*ddx + ddy*ddy > 0.5);   // > ~0.7 px
                }

                p.setPen(shaftPen);
                p.setBrush(Qt::NoBrush);

                if (drawTwoSegments) {
                    // two segments from same source – no arrowhead
                    p.drawLine(tail, outlineTarget);
                    p.drawLine(tail, srcTarget);
                } else {
                    p.drawLine(tail, outlineTarget);
                    // arrowhead (only when not showing residuals)
                    double bx = dxShaft / arrowLen;
                    double by = -dyShaft / arrowLen;
                    double cs = std::cos(headAngle);
                    double sn = std::sin(headAngle);
                    QPointF w1(bx * cs - by * sn, bx * sn + by * cs);
                    QPointF w2(bx * cs + by * sn, -bx * sn + by * cs);
                    QPolygonF head;
                    head << outlineTarget
                         << QPointF(outlineTarget.x() + w1.x() * headLen,
                                    outlineTarget.y() + w1.y() * headLen)
                         << QPointF(outlineTarget.x() + w2.x() * headLen,
                                    outlineTarget.y() + w2.y() * headLen);
                    p.setBrush(color);
                    p.drawPolygon(head);
                }

                // number above tail end – black text in white box with colored border
                QRectF textRect(tail.x() - 14, tail.y() - 18, 28, 16);
                p.setBrush(Qt::white);
                p.setPen(QPen(color, 1));
                p.drawRect(textRect);
                p.setPen(Qt::black);
                p.drawText(textRect, Qt::AlignCenter, QString::number(i + 1));
                pinLabelRects_.append(qMakePair(textRect, i));
            }
        }
    }
}

void AlignViewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // in autoFit mode a repaint is sufficient
    if (autoFit_) {
        update();
    }
}

void AlignViewWidget::applyZoomAt(const QPoint& anchor, double factor)
{
    // Screen zoom around anchor point
    if (outlineOverlay_.isNull())
        return;

    // Switch from autoFit to fixed on first zoom
    if (autoFit_) {
        viewScale_ = computeFitScale();
        autoFit_ = false;
        panOffset_ = QPointF(0, 0);
    }

    double oldScale = viewScale_;
    double newScale = viewScale_ * factor;

    if (newScale < minScale) newScale = minScale;
    if (newScale > maxScale) newScale = maxScale;

    if (std::abs(newScale - oldScale) < 1e-9)
        return;

    // Point under cursor in "physical space" (relative to widget center)
    QPointF widgetCenter(width() / 2.0, height() / 2.0);
    QPointF anchorOffset = QPointF(anchor) - widgetCenter - panOffset_;

    // Physical position under cursor
    QPointF physPos = anchorOffset / oldScale;

    // New scale
    viewScale_ = newScale;

    // New panOffset so physPos stays under cursor
    QPointF newAnchorOffset = physPos * newScale;
    panOffset_ = QPointF(anchor) - widgetCenter - newAnchorOffset;

    update();
}

void AlignViewWidget::wheelEvent(QWheelEvent* event)
{
    if (outlineOverlay_.isNull()) {
        QWidget::wheelEvent(event);
        return;
    }

    int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        event->ignore();
        return;
    }

    // Wheel (with or without Ctrl) → SCREEN zoom of both images
    QPoint anchor = event->position().toPoint();
    double factor = (steps > 0) ? 1.15 : (1.0 / 1.15);
    applyZoomAt(anchor, factor);

    event->accept();
}

void AlignViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Hit-test of pin number label boxes
        QPointF pos(event->pos());
        for (const auto& pr : pinLabelRects_) {
            if (pr.first.contains(pos)) {
                emit pinLabelClicked(pr.second);
                event->accept();
                return;
            }
        }

        dragging_ = true;
        // Shift+drag only works when outline is visible
        outlineDragging_ = showOutline_ && (event->modifiers() & Qt::ShiftModifier);
        lastMousePos_ = event->pos();

        if (outlineDragging_) {
            setCursor(Qt::CrossCursor);      // outline drag
            emit beforePhysicalChange();     // signal for undo BEFORE drag starts
        } else {
            setCursor(Qt::ClosedHandCursor); // view panning
        }

        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void AlignViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QPoint pos = event->pos();
    QPoint delta = pos - lastMousePos_;
    lastMousePos_ = pos;

    if (outlineDragging_) {
        // Ctrl+drag → moves OUTLINE visually with cursor (anchor swap).
        // Src is anchored to widget; outline.center = src.center - delta*vs,
        // so to move outline right by Δscreen, dX must decrease by Δ/vs.
        double viewScale = autoFit_ ? computeFitScale() : viewScale_;
        if (viewScale <= 0) viewScale = 1.0;
        deltaX_ -= double(delta.x()) / viewScale;
        deltaY_ -= double(delta.y()) / viewScale;
        update();
        emit parametersChanged();
    } else {
        // Drag without Ctrl → SCREEN panning (both images)
        if (autoFit_) {
            // Switch from autoFit to fixed
            viewScale_ = computeFitScale();
            autoFit_ = false;
            panOffset_ = QPointF(0, 0);
        }
        panOffset_ += QPointF(delta);
        update();
    }

    event->accept();
}

void AlignViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        bool wasOutlineDrag = outlineDragging_;
        dragging_ = false;
        outlineDragging_ = false;
        unsetCursor();

        if (wasOutlineDrag) {
            emit physicalDragFinished();  // signal for undo after Ctrl+drag ends
        }

        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

QPointF AlignViewWidget::screenToOutlineImageCoords(const QPoint& screenPos) const
{
    // Convert screen position to outline image coordinates (0..outlineW, 0..outlineH)
    QRectF outRect = outlineRectOnWidget();
    if (outRect.isNull() || outlineSize_.isEmpty())
        return QPointF(-1, -1);

    QPointF pos(screenPos);

    // Relative position in screen rectangle (0..1)
    double relX = (pos.x() - outRect.left()) / outRect.width();
    double relY = (pos.y() - outRect.top()) / outRect.height();

    // Image pixel coordinates
    double imgX = relX * outlineSize_.width();
    double imgY = relY * outlineSize_.height();

    return QPointF(imgX, imgY);
}

QPointF AlignViewWidget::screenToSrcImageCoords(const QPoint& screenPos) const
{
    if (srcSize_.isEmpty())
        return QPointF(-1, -1);

    bool nonLinear =
        std::abs(quadX_)   > 1e-15 || std::abs(quadY_)   > 1e-15 ||
        std::abs(rotXY_)   > 1e-15 || std::abs(rotYX_)   > 1e-15 ||
        std::abs(crossXY_) > 1e-15 || std::abs(crossYX_) > 1e-15;
    if (nonLinear) {
        double viewScale = autoFit_ ? computeFitScale() : viewScale_;
        if (viewScale <= 0)
            return QPointF(-1, -1);

        QRectF outRect = outlineRectOnWidget();
        QPointF outCenter = outRect.isNull() ?
            QPointF(width() / 2.0, height() / 2.0) :
            outRect.center();

        double oxc = (screenPos.x() - outCenter.x()) / viewScale - deltaX_;
        double oyc = (screenPos.y() - outCenter.y()) / viewScale - deltaY_;

        double srcW = srcSize_.width();
        double srcH = srcSize_.height();
        double ax = scaleX_ / srcW;
        double ay = scaleY_ / srcH;

        bool hasCross = std::abs(rotXY_) > 1e-15 || std::abs(rotYX_) > 1e-15
                     || std::abs(crossXY_) > 1e-15 || std::abs(crossYX_) > 1e-15;

        double u, v;
        if (!hasCross) {
            u = invQuad(oxc, ax, quadX_);
            v = invQuad(oyc, ay, quadY_);
            if (std::isnan(u) || std::isnan(v))
                return QPointF(-1, -1);
        } else {
            // Newton z liniowym startem
            double detLin = ax*ay - rotXY_*rotYX_;
            if (std::abs(detLin) > 1e-12) {
                u = ( ay      * oxc - rotXY_ * oyc) / detLin;
                v = (-rotYX_  * oxc + ax     * oyc) / detLin;
            } else {
                u = oxc / (std::abs(ax) > 1e-12 ? ax : 1.0);
                v = oyc / (std::abs(ay) > 1e-12 ? ay : 1.0);
            }
            for (int it = 0; it < 8; ++it) {
                double Fx = ax*u + rotXY_*v + quadX_*u*u + crossXY_*u*v - oxc;
                double Fy = rotYX_*u + ay*v + quadY_*v*v + crossYX_*u*v - oyc;
                if (Fx*Fx + Fy*Fy < 1e-10) break;
                double Jxx = ax + 2.0*quadX_*u + crossXY_*v;
                double Jxy = rotXY_ + crossXY_*u;
                double Jyx = rotYX_ + crossYX_*v;
                double Jyy = ay + 2.0*quadY_*v + crossYX_*u;
                double det = Jxx*Jyy - Jxy*Jyx;
                if (std::abs(det) < 1e-14) break;
                u -= ( Jyy*Fx - Jxy*Fy) / det;
                v -= (-Jyx*Fx + Jxx*Fy) / det;
            }
        }
        return QPointF(u + srcW / 2.0, v + srcH / 2.0);
    }

    // Linear mode: relative position in srcRect
    QRectF srcRect = srcRectOnWidget();
    if (srcRect.isNull())
        return QPointF(-1, -1);

    QPointF pos(screenPos);
    double relX = (pos.x() - srcRect.left()) / srcRect.width();
    double relY = (pos.y() - srcRect.top()) / srcRect.height();

    double imgX = relX * srcSize_.width();
    double imgY = relY * srcSize_.height();

    return QPointF(imgX, imgY);
}