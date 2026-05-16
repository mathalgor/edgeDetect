#ifndef CANNYVIEWWIDGET_H
#define CANNYVIEWWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <opencv2/opencv.hpp>

class CannyViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CannyViewWidget(QWidget* parent = nullptr);
    void setSource(const cv::Mat& gray);     // CV_8UC1
    void setOutlineMask(const cv::Mat& mask);// CV_8UC1, 0=line/255=background (or reverse)
    void setOriginal(const cv::Mat& img);    // BGR or gray; empty = none
    void setShowOriginal(bool on);
    bool hasOriginal() const { return !original_.empty(); }
    bool hasImage() const { return !src_.empty(); }

    void setRange(int lo, int hi);           // 1..255
    void setFilter(bool on);
    void setFilterOutline(bool on);
    void setShowSource(bool on);
    void setShowOutline(bool on);
    void setBlackMode(bool on);
    void setHideDone(bool on);
    void setMinSize(int n);                  // 0 = disabled
    void setMinExtent(double d);             // 0 = disabled
    void setJoinTol(int tol);                // widens flood-fill by ±tol
    void setAllDarker(bool on);              // includes all darker than seed
    void setConn8(bool on);                  // true=8-conn, false=4-conn
    bool conn8() const { return conn8_; }
    int  maxComponentSize() const   { return maxSize_; }
    int  maxComponentExtent() const { return int(std::ceil(maxExtent_)); }

    // apply outline op without emitting the signal (used by undo/redo)
    bool applyOp(QPoint seed, bool add);   // uses current joinTol_/allDarker_/conn8_
    bool applyOp(QPoint seed, bool add, int joinTol, bool allDarker, bool conn8);
    void applyBulkOp(const cv::Mat& mask, bool add);

    int  joinTol() const  { return joinTol_; }
    bool allDarker() const { return allDarker_; }

    // rect selection (shift+drag)
    enum class CandMode { Touching = 0, Inside = 1 };
    void setRectThreshold(int t);
    int  rectThreshold() const { return rectThreshold_; }
    void setCandidateMode(int m);   // CandMode enum value
    int  candidateMode() const { return int(candidateMode_); }
    void commitRectSelection();   // orange → outline, emits outlineBulkOp
    void cancelRectSelection();
    bool hasRectSelection() const { return rectShowing_; }
    QPoint rectAnchorGlobal() const;  // dialog anchor position (bottom-right corner of rect)
    QVector<int> uniqueCandidateValues() const;  // sorted unique v from candMask_

    bool hasOutline() const;
    bool saveOutline(const QString& path) const;  // PNG: 0=line, 255=background

    // Fit to others — compare src segments with current outline (snapshot)
    void enterFitMode(const cv::Mat& fitRef, bool conn8, int coveragePct, int tol, bool append);
    void setFitConn8(bool on);
    void setFitCoverage(int pct);
    void setFitTol(int tol);
    void setFitAppend(bool on);
    void setFitMinGreen(int n);
    void commitFitMode();
    void exitFitMode();
    bool fitMode() const { return fitMode_; }

    void fitToWindow();
    void zoomOneToOne();

    // threshold add/remove tool
    void enterThresholdMode(bool addOn, int addVal, bool rmOn, int rmVal, bool finalPv);
    void exitThresholdMode();
    void setThresholdAdd(bool on, int v);
    void setThresholdRemove(bool on, int v);
    void setThresholdFinal(bool on);
    void setThresholdLimitRegion(bool on);
    void setThresholdRegionMode(int m);     // 0=Touching, 1=Inside
    void clearThresholdRegion();
    bool thresholdLimitRegion() const { return thresholdLimitRegion_; }
    bool thresholdHasRegion() const { return !thresholdRegionMask_.empty(); }
    void commitThreshold();   // emits outlineBulkOp twice (add, remove), then exits
    bool thresholdMode() const { return thresholdMode_; }
    // how many pixels would be added/removed for a hypothetical threshold (snap)
    int  thresholdAddCountIf(int v) const;
    int  thresholdRemoveCountIf(int v) const;

