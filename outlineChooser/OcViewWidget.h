#ifndef OC_VIEW_WIDGET_H
#define OC_VIEW_WIDGET_H

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <opencv2/opencv.hpp>
#include <vector>

#include "ViewPreset.h"

class OcViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit OcViewWidget(QWidget* parent = nullptr);

    // setSource: gray (CV_8UC1)
    // setOutline1/2: file-format outline (0=line, 255=bg) → internally inverted
    // outFileFmt: optional existing result outline; if empty, output starts
    // from the intersection of o1 and o2.
    // original: optional BGR/BGRA/gray cv::Mat used by ORIGINAL background.
    void setData(const cv::Mat& srcGray,
                 const cv::Mat& o1FileFmt,
                 const cv::Mat& o2FileFmt,
                 const cv::Mat& outFileFmt = cv::Mat(),
                 const cv::Mat& original = cv::Mat());
    void setConn8(bool on);
    bool hasData() const { return !src_.empty(); }
    bool hasOriginal() const { return !originalRgba_.empty(); }
    bool dirty() const { return dirty_; }

    // View presets (background + per-cell color table).
    const std::vector<ViewPreset>& presets() const { return presets_; }
    int  presetIndex() const { return presetIndex_; }
    void setPresetIndex(int i);
    void swapWithPrevPreset();

    // returns output mask in file format (0=line, 255=bg)
    cv::Mat outputFileFmt() const;
    void    markSaved() { dirty_ = false; }

    void fitToWindow();
    void zoomOneToOne();

    // Signal-less apply for undo/redo: set every pt in out_ to (add ? 255 : 0),
    // patch the visualization, mark dirty.
    void applyOp(const std::vector<cv::Point>& pts, bool add);

    // Advanced editing: allow click on a gray edge pixel (cell 0 with
    // src < 255) to add that whole same-value segment to the result.
    // Only meaningful with a GraySource-background preset.
    void setAllowGrayEdit(bool on) { allowGrayEdit_ = on; }
    bool allowGrayEdit() const { return allowGrayEdit_; }
    // Performs the gray-edit at (x,y) unconditionally. Used by MainWindow
    // after the user confirms the enabling dialog.
    void performGrayEditAt(int x, int y);

    // Shift rect/strip selection ----------------------------------------
    enum class CandColor { Red, Green, Gray };
    enum class CandMode  { Touching, Inside };
    // Apply the captured polygon with chosen parameters. Threshold is the
    // max src gray value of components that are eligible (≤ threshold; use
    // 255 to disable). Emits editOp() for undo.
    void commitRectSelection(int threshold, CandMode mode, CandColor color);
    // Drops the in-progress / pending polygon and repaints.
    void cancelRectSelection();
    bool hasPendingRect() const { return !lastPolyMask_.empty(); }
    // True when a rect/strip Gray-color commit would be meaningful here
    // (allowGrayEdit_ is on and the active preset has a GraySource bg).
    bool grayCandidateAvailable() const;
    // Recomputes the live preview (orange = will be added, yellow =
    // eligible component but threshold rejects). Called by MainWindow
    // as the dialog values change.
    void setRectPreview(int threshold, CandMode mode, CandColor color);

