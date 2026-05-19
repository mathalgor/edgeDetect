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

    enum class Bg { Plain, Original, Gray, GrayRed, Prob };
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
    void swapWithPrevPreset();

    // Dim the rendered image toward black (for non-Plain bg presets) or
    // toward white (for Plain bg), 0..100. 100 = no fade.
    void setFadePercent(int p);
    int  fadePercent() const { return fadePercent_; }

signals:
    void presetChanged(int index);

public:

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
                      bool useG, int gMax, bool useR, int rMax,
                      bool useNum, int numThr,
                      bool useExt, int extThr,
                      bool useResultBlobs);
    void cancelPolygon();
    // Right-click menu actions.
    void selectWhole();        // polyMask_ = entire image rect; opens filter
    void selectNone();         // alias for cancelPolygon()
    void restoreLastPolygon(); // re-rasterizes the last user-drawn polygon
    bool hasLastPolygon() const { return !lastPolyVerts_.empty(); }
    // Counts (for the filter dialog labels): how many pixels would change
    // if the filter were applied with these parameters.
    // useG/useR gate the per-component G / avg-R thresholds (Add uses ≤,
    // Remove uses ≥). useNum / useExt gate size-based filters: Add keeps
    // only components with size ≥ numThr and max bbox dim ≥ extThr; Remove
    // keeps only components with size ≤ numThr and max bbox dim ≤ extThr.
    // useResultBlobs (Remove only): when true, num/extent thresholds compare
    // against connected components of outResult_ (binary CC, same conn) so
    // small isolated clusters can be removed even when they span multiple
    // G segments. Ignored when action == Add.
    int  filterCountIf(FilterMode mode, FilterAction action,
                       bool useG, int gMax, bool useR, int rMax,
                       bool useNum, int numThr,
                       bool useExt, int extThr,
                       bool useResultBlobs) const;
    int  setFilterPreview(FilterMode mode, FilterAction action,
                          bool useG, int gMax, bool useR, int rMax,
                          bool useNum, int numThr,
                          bool useExt, int extThr,
                          bool useResultBlobs);
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
    void contextMenuRequested(QPoint globalPos);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void enterEvent(QEnterEvent* e) override;

private:
    QPointF widgetToImage(const QPoint& p) const;
    QPointF imageToWidget(const QPointF& p) const;
    void applyZoomAt(const QPoint& anchor, double factor);
    void rebuildVisualization();
    void analyzeComponents();
    // Computes binary CC of outResult_==255 with current connectivity,
    // filling blobLabels (CV_32S), blobSize and blobExtent.
    void computeResultBlobs(cv::Mat& blobLabels,
                            std::vector<int>& blobSize,
                            std::vector<int>& blobExtent) const;
    void buildDefaultPresets();
    void closePolygonAndEmit();
    bool isPolyComplete() const { return !polyMask_.empty(); }
    void emitHud(const QPoint& widgetPos);
    cv::Mat rasterizePolygon(const std::vector<cv::Point>& verts) const;
    void updateCursorForMods(Qt::KeyboardModifiers m);
    // Returns label > 0 nearest to (cx, cy) within radius (image pixels)
    // whose pixels are currently 255 in outResult_. Returns 0 if none.
    int pickEraseLabelNear(int cx, int cy, int radius) const;
    // Removes all out_ pixels belonging to label L, emits editOp, applies.
    void eraseLabel(int L);
    void penLabel(int L);
    // Pick the closest editable target: either a black-outline pixel
    // (outResult_=255) or a gray-segment pixel (labels_!=0, out=0).
    // Returns label > 0 and a flag indicating whether it's currently in
    // the result; (0, false) if nothing within radius.
    struct EditPick { int label = 0; bool inResult = false; };
    EditPick pickEditTargetNear(int cx, int cy, int radius,
                                bool allowPen) const;

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
    std::vector<int>   labelExtent_;       // max(bbox width, height) per label

    // Polygon-in-progress (vertex sequence) and rasterized mask (when closed).
    std::vector<cv::Point> polyVerts_;
    bool                   polyOpen_ = false;     // drawing in progress
    QPoint                 polyHover_;            // last mouse pos in image coords (for rubber line)
    bool                   polyHoverValid_ = false;
    cv::Mat                polyMask_;             // 0/255 mask of closed polygon (empty otherwise)
    std::vector<cv::Point> closedPolyVerts_;      // outline-display verts for the captured polygon
    std::vector<cv::Point> lastPolyVerts_;        // last user-drawn polygon (for Restore)

    bool dirty_ = false;
    bool editLocked_ = false;
    bool conn8_ = true;
    std::vector<Preset> presets_;
    int  presetIndex_ = 0;
    int  prevPresetIndex_ = 0;
    int  fadePercent_ = 100;

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