#ifndef ALIGNVIEWWIDGET_H
#define ALIGNVIEWWIDGET_H

#include <QWidget>
#include <QImage>
#include <QRectF>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPoint>
#include <opencv2/opencv.hpp>

class AlignViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AlignViewWidget(QWidget* parent = nullptr);

    void setSrcImage(const cv::Mat& src);
    void setOutlineImage(const cv::Mat& outline);

    // alignment parameters
    void setDelta(double dx, double dy);       // src offset relative to outline (in pixels)
    void setSrcScale(double sx, double sy);   // src size in pixels (as if src were 1x1)
    void setQuadX(double q);                   // quadratic coefficient in X (0 = none)
    void setQuadY(double q);                   // quadratic coefficient in Y (0 = none)
    void setRotXY(double v);                   // coef gyc w X-eq (rotacja/shear)
    void setRotYX(double v);                   // coef gxc w Y-eq
    void setCrossXY(double v);                 // coef gxc*gyc w X-eq (quadratic cross)
    void setCrossYX(double v);                 // coef gxc*gyc w Y-eq

    double deltaX()   const { return deltaX_; }
    double deltaY()   const { return deltaY_; }
    double scaleX()   const { return scaleX_; }
    double scaleY()   const { return scaleY_; }
    double quadX()    const { return quadX_; }
    double quadY()    const { return quadY_; }
    double rotXY()    const { return rotXY_; }
    double rotYX()    const { return rotYX_; }
    double crossXY()  const { return crossXY_; }
    double crossYX()  const { return crossYX_; }

    // auto-fit src to outline preserving aspect ratio
    void fitSrcToOutline();

    // rectangles (after scaling and offset) – for margin computation
    bool hasBothImages() const;

    QRectF srcRectOnWidget() const;
    QRectF outlineRectOnWidget() const;

    QSize originalsrcSize() const  { return srcSize_; }
    QSize originalOutlineSize() const { return outlineSize_; }

    void resetView();  // reset pan/zoom do fit

    void setShowOutline(bool show);
    bool showOutline() const { return showOutline_; }

    void setShowSrc(bool show);
    bool showSrc() const { return showSrc_; }

    // screen → image coordinate conversion (returns -1,-1 if outside image)
    QPointF screenToSrcImageCoords(const QPoint& screenPos) const;
    QPointF screenToOutlineImageCoords(const QPoint& screenPos) const;

    // pins to draw (vector index = pin number - 1)
    struct DrawPin {
        bool active;
        bool confirmed;
        double outlineX;       // marker position on outline (outline image px)
        double outlineY;
        double srcX;          // position on src (src image px) – for forward T in "show uncertainty" mode
        double srcY;
    };
    void setPinsToDraw(const QVector<DrawPin>& pins);

    void setShowUncertainty(bool show);
    bool showUncertainty() const { return showUncertainty_; }

signals:
    void parametersChanged();     // triggers margin recomputation outside
    void beforePhysicalChange();  // emitted before a physical change (Ctrl+drag) – for undo
    void physicalDragFinished();  // Ctrl+drag finished – for undo
    void pinLabelClicked(int slotIndex);  // click on pin label box

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateSrcQImage();
    void updateOutlineQImage();
    double computeFitScale() const;
    void applyZoomAt(const QPoint& anchor, double factor);

    void rebuildSrcWarpedIfNeeded();
    QRectF srcWarpedRectOnWidget() const;

    cv::Mat srcMat_;
    cv::Mat outlineMat_;

    QImage srcImage_;
    QImage outlineOverlay_;

    // src after quadratic warp (rendered when quadX_ != 0 or quadY_ != 0)
    QImage srcWarped_;
    double srcWarpedOMin_ = 0.0;   // left edge of canvas in outline-centered pixels (X)
    double srcWarpedOMinY_ = 0.0;  // top edge of canvas in outline-centered pixels (Y)
    bool  srcWarpedDirty_ = true;

    QSize srcSize_;
    QSize outlineSize_;

    // PHYSICAL parameters (saved to JSONL, shown in toolbox)
    double deltaX_ = 0.0;    // src offset relative to outline in physical pixels
    double deltaY_ = 0.0;
    double scaleX_ = 100.0;  // target src size in physical pixels (width)
    double scaleY_ = 100.0;  // target src size in physical pixels (height)
    double quadX_  = 0.0;    // quadratic coef: outline_x_centered += quadX_ * (src_x_centered)^2
    double quadY_  = 0.0;    // quadratic coef: outline_y_centered += quadY_ * (src_y_centered)^2
    double rotXY_  = 0.0;    // outline_x_centered += rotXY_ * src_y_centered  (rotation/shear)
    double rotYX_  = 0.0;    // outline_y_centered += rotYX_ * src_x_centered
    double crossXY_= 0.0;    // outline_x_centered += crossXY_ * gxc*gyc
    double crossYX_= 0.0;    // outline_y_centered += crossYX_ * gxc*gyc

    // SCREEN parameters (display only, NOT saved)
    double  viewScale_ = 1.0;   // screen scale (view zoom)
    QPointF panOffset_{0, 0};   // screen pan offset
    bool    autoFit_ = true;    // auto-fit vs fixed zoom mode

    bool   showOutline_   = true;
    bool   showSrc_      = true;
    bool   showUncertainty_ = false;
    bool   dragging_      = false;
    bool   outlineDragging_  = false;
    QPoint lastMousePos_;

    QVector<DrawPin> pinsToDraw_;
    QVector<QPair<QRectF, int>> pinLabelRects_;   // do hit-testu kliku w numerek
};

#endif // ALIGNVIEWWIDGET_H