signals:
    void hudUpdate(const QString& s);
    void dirtyChanged(bool dirty);
    void presetChanged(int index);
    // Emitted after a click edit. `pts` are the pixels that actually
    // changed; `add` is true for include-segment, false for remove-segment.
    void editOp(std::vector<cv::Point> pts, bool add);
    // Emitted when the user clicks a gray candidate but allowGrayEdit_
    // is false. MainWindow shows a confirmation dialog and on Yes calls
    // setAllowGrayEdit(true) + performGrayEditAt(x, y).
    void grayEditRequested(int x, int y);
    // Emitted after a Shift-drag rect or Shift-click strip has been
    // captured. MainWindow opens the candidate dialog; on accept it
    // calls commitRectSelection() with the chosen parameters, on cancel
    // cancelRectSelection().
    void rectSelectionFinished();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QPointF widgetToImage(const QPoint& p) const;
    QPointF imageToWidget(const QPointF& p) const;
    void applyZoomAt(const QPoint& anchor, double factor);
    void emitHud(const QPoint& widgetPos);
    void rebuildVisualization();        // rebuild the entire vis_
    void updateVisualizationAt(const std::vector<cv::Point>& pts);
    void updateCursorForMods(Qt::KeyboardModifiers m);
    // Nearest "seedable" pixel within radius (image pixels). A pixel counts
    // as seedable if it is in outline 1 or outline 2; if includeGray is
    // true, gray-edge pixels (src<255) also count. Returns (-1,-1) if none.
    cv::Point pickClosestNear(int cx, int cy, int radius,
                              bool includeGray = false) const;

    int  colorAt(int x, int y) const;   // 0=white,1=green,2=red,3=yellow,4=black
    int  cellAt(int x, int y) const;    // 0..7 = (in1<<2)|(in2<<1)|out
    QRgb composePixel(int x, int y) const;
    void buildDefaultPresets();

    // Same-value gray-component analysis (used by rect/strip commit).
    void analyzeComponents();
    // Strip geometry: rectangle defined by axis p1→p2 extended by the
    // perpendicular vector from p1 to cursor.
    std::vector<cv::Point> stripCorners(const QPoint& p1,
                                        const QPoint& p2,
                                        const QPoint& cursor) const;
    // Build lastPolyMask_ from current rect or strip and emit
    // rectSelectionFinished.
    void finishRectFromCurrent();
    void finishStrip(const QPoint& widthRef);
    void cancelStripInProgress();
    void floodSegment(int x0, int y0, int wantedColor,
                      std::vector<cv::Point>& out) const;
    void floodAnyColor(int x0, int y0, std::vector<cv::Point>& out) const;

    cv::Mat src_;          // CV_8UC1, gray
    cv::Mat o1_;           // CV_8UC1, 0/255 (255=line, internal)
    cv::Mat o2_;           // CV_8UC1, 0/255
    cv::Mat out_;          // CV_8UC1, 0/255 (255=in result)
    cv::Mat originalRgba_; // CV_8UC4 RGBA, resized to src_; empty = none
    QImage  vis_;          // RGBA visualization (composed)
    bool    conn8_ = true;
    bool    dirty_ = false;
    bool    allowGrayEdit_ = false;

    // Same-value components over src_; built by analyzeComponents() in
    // setData(). labels_[0] = background (src >= 255); other labels index
    // labelSize_ / labelValue_.
    cv::Mat labels_;                    // CV_32S
    std::vector<int>   labelSize_;
    std::vector<uchar> labelValue_;     // src gray value per label

    // Shift selection state.
    enum class StripPhase { None, Tentative, P1Set, P2Set };
    StripPhase stripPhase_ = StripPhase::None;
    QPoint     stripP1_, stripP2_, stripCursor_;
    QPoint     stripPressWidget_;
    bool       rectDragging_ = false;
    QPoint     rectStart_, rectEnd_;
    cv::Mat    lastPolyMask_;           // 0/255 mask of the last captured polygon
    cv::Mat    previewOrange_;          // 0/255 — pixels that WILL be added
    cv::Mat    previewYellow_;          // 0/255 — eligible but threshold rejects
    QImage     previewImage_;           // composed RGBA overlay
    bool       previewActive_ = false;

    std::vector<ViewPreset> presets_;
    int presetIndex_ = 0;
    int prevPresetIndex_ = 0;

    double  scale_ = 1.0;
    QPointF panOffset_{0, 0};
    bool    pendingFit_ = true;
    bool    panning_ = false;
    QPoint  lastMousePos_;
    QPointF panOffsetAtPress_{0, 0};
    QPoint  pressPos_;
};

#endif