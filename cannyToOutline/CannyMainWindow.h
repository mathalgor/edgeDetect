#ifndef CANNYMAINWINDOW_H
#define CANNYMAINWINDOW_H

#include <QMainWindow>
#include <QPoint>
#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>
#include <opencv2/core.hpp>
#include "AppConfig.h"
#include "ProjectConfig.h"

class QCloseEvent;

class QAction;
class QLabel;
class QMenu;
class QSlider;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QDialog;
class CannyViewWidget;

class CannyMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit CannyMainWindow(QWidget* parent = nullptr);

    bool loadFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onOpen();
    void onSave();
    void onSaveAs();
    void onLoChanged(int v);
    void onHiChanged(int v);
    void onLockToggled(bool on);
    void onFilterToggled(bool on);
    void onMinSizeChanged(int v);
    void onMinExtentChanged(int v);
    void setViewMode(int idx);
    void onJoinTolChanged(int v);
    void onAllDarkerToggled(bool on);
    void onConn8Toggled(bool on);
    void onAnalysisReady();
    void onFit();
    void onOneToOne();
    void onHud(const QString& s);
    void onOutlineOp(QPoint seed, bool add, int joinTol, bool allDarker, bool conn8);
    void onOutlineBulkOp(cv::Mat mask, bool add);
    void onRectSelectionFinished();
    void onUndo();
    void onRedo();
    void onFitToOthers();
    void onSetProject();
    void onNewProject();
    void onOpenProject();
    void onFitBulk();
    void onThresholdBulk();
    void onPrevFile();
    void onNextFile();
    void onFirstEmpty();

private:
    void createUi();
    void syncRangeToView();
    void updateUndoActions();
    void updateTitle();
    bool maybeSave();                 // true = may continue, false = cancel
    bool doSave();                    // actually saves, true on OK

    CannyViewWidget* view_ = nullptr;

    QSlider*   loSlider_ = nullptr;
    QSlider*   hiSlider_ = nullptr;
    QSpinBox*  loSpin_   = nullptr;
    QSpinBox*  hiSpin_   = nullptr;
    QCheckBox* lockCb_   = nullptr;
    QCheckBox* filterCb_ = nullptr;
    QCheckBox* filterOutlineCb_ = nullptr;
    QComboBox* viewModeCb_ = nullptr;
    int        viewMode_ = 5;       // default = "Gray source + red outline"
    int        prevViewMode_ = 5;
    bool       hasOriginal_ = false;
    QSpinBox*  minSizeSpin_   = nullptr;
    QSpinBox*  minExtentSpin_ = nullptr;
    QSpinBox*  joinTolSpin_   = nullptr;
    QCheckBox* allDarkerCb_   = nullptr;
    QCheckBox* conn8Cb_       = nullptr;
    QLabel*    statsLabel_    = nullptr;

    QLabel*    hudLabel_ = nullptr;

    QAction*   aUndo_ = nullptr;
    QAction*   aRedo_ = nullptr;

    struct Op {
        bool isBulk = false;
        QPoint seed;
        bool add = true;
        int  joinTol = 0;
        bool allDarker = false;
        bool conn8 = true;
        cv::Mat mask;       // for bulk
    };
    QVector<Op> undoStack_;
    QVector<Op> redoStack_;
    int lastRectThreshold_ = 240;
    int lastCandidateMode_ = 0;     // 0=Touching, 1=Inside
    QDialog* rectDialog_ = nullptr;
    QWidget* fitPanel_ = nullptr;
    std::function<void()> fitShowFn_;
    QWidget* thrPanel_ = nullptr;
    class QSplitter* centralSplitter_ = nullptr;

    // last Threshold tool settings (runtime)
    bool thrAddOn_     = true;
    int  thrAddVal_    = 200;
    bool thrRemoveOn_  = true;
    int  thrRemoveVal_ = 255;
    bool thrFinalOn_   = false;
    bool thrLimitOn_   = false;
    int  thrRegionMode_= 0;     // 0=Touching, 1=Inside

    void onThresholdTool();

    // last Fit dialog settings (kept in runtime, not in config)
    int  fitLastCoverage_  = 75;
    int  fitLastTol_       = 2;
    int  fitLastMinGreen_  = 0;
    bool fitLastAppend_    = true;
    bool fitLastShowOrig_  = true;

    int  lockDelta_ = 0;
    bool guard_ = false;
    bool dirty_ = false;

    QString currentPath_;
    QString outlinePath_;
    QString defaultOutlinePath() const;

    AppConfig     appConfig_;
    ProjectConfig project_;
    QString       projectPath_;       // path to the .ctoprj file
    QMenu*        recentMenu_ = nullptr;
    void rebuildRecentMenu();
    bool loadProjectFromPath(const QString& path);
    bool createNewProjectAt(const QString& path);
    bool saveCurrentProject();        // to projectPath_
    QStringList   fileList_;     // base file names (e.g. "001.png")
    int           fileIndex_ = -1;
    QLabel*       fileLabel_ = nullptr;
    QAction*      aPrev_ = nullptr;
    QAction*      aNext_ = nullptr;
    QAction*      aFirstEmpty_ = nullptr;
    QSpinBox*     fileSpin_ = nullptr;

    void scanProject();
    bool loadProjectIndex(int idx);
    void updateFileLabel();
};

#endif