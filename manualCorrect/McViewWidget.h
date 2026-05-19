#ifndef MC_VIEW_WIDGET_H
#define MC_VIEW_WIDGET_H

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>
#include <opencv2/core.hpp>
#include <vector>

class McViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit McViewWidget(QWidget* parent = nullptr);

    enum class Bg { Plain, Original, Gray, Prob };
    enum class FilterMode { Inside, Touching };
    enum class FilterAction { Remove, Add };

    struct Preset {
        QString name;
        Bg      bg;
        bool    showResult;
        bool    showInputOutline;
    };

    // outFileFmt is optional — if empty, output starts as a copy of the input
    // outline. dbgrg is BGR: B=0, G=gray edge level, R=inverted prob.
    void setData(const cv::Mat& inOutlineFileFmt,
                 const cv::Mat& dbgrg,
                 const cv::Mat& originalBgr,
                 const cv::Mat& outFileFmt = cv::Mat());
    bool hasData() const { return !outResult_.empty(); }
    bool dirty() const { return dirty_; }
    void markSaved() { if (dirty_) { dirty_ = false; emit dirtyChanged(false); } }

    cv::Mat outputFileFmt() const;  // 0 = line, 255 = bg

    void setConn8(bool on);
    bool conn8() const { return conn8_; }

    const std::vector<Preset>& presets() const { return presets_; }
    int  presetIndex() const { return presetIndex_; }
    void setPresetIndex(int i);

    // Edit lock (Done state)
    void setEditLocked(bool on);
    bool editLocked() const { return editLocked_; }

    void fitToWindow();
    void zoomOneToOne();

    // Apply a captured edit (pts go to 255 for add=true, else 0). For undo/redo.
    void applyOp(const std::vector<cv::Point>& pts, bool add);

    // Filter API ----------------------------------------------------------
    // Returns true when a polygon has been closed and is awaiting a filter
    // commit. The MainWindow opens its filter dialog in response to
    // polygonFinished() and either calls commitFilter() or cancelPolygon().
    bool hasPendingPolygon() const { return !polyMask_.empty(); }
    // Apply filter parameters: emits editOp(pts, action==Add) and updates
    // out_/vis_. Drops the polygon afterwards.
    void commitFilter(FilterMode mode, FilterAction action,
                      int gMax, int rMax);
    void cancelPolygon();
    // Counts (for the filter dialog labels): how many pixels would change
    // if the filter were applied with these parameters.
    int  filterCountIf(FilterMode mode, FilterAction action,
                       int gMax, int rMax) const;
    // Live preview: compute the affected-pixel mask for these parameters,
    // store it, repaint, and return the pixel count. The mask is shown as
    // cyan (Add) or magenta (Remove) on top of the polygon overlay.
    int  setFilterPreview(FilterMode mode, FilterAction action,
                          int gMax, int rMax);
    void clearFilterPreview();
    // True when the captured polygon has at least one segment touching it
    // — needed for sanity in the dialog.
    bool polygonHasComponents() const { return !polyMask_.empty(); }

signals:
    void hudUpdate(const QString& s);
    void dirtyChanged(bool dirty);
    void editOp(std::vector<cv::Point> pts, bool add);
    void editBlocked();
    void polygonFinished();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QPointF widgetToImage(const QPoint& p) const;
    QPointF imageToWidget(const QPointF& p) const;
    void applyZoomAt(const QPoint& anchor, double factor);
    void rebuildVisualization();
    void analyzeComponents();
    void buildDefaultPresets();
    void closePolygonAndEmit();
    bool isPolyComplete() const { return !polyMask_.empty(); }
    void emitHud(const QPoint& widgetPos);
    cv::Mat rasterizePolygon(const std::vector<cv::Point>& verts) const;

    cv::Mat outIn_;        // CV_8UC1 (internal 255=line) — original input outline (read-only)
    cv::Mat outResult_;    // CV_8UC1 (internal 255=line) — the edited result
    cv::Mat srcGray_;      // CV_8UC1 — G channel of .dbgrg (0..254 edge, 255 bg)
    cv::Mat probR_;        // CV_8UC1 — R channel of .dbgrg
    cv::Mat originalRgba_; // CV_8UC4 RGBA (resized to data size)
    QImage  vis_;          // RGBA composed view

    // 8/4-conn components over srcGray_ pixels where srcGray_ < 255.
    cv::Mat labels_;                       // CV_32S
    std::vector<int>   labelSize_;
    std::vector<uchar> labelGray_;         // src gray value per label
    std::vector<uchar> labelAvgR_;         // average R per label

    // Polygon-in-progress (vertex sequence) and rasterized mask (when closed).
    std::vector<cv::Point> polyVerts_;
    bool                   polyOpen_ = false;     // drawing in progress
    QPoint                 polyHover_;            // last mouse pos in image coords (for rubber line)
    bool                   polyHoverValid_ = false;
    cv::Mat                polyMask_;             // 0/255 mask of closed polygon (empty otherwise)

    bool dirty_ = false;
    bool editLocked_ = false;
    bool conn8_ = true;
    std::vector<Preset> presets_;
    int  presetIndex_ = 0;

    // Live filter preview: 0/255 mask of affected pixels + action color hint.
    cv::Mat previewMask_;
    bool    previewIsAdd_ = false;

    double  scale_ = 1.0;
    QPointF panOffset_{0, 0};
    bool    pendingFit_ = true;
    bool    panning_ = false;
    QPoint  lastMousePos_;
    QPointF panOffsetAtPress_{0, 0};
};

#endif