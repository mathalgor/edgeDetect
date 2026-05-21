#ifndef ALIGNMAINWINDOW_H
#define ALIGNMAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QStack>
#include <QMap>

#include <QElapsedTimer>
#include <QDateTime>

#include "AlignViewWidget.h"

class QSpinBox;
class QLabel;
class QCheckBox;
class QComboBox;
class QTimer;
class QAction;
class QCloseEvent;

// State for undo/redo
struct AlignState {
    double scaleX;
    double scaleY;
    double deltaX;
    double deltaY;
    double quadX   = 0.0;
    double quadY   = 0.0;
    double rotXY   = 0.0;   // coef gyc in X-eq (rotation/shear)
    double rotYX   = 0.0;   // coef gxc w Y-eq
    double crossXY = 0.0;   // coef gxc*gyc w X-eq
    double crossYX = 0.0;   // coef gxc*gyc w Y-eq

    bool operator==(const AlignState& o) const {
        return scaleX == o.scaleX && scaleY == o.scaleY &&
               deltaX == o.deltaX && deltaY == o.deltaY &&
               quadX == o.quadX && quadY == o.quadY &&
               rotXY == o.rotXY && rotYX == o.rotYX &&
               crossXY == o.crossXY && crossYX == o.crossYX;
    }
    bool operator!=(const AlignState& o) const { return !(*this == o); }
};

struct FilePair {
    QString grayPath;
    QString outlinePath;
};

// Accumulated activity time per image
struct ImageTiming {
    qint64 activeMs = 0;          // total activity time (ms)
    int events = 0;               // input event counter
    qint64 firstEventMs = 0;      // epoch ms (first event)
    qint64 lastEventMs = 0;       // epoch ms (last event)
};

// Pin – point linking gray to outline
struct PinPoint {
    bool active = false;        // whether the pin is set
    bool confirmed = false;     // true = participated in the last Apply (blue color)
                                // false = freshly placed, not yet confirmed (green)
    double grayX = 0.0;         // position on gray (image pixels, 0..grayW)
    double grayY = 0.0;
    double outlineX = 0.0;      // position on outline (image pixels, 0..outlineW)
    double outlineY = 0.0;
};

class AlignMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AlignMainWindow(QWidget* parent = nullptr);

public slots:
    void pushUndoState();       // save current state onto the undo stack (called by view)
    void onDragFinished();      // Ctrl+drag finished – save to undo