signals:
    void hudUpdate(const QString& s);
    void outlineOp(QPoint seed, bool add, int joinTol, bool allDarker, bool conn8);
    void outlineBulkOp(cv::Mat mask, bool add);   // bulk add/remove
    void rectSelectionFinished();            // candidates ready for dialog
    void analysisReady();
    void thresholdCountsChanged(int addCount, int removeCount);

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
    void rebuildDisplay();
    QPointF widgetToImage(const QPoint& p) const;
    QPointF imageToWidget(const QPointF& p) const;
    void applyZoomAt(const QPoint& anchor, double factor);
    void emitHud(const QPoint& widgetPos);
    void updateCursorForMods(Qt::KeyboardModifiers m);
    QCursor buildPickCursor() const;
    cv::Point pickDarkestNear(const cv::Point& p, int radius) const;
    cv::Point pickClosestNear(const cv::Point& p, int radius) const;
    cv::Point pickSeedNear(const cv::Point& p, int radius) const;
    cv::Point pickOutlineNear(const cv::Point& p, int radius) const;
    bool passesFilter(int x, int y) const;
    void analyzeComponents();
    void floodSelectSameValue(const cv::Point& seed, int joinTol, bool allDarker, bool conn8);
    void clearSelection();
    void rebuildSelectionImage();
    void rebuildOutlineImage();

    cv::Mat src_;          // CV_8UC1 original
    QImage  display_;      // what we draw (after filter or original)
    cv::Mat selMask_;      // CV_8UC1 0/255 — yellow preview (Shift)
    cv::Mat outline_;      // CV_8UC1 0/255 — red persistent outline (Ctrl)
    QImage  selImage_;     // RGBA with yellow overlay
    QImage  outlineImage_; // RGBA with red overlay
    bool    selDirty_ = true;
    bool    outlineDirty_ = true;

    int  lo_ = 0;
    int  hi_ = 254;
    bool filter_ = false;
    bool filterOutline_ = false;
    bool showOriginalMode_ = false;
    cv::Mat original_;        // BGR or gray
    QImage  originalImage_;   // ready to draw
    bool showSource_  = true;
    bool showOutline_ = true;
    bool blackMode_   = false;
    bool hideDone_    = false;

    cv::Mat labels_;            // CV_32S, 0 = background, same-value 8-conn labels
    std::vector<int> labelSize_;// component size by label index
    cv::Mat sizeMap_;    // CV_32S
    cv::Mat extentMap_;  // CV_32F
    int     maxSize_   = 0;
    float   maxExtent_ = 0.0f;
    int     minSize_   = 0;
    float   minExtent_ = 0.0f;
    int     joinTol_   = 0;
    bool    allDarker_ = false;
    bool    conn8_     = true;

    // rect selection (shift+drag)
    bool   rectDragging_ = false;
    bool   rectShowing_  = false;
    QPoint rectStartImg_;
    QPoint rectEndImg_;
    cv::Mat candMask_;
    cv::Mat yellowMask_;
    cv::Mat orangeMask_;
    int     rectThreshold_ = 240;
    QImage  rectOverlayImage_;
    bool    rectOverlayDirty_ = true;
    cv::Mat lastPolyMask_;
    CandMode candidateMode_ = CandMode::Touching;

    // fit-to-others
    bool    fitMode_ = false;
    bool    fitConn8_ = true;
    int     fitCoveragePct_ = 75;
    int     fitTol_ = 2;
    int     fitMinGreen_ = 0;
    bool    fitAppend_ = true;
    cv::Mat fitOutlineSnap_;     // CV_8UC1 0/255 — our outline at the moment of entry
    cv::Mat fitRef_;             // CV_8UC1 0/255 — loaded fit file (255=line, internal)
    cv::Mat fitDist_;            // CV_32F, distance to nearest fitRef pixel
    cv::Mat fitLabels_;          // CV_32S
    std::vector<int> fitLabelSize_;
    std::vector<int> fitLabelCovered_;
    cv::Mat fitMask_;            // CV_8UC1 0/255 — M = matching segments
    cv::Mat fitGreen_;           // M \ outline
    cv::Mat fitPurple_;          // outline \ dilate(M, tol)  (when !append)
    QImage  fitGreenImage_;
    QImage  fitPurpleImage_;
    bool    fitGreenDirty_ = true;
    bool    fitPurpleDirty_ = true;
    // threshold tool state
    bool    thresholdMode_     = false;
    bool    thresholdAddOn_    = true;
    int     thresholdAddVal_   = 100;
    bool    thresholdRemoveOn_ = true;
    int     thresholdRemoveVal_= 230;
    bool    thresholdFinal_    = false;
    cv::Mat thresholdAdd_;     // 0/255 — pixels to add
    cv::Mat thresholdRemove_;  // 0/255 — pixels to remove
    QImage  thresholdOverlayImage_;  // orange/yellow (raw mode)
    QImage  thresholdDisplayImage_;  // synthetic display (final mode)
    QImage  thresholdOutlineFinalImage_; // synthetic outline (final mode)
    // histograms of eligible pixels by gray [0..255]
    int     thrAddHist_[256] = {0};   // ¬outline (+ optional final filters)
    int     thrRmHist_[256]  = {0};   // ∈ outline (+ optional final filters)
    // bounding region (axis rect or oriented strip)
    bool    thresholdLimitRegion_ = false;
    int     thresholdRegionMode_ = 0;   // 0=Touching, 1=Inside (CandMode)
    cv::Mat thresholdRegionMask_;       // 0/255 filled region mask
    cv::Mat thresholdEligibleMask_;     // 0/255 mask after Touching/Inside on components
    // drawing gestures (Shift+drag rect / Shift+click strip) — independent of rect-select
    bool    thrDragging_ = false;
    QPoint  thrRectStartImg_;
    QPoint  thrRectEndImg_;
    enum class ThrStrip { None, Tentative, P1Set, P2Set };
    ThrStrip thrStripPhase_ = ThrStrip::None;
    QPoint   thrStripP1_;
    QPoint   thrStripP2_;
    QPoint   thrStripCursor_;
    QPoint   thrStripPressWidget_;
    std::vector<cv::Point> thrLastPoly_;  // strip polygon (4 corners) if last region was a strip
    void    recomputeThresholdEligible();
    void    finishThrRectRegion();
    void    finishThrStripRegion(const QPoint& widthRef);
    void    recomputeThresholdMasks();
    void    rebuildThresholdOverlay();
    void    rebuildThresholdFinal();

    void    rebuildRectOverlay();
    void    rebuildFitGreenImage();
    void    rebuildFitPurpleImage();
    void    recomputeFitSegments();
    void    recomputeFitCoverage();
    void    rebuildFitMask();
    void    computeCandidates();
    void    splitByThreshold();
    void    computeCandidatesFromPoly(const std::vector<cv::Point>& poly);
    void    rebuildCandidatesFromPoly();   // uses lastPolyMask_ + candidateMode_

    // oriented strip selection state
    enum class StripPhase { None, Tentative, P1Set, P2Set };
    StripPhase stripPhase_ = StripPhase::None;
    QPoint stripP1_;          // image coords
    QPoint stripP2_;
    QPoint stripCursor_;      // current cursor position in phase 1/2 (image)
    QPoint stripPressWidget_; // where the press was (widget)
    void cancelStrip();
    void finishStrip(const QPoint& widthRef);
    std::vector<cv::Point> stripCorners(const QPoint& p1, const QPoint& p2,
                                        const QPoint& cursor) const;

    double  scale_ = 1.0;
    QPointF panOffset_{0, 0};
    bool    pendingFit_ = true;

    bool   panning_ = false;
    QPoint lastMousePos_;
    QPointF panOffsetAtPress_{0, 0};
    QPoint  pressPos_;
};

#endif