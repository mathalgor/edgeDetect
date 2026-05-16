#ifndef OC_MAIN_WINDOW_H
#define OC_MAIN_WINDOW_H

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
class OcViewWidget;

class OcMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit OcMainWindow(QWidget* parent = nullptr);

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
    void onFit();
    void onOneToOne();
    void onConn8Toggled(bool on);
    void onHud(const QString& s);
    void onDirtyChanged(bool d);
    void onEditOp(std::vector<cv::Point> pts, bool add);
    void onUndo();
    void onRedo();

private:
    void createUi();
    void rebuildRecentMenu();
    bool loadProjectFromPath(const QString& path);
    bool createNewProjectAt(const QString& path);
    void scanProject();
    bool loadProjectIndex(int idx);
    bool doSave();
    // Save without prompting when the user just navigates or closes the
    // window. If the project has no outputDir yet, this is a no-op (we
    // can't write anywhere). The dirty flag is cleared on success.
    void autoSaveIfPossible();
    void updateFileLabel();
    void updateTitle();

    OcViewWidget* view_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QLabel* hudLabel_  = nullptr;
    QLabel* timeLabel_ = nullptr;
    QAction* aPrev_ = nullptr;
    QAction* aNext_ = nullptr;
    QAction* aUndo_ = nullptr;
    QAction* aRedo_ = nullptr;
    QSpinBox* fileSpin_ = nullptr;
    QCheckBox* conn8Cb_ = nullptr;
    QComboBox* presetCb_ = nullptr;
    QPushButton* doneBtn_ = nullptr;
    QMenu* recentMenu_ = nullptr;

    TimeTracker tracker_;
    void updateDoneButton(bool done);

    struct EditOp {
        std::vector<cv::Point> pts;
        bool add = true;
    };
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