private slots:
    // directories
    void openGrayDir();
    void openOutlineDir();

    // save current pair to align.jsonl
    void saveJsonlForCurrent();

    void onDeltaXChanged(int value);
    void onDeltaYChanged(int value);
    void onScaleXChanged(int value);
    void onScaleYChanged(int value);

    void updateInfoLabel();

    // undo/redo
    void undo();
    void redo();
    void restore();    // revert to state saved in JSON
    void onSpinboxEditTimeout();   // spinbox timer – ends "transient" edit

    // pins
    void onPinContextMenu(const QPoint& screenPos);  // context menu
    void togglePin(int pinIndex);                     // toggle pin active state
    void applyPins();                                 // apply pins
    void onQuadXToggled(bool checked);                // toggle quadratic mode in X
    void onQuadYToggled(bool checked);                // toggle quadratic mode in Y
    void onRotToggled(bool checked);                  // rotation/shear (linear cross)
    void onXyToggled(bool checked);                   // quadratic XY cross
    void restoreLastPins();                           // restore last applied pins
    void hidePins();                                  // clear view and pin menu (lastAppliedPins_ is kept)
    void setPinCount();                               // dialog: change pin slot count
    void onPinLabelClicked(int slotIndex);            // snap delta so pin has 0 residual

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void createUi();

    bool loadImageFile(const QString& path, cv::Mat& outMat);
    void goToNext();
    void goToPrev();
    void goFirst();
    void goLast();
    void goFirstUnaligned();   // skok do pierwszej pary z delta != 0

    // nawigacja po parach
    void refreshPairsIfReady();
    void loadCurrentPair();

    // JSONL
    void computeMargins(
        double& grayLeft, double& grayRight,
        double& grayTop, double& grayBottom,
        double& outlineLeft, double& outlineRight,
        double& outlineTop, double& outlineBottom
    ) const;

    void markDirty(); // parameter changed → save required

    // JSONL - new logic
    void loadJsonlFile();              // load JSONL file into map
    void saveJsonlFile();              // save entire map to file
    void updateCurrentInMap();         // update current entry in map
    bool loadParamsFromMap();          // load parameters from map for current image

    void closeEvent(QCloseEvent* event) override;  // save on close

    // dane GUI
    AlignViewWidget* view_ = nullptr;

    QSpinBox*       scaleXSpin_   = nullptr;
    QSpinBox*       scaleYSpin_   = nullptr;
    QSpinBox*       deltaXSpin_   = nullptr;
    QSpinBox*       deltaYSpin_   = nullptr;

    QLabel* infoLabel_ = nullptr;
    QLabel* fitInfoLabel_ = nullptr;   // permanent label showing last fit result

    // directories and files
    QString grayDir_;
    QString outlineDir_;
    QVector<FilePair> pairs_;
    int currentIndex_ = -1;

    // JSONL - mapa: key=nazwa outline, value=linia JSON
    QString jsonlPath_;
    QMap<QString, QString> jsonlData_;
    bool dirty_ = false;

    QCheckBox*      showOutlineCheck_ = nullptr;
    QCheckBox*      quadXCheck_ = nullptr;
    QCheckBox*      quadYCheck_ = nullptr;
    QCheckBox*      rotCheck_   = nullptr;
    QCheckBox*      xyCheck_    = nullptr;

    // undo/redo
    QStack<AlignState> undoStack_;
    QStack<AlignState> redoStack_;
    AlignState currentState();                    // get current state from view
    void applyState(const AlignState& state);     // apply state to view and spinboxes
    void updateUndoRedoActions();                 // update enabled state of undo/redo actions

    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* restoreAction_ = nullptr;

    // restore – revert to state saved in JSON
    AlignState savedState_{};                     // state saved in JSON (or initial from fitGrayToOutline)
    bool savedStateValid_ = false;

    // spinbox edit tracking - wykrywanie tymczasowych edycji
    QTimer*     spinboxTimer_ = nullptr;
    AlignState  stateBeforeSpinboxEdit_;          // state before spinbox edit began
    bool        spinboxEditInProgress_ = false;   // whether a spinbox edit is in progress
    bool        suppressUndoPush_ = false;        // suppress push during undo/redo/load

    // pins – configurable count (default 14, min 5, soft max).
    // pinCount_ = preferred count (from menu); pins_.size() is current, may be
    // temporarily increased by Restore last when loaded > pinCount_.
    int pinCount_ = 14;
    QVector<PinPoint> pins_;
    QPoint lastContextMenuPos_;                   // position of the last right-click

    // last applied pins – for recalculation after checkbox toggle
    QVector<PinPoint> lastAppliedPins_;
    // shared implementation: computes transform from pins and applies it
    // Returns RMS residual; optionally stores max(|residual_per_pin|) in *maxAbsErr
    // and (1-based, compact) index of worst pin in *maxAbsPin.
    // applyResult=false -> compute only (no undo/view/JSONL/status).
    double computeAndApplyFromPins(const QVector<PinPoint>& activePins,
                                   bool useQuadX, bool useQuadY,
                                   bool useRot, bool useXY,
                                   bool applyResult = true,
                                   double* maxAbsErr = nullptr,
                                   int* maxAbsPin = nullptr,
                                   bool* rejected = nullptr);
    void runOptimalMode();
    static int requiredPins(bool qX, bool qY, bool R, bool XY);
    void updateCheckboxesEnabled();
    // refresh pin drawing in view from current pins_[]
    void refreshPinsInView();

    QAction* restoreLastPinsAction_ = nullptr;
    QAction* optimalModeAction_ = nullptr;

    // --- Activity tracking (work time per image) ---
    QMap<QString, ImageTiming> timings_;       // key = outline_name
    QString currentTimingKey_;                 // current outline_name
    QString timingPath_;                       // path to <jsonl>.timing.jsonl
    qint64 lastActivityMs_ = 0;                // monotonic ms (QElapsedTimer)
    QElapsedTimer monoTimer_;                  // monotonic clock
    bool appActive_ = true;                    // status fokusu aplikacji

    QLabel* timingLabel_ = nullptr;            // statusbar label (opcjonalny)
    QTimer* timingTickTimer_ = nullptr;        // refresh statusbar co 1s
    QAction* showTimingStatusAction_ = nullptr;
    QAction* timingStatsAction_ = nullptr;

    void setupActivityTracking();
    void bumpActivity();                       // called on every input event
    void switchTimingKey(const QString& newKey);  // przed loadCurrentPair
    void loadTimings();
    void saveTimings();
    void updateTimingLabel();
    void showTimingStats();
    void onShowTimingStatusToggled(bool on);
};

#endif // ALIGNMAINWINDOW_H
