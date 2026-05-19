#ifndef MC_MAIN_WINDOW_H
#define MC_MAIN_WINDOW_H

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

#include <opencv2/core.hpp>
#include <vector>

#include "AppConfig.h"
#include "ProjectConfig.h"
#include "TimeTracker.h"

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class McViewWidget;

class McMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit McMainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSetProject();
    void onSave();
    void onPrevFile();
    void onNextFile();
    void onFirstNotDone();
    void onFit();
    void onOneToOne();
    void onConn8Toggled(bool on);
    void onPresetChanged(int i);
    void onHud(const QString& s);
    void onDirtyChanged(bool d);
    void onEditOp(std::vector<cv::Point> pts, bool add);
    void onUndo();
    void onRedo();
    void onPolygonFinished();

private:
    void createUi();
    void rebuildRecentMenu();
    bool loadProjectFromPath(const QString& path);
    bool createNewProjectAt(const QString& path);
    void scanProject();
    bool loadProjectIndex(int idx);
    bool doSave();
    void autoSaveIfPossible();
    void updateFileLabel();
    void updateTitle();
    void updateDoneButton(bool done);

    McViewWidget* view_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QLabel* hudLabel_  = nullptr;
    QLabel* timeLabel_ = nullptr;
    QAction* aPrev_ = nullptr;
    QAction* aNext_ = nullptr;
    QAction* aFirstNotDone_ = nullptr;
    QAction* aUndo_ = nullptr;
    QAction* aRedo_ = nullptr;
    QSpinBox* fileSpin_ = nullptr;
    QCheckBox* conn8Cb_ = nullptr;
    QComboBox* presetCb_ = nullptr;
    QPushButton* doneBtn_ = nullptr;
    QMenu* recentMenu_ = nullptr;

    // Persistent filter dialog (created lazily on first polygon).
    class QDialog*  filterDlg_     = nullptr;
    class QComboBox* fdModeCb_     = nullptr;
    class QComboBox* fdActCb_      = nullptr;
    class QCheckBox* fdGCb_        = nullptr;
    class QCheckBox* fdRCb_        = nullptr;
    class CountSnapSpinBox* fdGSb_ = nullptr;
    class CountSnapSpinBox* fdRSb_ = nullptr;
    class QLabel*    fdGLbl_       = nullptr;
    class QLabel*    fdRLbl_       = nullptr;
    class QLabel*    fdCountLbl_   = nullptr;
    void ensureFilterDialog();
    void filterDialogRefresh();

    TimeTracker tracker_;

    struct EditOp { std::vector<cv::Point> pts; bool add = true; };
    QVector<EditOp> undoStack_;
    QVector<EditOp> redoStack_;
    void clearUndoStacks();
    void updateUndoActions();

    AppConfig     appConfig_;
    ProjectConfig project_;
    QString       projectPath_;
    QStringList   fileList_;
    int           fileIndex_ = -1;
    QString       currentPath_;
    bool          guard_ = false;
};

#endif
