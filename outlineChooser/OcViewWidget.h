#ifndef OC_VIEW_WIDGET_H
#define OC_VIEW_WIDGET_H

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <opencv2/opencv.hpp>

class OcViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit OcViewWidget(QWidget* parent = nullptr);

    // setSource: gray (CV_8UC1)
    // setOutline1/2: file-format outline (0=line, 255=bg) → internally inverted
    // outFileFmt: optional existing result outline; if empty, output starts
    // from the intersection of o1 and o2.
    void setData(const cv::Mat& srcGray,
                 const cv::Mat& o1FileFmt,
                 const cv::Mat& o2FileFmt,
                 const cv::Mat& outFileFmt = cv::Mat());
    void setConn8(bool on);
    bool hasData() const { return !src_.empty(); }
    bool dirty() const { return dirty_; }

    // returns output mask in file format (0=line, 255=bg)
    cv::Mat outputFileFmt() const;
    void    markSaved() { dirty_ = false; }

    void fitToWindow();
    void zoomOneToOne();

signals:
    void hudUpdate(const QString& s);
    void dirtyChanged(bool dirty);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QPointF widgetToImage(const QPoint& p) const;
    QPointF imageToWidget(const QPointF& p) const;
    void applyZoomAt(const QPoint& anchor, double factor);
    void emitHud(const QPoint& widgetPos);
    void rebuildVisualization();        // rebuild the entire vis_
    void updateVisualizationAt(const std::vector<cv::Point>& pts);

    int  colorAt(int x, int y) const;   // 0=white,1=green,2=red,3=yellow,4=black
    void floodSegment(int x0, int y0, int wantedColor,
                      std::vector<cv::Point>& out) const;
    void floodAnyColor(int x0, int y0, std::vector<cv::Point>& out) const;

    cv::Mat src_;        // CV_8UC1, gray
    cv::Mat o1_;         // CV_8UC1, 0/255 (255=line, internal)
    cv::Mat o2_;         // CV_8UC1, 0/255
    cv::Mat out_;        // CV_8UC1, 0/255 (255=in result)
    QImage  vis_;        // RGBA visualization
    bool    conn8_ = true;
    bool    dirty_ = false;

    double  scale_ = 1.0;
    QPointF panOffset_{0, 0};
    bool    pendingFit_ = true;
    bool    panning_ = false;
    QPoint  lastMousePos_;
    QPointF panOffsetAtPress_{0, 0};
    QPoint  pressPos_;
};

#endif