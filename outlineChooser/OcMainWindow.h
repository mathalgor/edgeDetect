#ifndef OC_MAIN_WINDOW_H
#define OC_MAIN_WINDOW_H

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "AppConfig.h"
#include "ProjectConfig.h"

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QSpinBox;
class OcViewWidget;

class OcMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit OcMainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSetProject();
    void onPrevFile();
    void onNextFile();
    void onFit();
    void onOneToOne();
    void onConn8Toggled(bool on);
    void onHud(const QString& s);
    void onDirtyChanged(bool d);

private:
    void createUi();
    void rebuildRecentMenu();
    bool loadProjectFromPath(const QString& path);
    bool createNewProjectAt(const QString& path);
    void scanProject();
    bool loadProjectIndex(int idx);
    bool doSave();
    bool maybeSave();
    void updateFileLabel();
    void updateTitle();

    OcViewWidget* view_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QLabel* hudLabel_  = nullptr;
    QAction* aPrev_ = nullptr;
    QAction* aNext_ = nullptr;
    QSpinBox* fileSpin_ = nullptr;
    QCheckBox* conn8Cb_ = nullptr;
    QComboBox* presetCb_ = nullptr;
    QMenu* recentMenu_ = nullptr;

    AppConfig     appConfig_;
    ProjectConfig project_;
    QString       projectPath_;
    QStringList   fileList_;
    int           fileIndex_ = -1;
    QString       currentPath_;
    bool          guard_ = false;
};

#endif