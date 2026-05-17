#include "CannyMainWindow.h"
#include "CannyViewWidget.h"
#include "CountSnapSpinBox.h"
#include "ProjectDialog.h"
#include "ProjectTimeDialog.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QShortcut>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMetaType>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <opencv2/imgcodecs.hpp>

CannyMainWindow::CannyMainWindow(QWidget* parent) : QMainWindow(parent)
{
    qRegisterMetaType<cv::Mat>("cv::Mat");
    createUi();
    setWindowTitle("cannyToOutline");
    resize(1200, 800);

    appConfig_.load();
    rebuildRecentMenu();
    if (!appConfig_.currentProjectPath.isEmpty()
        && QFileInfo::exists(appConfig_.currentProjectPath)) {
        loadProjectFromPath(appConfig_.currentProjectPath);
    } else {
        fileLabel_->setText("(no project — File → New/Open project)");
    }
}

void CannyMainWindow::rebuildRecentMenu()
{
    if (!recentMenu_) return;
    recentMenu_->clear();
    if (appConfig_.recentProjects.isEmpty()) {
        auto* a = recentMenu_->addAction("(empty)");
        a->setEnabled(false);
        return;
    }
    for (const QString& p : appConfig_.recentProjects) {
        auto* a = recentMenu_->addAction(p);
        connect(a, &QAction::triggered, this, [this, p]{ loadProjectFromPath(p); });
    }
    recentMenu_->addSeparator();
    auto* clr = recentMenu_->addAction("Clear list");
    connect(clr, &QAction::triggered, this, [this]{
        appConfig_.recentProjects.clear();
        appConfig_.save();
        rebuildRecentMenu();
    });
}

bool CannyMainWindow::loadProjectFromPath(const QString& path)
{
    ProjectConfig nc;
    if (!nc.load(path) || !nc.isValid()) {
        QMessageBox::warning(this, "Open project",
            QString("Failed to load project:\n%1").arg(path));
        return false;
    }
    project_ = nc;
    projectPath_ = path;
    appConfig_.currentProjectPath = path;
    appConfig_.addRecent(path);
    appConfig_.save();
    rebuildRecentMenu();
    updateTitle();
    tracker_.bindToProject(QFileInfo(path).absoluteFilePath());
    scanProject();
    return true;
}

bool CannyMainWindow::createNewProjectAt(const QString& path)
{
    ProjectDialog dlg(ProjectConfig(), this);
    if (dlg.exec() != QDialog::Accepted) return false;
    const ProjectConfig nc = dlg.config();
    if (!nc.isValid()) {
        QMessageBox::warning(this, "New project",
            "Source and Output must point to existing directories.");
        return false;
    }
    if (!nc.save(path)) {
        QMessageBox::warning(this, "New project",
            QString("Failed to save project:\n%1").arg(path));
        return false;
    }
    project_ = nc;
    projectPath_ = path;
    appConfig_.currentProjectPath = path;
    appConfig_.addRecent(path);
    appConfig_.save();
    rebuildRecentMenu();
    updateTitle();
    scanProject();
    return true;
}

bool CannyMainWindow::saveCurrentProject()
{
    if (projectPath_.isEmpty()) return false;
    return project_.save(projectPath_);
}

void CannyMainWindow::createUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // --- slider bar on top ---
    auto* top = new QHBoxLayout();
    top->setSpacing(8);

    auto* loTag = new QLabel("lo");
    loSpin_ = new QSpinBox;
    loSpin_->setRange(0, 254);
    loSpin_->setValue(0);
    loSpin_->setAccelerated(true);
    loSlider_ = new QSlider(Qt::Horizontal);
    loSlider_->setRange(0, 254);
    loSlider_->setValue(0);

    auto* hiTag = new QLabel("hi");
    hiSpin_ = new QSpinBox;
    hiSpin_->setRange(0, 254);
    hiSpin_->setValue(254);
    hiSpin_->setAccelerated(true);
    hiSlider_ = new QSlider(Qt::Horizontal);
    hiSlider_->setRange(0, 254);
    hiSlider_->setValue(254);

    lockCb_    = new QCheckBox("lock distance");
    filterCb_  = new QCheckBox("filter");
    filterOutlineCb_ = new QCheckBox("filter outline");
    filterOutlineCb_->setToolTip("Apply lo..hi range to outline display");

    viewModeCb_ = new QComboBox();
    viewModeCb_->setToolTip("View mode — press 1..7 or Tab to swap with previous");
    // Order must match presets table in setViewMode().
    viewModeCb_->addItem("Original");
    viewModeCb_->addItem("Original + outline red");
    viewModeCb_->addItem("Outline black");
    viewModeCb_->addItem("Gray source");
    viewModeCb_->addItem("Source (all black)");
    viewModeCb_->addItem("Gray source + red outline");
    viewModeCb_->addItem("Black source + red outline");
    viewModeCb_->setCurrentIndex(viewMode_);

    top->addWidget(loTag);
    top->addWidget(loSpin_);
    top->addWidget(loSlider_, 1);
    top->addSpacing(12);
    top->addWidget(hiTag);
    top->addWidget(hiSpin_);
    top->addWidget(hiSlider_, 1);
    top->addSpacing(12);
    top->addWidget(lockCb_);
    top->addWidget(filterCb_);
    top->addWidget(filterOutlineCb_);
    top->addSpacing(12);
    top->addWidget(new QLabel("View:"));
    top->addWidget(viewModeCb_);

    root->addLayout(top);

    // --- second row: component filters ---
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(8);
    row2->addWidget(new QLabel("min size (px):"));
    minSizeSpin_ = new QSpinBox;
    minSizeSpin_->setRange(0, 10'000'000);
    minSizeSpin_->setAccelerated(true);
    minSizeSpin_->setValue(0);
    row2->addWidget(minSizeSpin_);
    row2->addSpacing(12);
    row2->addWidget(new QLabel("min extent (px):"));
    minExtentSpin_ = new QSpinBox;
    minExtentSpin_->setRange(0, 100'000);
    minExtentSpin_->setAccelerated(true);
    minExtentSpin_->setValue(0);
    row2->addWidget(minExtentSpin_);
    row2->addSpacing(16);
    row2->addWidget(new QLabel("join tol:"));
    joinTolSpin_ = new QSpinBox;
    joinTolSpin_->setRange(0, 100);
    joinTolSpin_->setAccelerated(true);
    joinTolSpin_->setValue(0);
    row2->addWidget(joinTolSpin_);
    allDarkerCb_ = new QCheckBox("all darker");
    row2->addWidget(allDarkerCb_);
    conn8Cb_ = new QCheckBox("8-conn");
    conn8Cb_->setChecked(true);
    row2->addWidget(conn8Cb_);
    row2->addSpacing(16);
    statsLabel_ = new QLabel("(no analysis)");
    row2->addWidget(statsLabel_);
    row2->addStretch(1);
    root->addLayout(row2);

    // --- view ---
    view_ = new CannyViewWidget(central);
    centralSplitter_ = new QSplitter(Qt::Horizontal, central);
    centralSplitter_->addWidget(view_);
    centralSplitter_->setStretchFactor(0, 1);
    centralSplitter_->setChildrenCollapsible(false);
    root->addWidget(centralSplitter_, 1);

    setCentralWidget(central);

    // --- menu / toolbar ---
    auto* tb = addToolBar("main");
    aUndo_ = tb->addAction("Undo");
    aRedo_ = tb->addAction("Redo");
    aUndo_->setShortcut(QKeySequence::Undo);
    aRedo_->setShortcut(QKeySequence::Redo);
    aUndo_->setEnabled(false);
    aRedo_->setEnabled(false);
    tb->addSeparator();
    aPrev_ = tb->addAction("◀");
    aNext_ = tb->addAction("▶");
    aFirstEmpty_ = tb->addAction("▶?");
    aFirstEmpty_->setToolTip("Go to first file without outline");
    aPrev_->setShortcut(QKeySequence(Qt::Key_PageUp));
    aNext_->setShortcut(QKeySequence(Qt::Key_PageDown));
    aPrev_->setEnabled(false);
    aNext_->setEnabled(false);
    aFirstEmpty_->setEnabled(false);
    fileSpin_ = new QSpinBox();
    fileSpin_->setRange(0, 0);
    fileSpin_->setKeyboardTracking(false);
    fileSpin_->setEnabled(false);
    fileSpin_->setToolTip("Jump to image number");
    tb->addWidget(fileSpin_);
    tb->addSeparator();
    auto* aFit  = tb->addAction("Fit");
    auto* aOne  = tb->addAction("1:1");

    auto* tbSpacer = new QWidget();
    tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(tbSpacer);

    doneBtn_ = new QPushButton("Done");
    doneBtn_->setCheckable(true);
    doneBtn_->setToolTip("Mark this file as done (click to toggle)");
    tb->addWidget(doneBtn_);
    updateDoneButton(false);


    auto* mFile = menuBar()->addMenu("&File");
    auto* aNewProj  = mFile->addAction("&New project...");
    auto* aOpenProj = mFile->addAction("&Open project...");
    recentMenu_ = mFile->addMenu("&Recent projects");
    auto* aSetProj  = mFile->addAction("&Set project dirs...");
    mFile->addSeparator();
    auto* aSave = mFile->addAction("&Save");
    aSave->setShortcut(QKeySequence::Save);
    auto* aSaveAs = mFile->addAction("Save &as...");
    mFile->addSeparator();
    mFile->addAction(aPrev_);
    mFile->addAction(aNext_);
    mFile->addSeparator();
    auto* aQuit = mFile->addAction("&Quit");
    aQuit->setShortcut(QKeySequence::Quit);
    connect(aNewProj,  &QAction::triggered, this, &CannyMainWindow::onNewProject);
    connect(aOpenProj, &QAction::triggered, this, &CannyMainWindow::onOpenProject);
    connect(aSave,     &QAction::triggered, this, &CannyMainWindow::onSave);
    connect(aSaveAs,   &QAction::triggered, this, &CannyMainWindow::onSaveAs);

    auto* mEdit = menuBar()->addMenu("&Edit");
    mEdit->addAction(aUndo_);
    mEdit->addAction(aRedo_);

    auto* mView = menuBar()->addMenu("&View");
    mView->addAction(aFit);
    mView->addAction(aOne);

    auto* mTools = menuBar()->addMenu("&Tools");
    auto* aFitOthers = mTools->addAction("Fit to others...");
    aFitOthers->setShortcut(QKeySequence(Qt::Key_F4));
    connect(aFitOthers, &QAction::triggered, this, &CannyMainWindow::onFitToOthers);
    auto* aThrTool = mTools->addAction("Threshold add/remove...");
    aThrTool->setShortcut(QKeySequence(Qt::Key_F5));
    connect(aThrTool, &QAction::triggered, this, &CannyMainWindow::onThresholdTool);
    mTools->addSeparator();
    auto* aProjTime = mTools->addAction("&Project time...");
    connect(aProjTime, &QAction::triggered, this, [this]{
        ProjectTimeDialog dlg(&tracker_, fileList_, this);
        dlg.exec();
    });

    // --- status ---
    fileLabel_ = new QLabel("(no project)");
    statusBar()->addWidget(fileLabel_);
    timeLabel_ = new QLabel();
    timeLabel_->setToolTip("Active time spent on this file");
    statusBar()->addPermanentWidget(timeLabel_);
    hudLabel_ = new QLabel();
    statusBar()->addPermanentWidget(hudLabel_);

    // --- signals ---
    connect(aQuit,   &QAction::triggered, this, &QMainWindow::close);
    connect(aFit,  &QAction::triggered, this, &CannyMainWindow::onFit);
    connect(aOne,  &QAction::triggered, this, &CannyMainWindow::onOneToOne);

    connect(loSlider_,  &QSlider::valueChanged, this, &CannyMainWindow::onLoChanged);
    connect(hiSlider_,  &QSlider::valueChanged, this, &CannyMainWindow::onHiChanged);
    connect(loSpin_,    QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ loSlider_->setValue(v); });
    connect(hiSpin_,    QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ hiSlider_->setValue(v); });
    connect(lockCb_,    &QCheckBox::toggled,    this, &CannyMainWindow::onLockToggled);
    connect(filterCb_,  &QCheckBox::toggled,    this, &CannyMainWindow::onFilterToggled);
    connect(filterOutlineCb_, &QCheckBox::toggled, view_, &CannyViewWidget::setFilterOutline);
    connect(viewModeCb_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CannyMainWindow::setViewMode);

    // Digit keys 1..7 pick a preset; Tab swaps with previous.
    for (int i = 0; i < 7; ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::Key_1 + i), this);
        connect(sc, &QShortcut::activated, this, [this, i]{
            viewModeCb_->setCurrentIndex(i);
        });
    }
    auto* scTab = new QShortcut(QKeySequence(Qt::Key_Tab), this);
    scTab->setContext(Qt::ApplicationShortcut);
    connect(scTab, &QShortcut::activated, this, [this]{
        viewModeCb_->setCurrentIndex(prevViewMode_);
    });

    connect(aUndo_, &QAction::triggered, this, &CannyMainWindow::onUndo);
    connect(aRedo_, &QAction::triggered, this, &CannyMainWindow::onRedo);
    connect(aSetProj, &QAction::triggered, this, &CannyMainWindow::onSetProject);
    connect(aPrev_,        &QAction::triggered, this, &CannyMainWindow::onPrevFile);
    connect(aNext_,        &QAction::triggered, this, &CannyMainWindow::onNextFile);
    connect(aFirstEmpty_,  &QAction::triggered, this, &CannyMainWindow::onFirstEmpty);
    connect(fileSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){
                if (guard_) return;
                const int idx = v - 1;
                if (idx < 0 || idx >= fileList_.size()) return;
                if (idx == fileIndex_) return;
                if (dirty_ && !doSave()) {
                    guard_ = true;
                    fileSpin_->setValue(fileIndex_ + 1);
                    guard_ = false;
                    return;
                }
                loadProjectIndex(idx);
            });

    connect(view_, &CannyViewWidget::hudUpdate,             this, &CannyMainWindow::onHud);
    connect(view_, &CannyViewWidget::outlineOp,             this, &CannyMainWindow::onOutlineOp);
    connect(view_, &CannyViewWidget::outlineBulkOp,         this, &CannyMainWindow::onOutlineBulkOp);
    connect(view_, &CannyViewWidget::rectSelectionFinished, this, &CannyMainWindow::onRectSelectionFinished);
    connect(view_, &CannyViewWidget::analysisReady,         this, &CannyMainWindow::onAnalysisReady);

    // Time tracking
    qApp->installEventFilter(this);
    connect(&tracker_, &TimeTracker::tick, this,
            [this](const QString&, qint64 s) {
        timeLabel_->setText("Time: " + TimeTracker::formatHMS(s));
    });
    connect(doneBtn_, &QPushButton::toggled, this, [this](bool on) {
        if (!currentPath_.isEmpty()) {
            tracker_.setDone(QFileInfo(currentPath_).fileName(), on);
        }
        updateDoneButton(on);
    });

    connect(minSizeSpin_,   QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CannyMainWindow::onMinSizeChanged);
    connect(minExtentSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CannyMainWindow::onMinExtentChanged);
    connect(joinTolSpin_,   QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CannyMainWindow::onJoinTolChanged);
    connect(allDarkerCb_,   &QCheckBox::toggled,
            this, &CannyMainWindow::onAllDarkerToggled);
    connect(conn8Cb_,       &QCheckBox::toggled,
            this, &CannyMainWindow::onConn8Toggled);

    syncRangeToView();
}

void CannyMainWindow::syncRangeToView()
{
    if (view_) view_->setRange(loSlider_->value(), hiSlider_->value());
    if (loSpin_->value() != loSlider_->value()) {
        QSignalBlocker b(loSpin_);
        loSpin_->setValue(loSlider_->value());
    }
    if (hiSpin_->value() != hiSlider_->value()) {
        QSignalBlocker b(hiSpin_);
        hiSpin_->setValue(hiSlider_->value());
    }
}

void CannyMainWindow::onOpen()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Open canny gray image", QString(),
        "PNG (*.png);;All files (*)");
    if (path.isEmpty()) return;
    if (!loadFile(path)) {
        QMessageBox::warning(this, "Open", "Failed to load file.");
    }
}

void CannyMainWindow::closeEvent(QCloseEvent* e)
{
    if (dirty_) doSave();   // silent auto-save; no prompt
    tracker_.flush();
    e->accept();
}

bool CannyMainWindow::eventFilter(QObject* obj, QEvent* e)
{
    switch (e->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            tracker_.registerActivity();
            break;
        default: break;
    }
    return QMainWindow::eventFilter(obj, e);
}

void CannyMainWindow::updateDoneButton(bool done)
{
    if (!doneBtn_) return;
    QSignalBlocker b(doneBtn_);
    doneBtn_->setChecked(done);
    if (done) {
        doneBtn_->setText("Done ✓");
        doneBtn_->setStyleSheet(
            "QPushButton{background-color:#2a8a2a;color:white;"
            "padding:4px 14px;border:1px solid #1f6f1f;border-radius:4px;}");
    } else {
        doneBtn_->setText("Not done");
        doneBtn_->setStyleSheet(
            "QPushButton{background-color:#c33;color:white;"
            "padding:4px 14px;border:1px solid #911;border-radius:4px;}");
    }
}

bool CannyMainWindow::maybeSave()
{
    if (!dirty_) return true;
    const auto r = QMessageBox::warning(
        this, "cannyToOutline",
        "Outline has unsaved changes.\nSave?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Discard) return true;
    return doSave();
}

bool CannyMainWindow::doSave()
{
    if (!view_->hasOutline()) return true;   // nothing to save = OK
    QString path = outlinePath_.isEmpty() ? defaultOutlinePath() : outlinePath_;
    if (path.isEmpty()) return false;
    if (!view_->saveOutline(path)) {
        QMessageBox::warning(this, "Save", "Save failed: " + path);
        return false;
    }
    outlinePath_ = path;
    dirty_ = false;
    updateTitle();
    statusBar()->showMessage("Saved: " + path, 3000);
    return true;
}

void CannyMainWindow::updateTitle()
{
    const QString proj = projectPath_.isEmpty()
        ? QString() : QFileInfo(projectPath_).completeBaseName();
    if (currentPath_.isEmpty()) {
        setWindowTitle(proj.isEmpty()
            ? "cannyToOutline"
            : QString("cannyToOutline — [%1]").arg(proj));
        return;
    }
    setWindowTitle(QString("cannyToOutline — %1%2%3")
                       .arg(currentPath_,
                            dirty_ ? " *" : "",
                            proj.isEmpty() ? QString() : QString("  [%1]").arg(proj)));
}

bool CannyMainWindow::loadFile(const QString& path)
{
    cv::Mat m = cv::imread(path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (m.empty()) return false;
    currentPath_ = path;
    outlinePath_.clear();
    view_->setSource(m);
    view_->fitToWindow();
    undoStack_.clear();
    redoStack_.clear();
    dirty_ = false;
    updateUndoActions();
    const QString name = QFileInfo(path).fileName();
    tracker_.setCurrentFile(name);
    updateDoneButton(tracker_.isDone(name));
    timeLabel_->setText("Time: " + TimeTracker::formatHMS(tracker_.secondsFor(name)));

    // if there is an outline file alongside — load it
    const QString op = defaultOutlinePath();
    if (!op.isEmpty() && QFileInfo::exists(op)) {
        cv::Mat o = cv::imread(op.toStdString(), cv::IMREAD_GRAYSCALE);
        if (!o.empty()) {
            view_->setOutlineMask(o);
            outlinePath_ = op;
            statusBar()->showMessage("Loaded outline: " + op, 3000);
        }
    }

    // optional "original" — search by stem, any extension
    cv::Mat orig;
    if (!project_.originalDir.isEmpty() && QDir(project_.originalDir).exists()) {
        const QString stem = QFileInfo(path).completeBaseName();
        const QStringList exts = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"};
        QDir od(project_.originalDir);
        QString found;
        for (const QString& e : exts) {
            const QString cand = od.filePath(stem + e);
            if (QFileInfo::exists(cand)) { found = cand; break; }
        }
        if (found.isEmpty()) {
            const QStringList list = od.entryList(QStringList{stem + ".*"}, QDir::Files, QDir::Name);
            if (!list.isEmpty()) found = od.filePath(list.first());
        }
        if (!found.isEmpty()) {
            orig = cv::imread(found.toStdString(), cv::IMREAD_UNCHANGED);
        }
    }
    view_->setOriginal(orig);
    hasOriginal_ = !orig.empty();
    // Disable presets that need the original photo when it's missing.
    if (auto* model = qobject_cast<QStandardItemModel*>(viewModeCb_->model())) {
        for (int i : {0, 1}) {
            if (auto* it = model->item(i)) it->setEnabled(hasOriginal_);
        }
    }
    if (!hasOriginal_ && (viewMode_ == 0 || viewMode_ == 1)) {
        viewModeCb_->setCurrentIndex(3);   // falls back to "Gray source"
    }

    updateTitle();
    return true;
}

void CannyMainWindow::onLoChanged(int v)
{
    if (guard_) return;
    guard_ = true;
    if (lockCb_->isChecked()) {
        int nh = v + lockDelta_;
        if (nh > 254) { nh = 254; v = nh - lockDelta_; loSlider_->setValue(v); }
        if (nh < 0)   { nh = 0;   v = nh - lockDelta_; loSlider_->setValue(v); }
        hiSlider_->setValue(nh);
    } else if (v > hiSlider_->value()) {
        hiSlider_->setValue(v);
    }
    guard_ = false;
    syncRangeToView();
}

void CannyMainWindow::onHiChanged(int v)
{
    if (guard_) return;
    guard_ = true;
    if (lockCb_->isChecked()) {
        int nl = v - lockDelta_;
        if (nl < 0)   { nl = 0;   v = nl + lockDelta_; hiSlider_->setValue(v); }
        if (nl > 254) { nl = 254; v = nl + lockDelta_; hiSlider_->setValue(v); }
        loSlider_->setValue(nl);
    } else if (v < loSlider_->value()) {
        loSlider_->setValue(v);
    }
    guard_ = false;
    syncRangeToView();
}

void CannyMainWindow::onLockToggled(bool on)
{
    if (on) lockDelta_ = hiSlider_->value() - loSlider_->value();
}

QString CannyMainWindow::defaultOutlinePath() const
{
    if (currentPath_.isEmpty()) return {};
    QFileInfo fi(currentPath_);
    if (project_.isValid()) {
        // same name, different directory
        return QDir(project_.outputDir).filePath(fi.fileName());
    }
    return fi.absoluteDir().filePath(fi.completeBaseName() + "_outline.png");
}

void CannyMainWindow::onSave()
{
    if (!view_->hasOutline()) {
        QMessageBox::information(this, "Save", "No outline to save.");
        return;
    }
    doSave();
}

void CannyMainWindow::onSaveAs()
{
    if (!view_->hasOutline()) {
        QMessageBox::information(this, "Save", "No outline to save.");
        return;
    }
    QString suggested = outlinePath_.isEmpty() ? defaultOutlinePath() : outlinePath_;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save outline", suggested, "PNG (*.png)");
    if (path.isEmpty()) return;
    if (!view_->saveOutline(path)) {
        QMessageBox::warning(this, "Save", "Save failed: " + path);
        return;
    }
    outlinePath_ = path;
    dirty_ = false;
    updateTitle();
    statusBar()->showMessage("Saved: " + path, 3000);
}

void CannyMainWindow::onFilterToggled(bool on)  { view_->setFilter(on); }
namespace {
struct CannyModePreset {
    bool original, source, outline, blackMode, hideDone;
};
// Indexed by viewMode_; order must match QComboBox items in createUi().
constexpr CannyModePreset kCannyPresets[] = {
    /* 0 Original                       */ {true,  false, false, false, false},
    /* 1 Original + outline red         */ {true,  false, true,  false, false},
    /* 2 Outline black                  */ {false, false, false, false, false},
    /* 3 Gray source                    */ {false, true,  false, false, false},
    /* 4 Source (all black)             */ {false, true,  false, true,  false},
    /* 5 Gray source + red outline      */ {false, true,  true,  false, false},
    /* 6 Black source + red outline     */ {false, true,  true,  true,  false},
};
constexpr int kCannyPresetCount = int(sizeof(kCannyPresets) / sizeof(kCannyPresets[0]));
}

void CannyMainWindow::setViewMode(int idx)
{
    if (idx < 0 || idx >= kCannyPresetCount || idx == viewMode_) return;
    // Modes that depend on the original photo fall back to "Gray source"
    // when no original is loaded.
    if (!hasOriginal_ && (idx == 0 || idx == 1)) {
        idx = 3;
        if (viewModeCb_ && viewModeCb_->currentIndex() != idx) {
            QSignalBlocker b(viewModeCb_);
            viewModeCb_->setCurrentIndex(idx);
        }
    }
    prevViewMode_ = viewMode_;
    viewMode_ = idx;
    const auto& p = kCannyPresets[idx];
    view_->setShowOriginal(p.original);
    view_->setShowSource(p.source);
    view_->setShowOutline(p.outline);
    view_->setBlackMode(p.blackMode);
    view_->setHideDone(p.hideDone);
    // Edits (Ctrl-click, Shift-drag/strip) only make sense when both the gray
    // source and the user's outline are visible — presets 5 & 6.
    view_->setEditEnabled(p.source && p.outline);
}
void CannyMainWindow::onMinSizeChanged(int v)   { view_->setMinSize(v); }
void CannyMainWindow::onMinExtentChanged(int v) { view_->setMinExtent(double(v)); }
void CannyMainWindow::onJoinTolChanged(int v)    { view_->setJoinTol(v); }
void CannyMainWindow::onAllDarkerToggled(bool on){ view_->setAllDarker(on); }
void CannyMainWindow::onConn8Toggled(bool on)    { view_->setConn8(on); }

void CannyMainWindow::onAnalysisReady()
{
    const int ms = view_->maxComponentSize();
    const int mx = view_->maxComponentExtent();
    statsLabel_->setText(QString("max size: %1   max extent: %2").arg(ms).arg(mx));
    minSizeSpin_->setMaximum(std::max(0, ms));
    minExtentSpin_->setMaximum(std::max(0, mx));
}

void CannyMainWindow::onFit()       { view_->fitToWindow(); }
void CannyMainWindow::onOneToOne()  { view_->zoomOneToOne(); }
void CannyMainWindow::onHud(const QString& s) { hudLabel_->setText(s); }

void CannyMainWindow::onOutlineOp(QPoint seed, bool add, int joinTol, bool allDarker, bool conn8)
{
    Op op;
    op.isBulk = false;
    op.seed = seed;
    op.add = add;
    op.joinTol = joinTol;
    op.allDarker = allDarker;
    op.conn8 = conn8;
    undoStack_.push_back(op);
    redoStack_.clear();
    dirty_ = true;
    updateUndoActions();
    updateTitle();
}

void CannyMainWindow::onOutlineBulkOp(cv::Mat mask, bool add)
{
    if (mask.empty()) return;
    Op op; op.isBulk = true; op.add = add; op.mask = mask;
    undoStack_.push_back(op);
    redoStack_.clear();
    dirty_ = true;
    updateUndoActions();
    updateTitle();
}

void CannyMainWindow::onRectSelectionFinished()
{
    if (rectDialog_) { rectDialog_->close(); rectDialog_ = nullptr; }

    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlags(dlg->windowFlags() | Qt::Tool);
    dlg->setWindowTitle("Rect threshold");
    dlg->setModal(false);

    auto* sb = new CountSnapSpinBox(dlg,
        [v = view_](int x){ return v->candAddCountIf(x); });
    sb->setRange(0, 254);
    sb->setValue(lastRectThreshold_);
    sb->setAccelerated(true);
    sb->setToolTip("Stepping jumps to the next value where the count changes");

    auto* modeCb = new QComboBox(dlg);
    modeCb->addItem("Touching (components partially)",     0);
    modeCb->addItem("Inside (components fully)",           1);
    modeCb->setCurrentIndex(lastCandidateMode_);

    auto* countsLbl = new QLabel(dlg);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);

    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(new QLabel("≤ threshold → red, > → keep gray"));
    lay->addWidget(sb);
    lay->addWidget(new QLabel("candidates:"));
    lay->addWidget(modeCb);
    lay->addWidget(countsLbl);
    lay->addWidget(bb);

    view_->setCandidateMode(modeCb->currentIndex());
    view_->setRectThreshold(sb->value());

    auto refreshCounts = [this, sb, countsLbl]() {
        const QLocale loc = QLocale::system();
        const int blue   = view_->candAddCountIf(sb->value());
        const int yellow = view_->candRejectCountIf(sb->value());
        countsLbl->setText(
            QString("adding %1 px,  past threshold %2 px")
                .arg(loc.toString(blue)).arg(loc.toString(yellow)));
    };
    refreshCounts();

    connect(modeCb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, refreshCounts](int m) {
                view_->setCandidateMode(m);
                refreshCounts();
            });
    connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, refreshCounts](int v) {
                view_->setRectThreshold(v);
                refreshCounts();
            });
    connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(dlg, &QDialog::accepted, this, [this, sb, modeCb]() {
        lastRectThreshold_ = sb->value();
        lastCandidateMode_ = modeCb->currentIndex();
        view_->commitRectSelection();
        rectDialog_ = nullptr;
    });
    connect(dlg, &QDialog::rejected, this, [this]() {
        view_->cancelRectSelection();
        rectDialog_ = nullptr;
    });

    rectDialog_ = dlg;
    dlg->adjustSize();
    dlg->move(view_->rectAnchorGlobal() + QPoint(8, 8));
    dlg->show();
    sb->setFocus();
}

void CannyMainWindow::onUndo()
{
    if (undoStack_.isEmpty()) return;
    const Op op = undoStack_.takeLast();
    if (op.isBulk) view_->applyBulkOp(op.mask, !op.add);
    else           view_->applyOp(op.seed, !op.add, op.joinTol, op.allDarker, op.conn8);
    redoStack_.push_back(op);
    dirty_ = true;
    updateUndoActions();
    updateTitle();
}

void CannyMainWindow::onRedo()
{
    if (redoStack_.isEmpty()) return;
    const Op op = redoStack_.takeLast();
    if (op.isBulk) view_->applyBulkOp(op.mask, op.add);
    else           view_->applyOp(op.seed, op.add, op.joinTol, op.allDarker, op.conn8);
    undoStack_.push_back(op);
    dirty_ = true;
    updateUndoActions();
    updateTitle();
}

void CannyMainWindow::onFitToOthers()
{
    if (!view_->hasImage()) {
        QMessageBox::information(this, "Fit to others", "Load a source file first.");
        return;
    }

    if (fitPanel_) {
        if (fitPanel_->isVisible()) {
            view_->exitFitMode();
            fitPanel_->setVisible(false);
        } else {
            if (thrPanel_ && thrPanel_->isVisible()) {
                view_->exitThresholdMode();
                thrPanel_->setVisible(false);
            }
            fitPanel_->setVisible(true);
            if (fitShowFn_) fitShowFn_();
        }
        return;
    }
    if (thrPanel_ && thrPanel_->isVisible()) {
        view_->exitThresholdMode();
        thrPanel_->setVisible(false);
    }

    auto* panel = new QWidget(centralSplitter_);

    auto* title = new QLabel("<b>Fit to others</b>", panel);
    auto* fileBtn = new QPushButton("File…", panel);
    auto* fileLbl = new QLabel("(none)", panel);
    fileLbl->setStyleSheet("color: #888;");
    fileLbl->setWordWrap(true);

    auto* covSpin = new QSpinBox(panel);
    covSpin->setRange(0, 100); covSpin->setSuffix(" %"); covSpin->setAccelerated(true);
    covSpin->setValue(fitLastCoverage_);
    auto* tolSpin = new QSpinBox(panel);
    tolSpin->setRange(0, 10); tolSpin->setSuffix(" px"); tolSpin->setAccelerated(true);
    tolSpin->setValue(fitLastTol_);
    auto* minGreenSpin = new QSpinBox(panel);
    minGreenSpin->setRange(0, 1000); minGreenSpin->setSuffix(" px");
    minGreenSpin->setAccelerated(true);
    minGreenSpin->setValue(fitLastMinGreen_);

    auto* appendCb = new QCheckBox("Append only", panel);
    appendCb->setToolTip("only add green, do not remove purple");
    appendCb->setChecked(fitLastAppend_);
    auto* showOrigCb = new QCheckBox("Show original", panel);
    showOrigCb->setChecked(fitLastShowOrig_);

    auto* okBtn = new QPushButton("OK", panel);
    auto* cancelBtn = new QPushButton("Cancel", panel);
    auto* setDefBtn = new QPushButton("Defaults", panel);
    auto* bulkBtn = new QPushButton("Bulk…", panel);
    bulkBtn->setToolTip("Apply to all source files (snapshot first)");
    okBtn->setDefault(true);
    okBtn->setEnabled(false);

    connect(bulkBtn, &QPushButton::clicked, this, &CannyMainWindow::onFitBulk);

    for (auto k : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* sc = new QShortcut(QKeySequence(k), panel);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, panel, [okBtn]() {
            if (okBtn->isEnabled()) okBtn->click();
        });
    }

    auto* form = new QVBoxLayout;
    form->setSpacing(4);
    auto addRow = [form, panel](const QString& l, QWidget* w) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(l, panel));
        row->addWidget(w);
        row->addStretch(1);
        form->addLayout(row);
    };
    form->addWidget(fileBtn);
    form->addWidget(fileLbl);
    addRow("coverage:",    covSpin);
    addRow("tol:",         tolSpin);
    addRow("min segment:", minGreenSpin);
    form->addWidget(appendCb);
    form->addWidget(showOrigCb);

    auto* bulkRow = new QHBoxLayout;
    bulkRow->addWidget(setDefBtn);
    bulkRow->addStretch(1);
    bulkRow->addWidget(bulkBtn);
    auto* bbRow = new QHBoxLayout;
    bbRow->addStretch(1);
    bbRow->addWidget(cancelBtn);
    bbRow->addWidget(okBtn);

    auto* lay = new QVBoxLayout(panel);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    lay->addWidget(title);
    lay->addLayout(form);
    lay->addStretch(1);
    lay->addLayout(bulkRow);
    lay->addLayout(bbRow);

    auto loadAndEnter = [this, covSpin, tolSpin, appendCb, okBtn](const QString& path) {
        cv::Mat ref = cv::imread(path.toStdString(), cv::IMREAD_GRAYSCALE);
        if (ref.empty()) {
            QMessageBox::warning(this, "Fit to others",
                "Failed to load: " + path);
            return;
        }
        view_->enterFitMode(ref, true, covSpin->value(),
                            tolSpin->value(), appendCb->isChecked());
        okBtn->setEnabled(true);
    };

    connect(fileBtn, &QPushButton::clicked, this,
            [this, fileLbl, loadAndEnter]() {
        const QString path = QFileDialog::getOpenFileName(
            this, "Choose fit file", QString(),
            "PNG (*.png);;All files (*)");
        if (path.isEmpty()) return;
        fileLbl->setText(QFileInfo(path).fileName());
        loadAndEnter(path);
    });

    connect(appendCb, &QCheckBox::toggled, view_, &CannyViewWidget::setFitAppend);
    connect(showOrigCb, &QCheckBox::toggled, this, [this](bool on){
        view_->setShowSource(on);
    });
    connect(covSpin,  QOverload<int>::of(&QSpinBox::valueChanged),
            view_, &CannyViewWidget::setFitCoverage);
    connect(tolSpin,  QOverload<int>::of(&QSpinBox::valueChanged),
            view_, &CannyViewWidget::setFitTol);
    connect(minGreenSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            view_, &CannyViewWidget::setFitMinGreen);

    connect(setDefBtn, &QPushButton::clicked, this,
            [covSpin, tolSpin, minGreenSpin, appendCb, showOrigCb]() {
        covSpin->setValue(75);
        tolSpin->setValue(2);
        minGreenSpin->setValue(0);
        appendCb->setChecked(true);
        showOrigCb->setChecked(true);
    });

    connect(appendCb,     &QCheckBox::toggled,
            this, [this](bool v){ fitLastAppend_ = v; });
    connect(showOrigCb,   &QCheckBox::toggled,
            this, [this](bool v){ fitLastShowOrig_ = v; });
    connect(covSpin,      QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ fitLastCoverage_ = v; });
    connect(tolSpin,      QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ fitLastTol_ = v; });
    connect(minGreenSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ fitLastMinGreen_ = v; });

    // wasShowingSource remembered once on show
    auto* wasShowingSource = new bool(false);
    connect(panel, &QObject::destroyed, this, [wasShowingSource]() { delete wasShowingSource; });

    connect(okBtn, &QPushButton::clicked, this,
            [this, panel, wasShowingSource]() {
        view_->commitFitMode();
        view_->setShowSource(*wasShowingSource);
        panel->setVisible(false);
    });
    connect(cancelBtn, &QPushButton::clicked, this,
            [this, panel, wasShowingSource]() {
        view_->exitFitMode();
        view_->setShowSource(*wasShowingSource);
        panel->setVisible(false);
    });

    fitShowFn_ = [this, fileLbl, fileBtn, okBtn, showOrigCb, loadAndEnter, wasShowingSource]() {
        *wasShowingSource = view_->showSource();
        view_->setShowSource(showOrigCb->isChecked());
        okBtn->setEnabled(false);

        QString autoPath;
        if (!project_.referenceDir.isEmpty() && !currentPath_.isEmpty()) {
            const QString name = QFileInfo(currentPath_).fileName();
            const QString p = QDir(project_.referenceDir).filePath(name);
            if (QFileInfo::exists(p)) autoPath = p;
        }
        if (!autoPath.isEmpty()) {
            fileLbl->setText(QFileInfo(autoPath).fileName());
            fileBtn->setVisible(false);
            loadAndEnter(autoPath);
        } else {
            fileLbl->setText("(none)");
            fileBtn->setVisible(true);
        }
    };

    panel->setMinimumWidth(140);
    fitPanel_ = panel;
    centralSplitter_->addWidget(panel);
    centralSplitter_->setStretchFactor(1, 0);
    centralSplitter_->setSizes({width() - 180, 180});

    fitShowFn_();
}

void CannyMainWindow::onThresholdTool()
{
    if (!view_->hasImage()) return;

    auto enterMode = [this]() {
        view_->enterThresholdMode(thrAddOn_, thrAddVal_,
                                  thrRemoveOn_, thrRemoveVal_, thrFinalOn_);
        if (thrLimitOn_) {
            view_->setThresholdRegionMode(thrRegionMode_);
            view_->setThresholdLimitRegion(true);
        }
    };

    if (thrPanel_) {
        if (thrPanel_->isVisible()) {
            view_->exitThresholdMode();
            thrPanel_->setVisible(false);
        } else {
            if (fitPanel_ && fitPanel_->isVisible()) {
                view_->exitFitMode();
                fitPanel_->setVisible(false);
            }
            thrPanel_->setVisible(true);
            enterMode();
        }
        return;
    }
    if (fitPanel_ && fitPanel_->isVisible()) {
        view_->exitFitMode();
        fitPanel_->setVisible(false);
    }

    auto* panel = new QWidget(centralSplitter_);

    auto* addCb = new QCheckBox("add  (gray ≤)", panel);
    auto* addSb = new CountSnapSpinBox(panel,
        [v = view_](int x){ return v->thresholdAddCountIf(x); });
    addSb->setRange(0, 254);
    addSb->setAccelerated(true);

    auto* rmCb = new QCheckBox("remove  (gray ≥)", panel);
    auto* rmSb = new CountSnapSpinBox(panel,
        [v = view_](int x){ return v->thresholdRemoveCountIf(x); });
    rmSb->setRange(1, 255);
    rmSb->setAccelerated(true);

    auto* finalCb = new QCheckBox("final preview", panel);
    finalCb->setToolTip("respect global filters");

    auto* limitCb = new QCheckBox("Limit to region", panel);
    auto* regionModeCb = new QComboBox(panel);
    regionModeCb->addItem("Touching", 0);
    regionModeCb->addItem("Inside",   1);
    regionModeCb->setToolTip("Touching = components partially, Inside = fully");
    auto* regionHint = new QLabel(
        "Shift+drag — rect; Shift+click ×3 — strip", panel);
    regionHint->setStyleSheet("color: #888;");
    regionHint->setWordWrap(true);
    regionHint->setMinimumWidth(1);
    auto* clearRegBtn = new QPushButton("clear region", panel);

    auto* countsLbl = new QLabel("", panel);

    auto* okBtn = new QPushButton("OK", panel);
    auto* cancelBtn = new QPushButton("Cancel", panel);
    auto* bulkBtn = new QPushButton("Bulk…", panel);
    bulkBtn->setToolTip("Apply to all source files (snapshot first)");
    okBtn->setDefault(true);

    connect(bulkBtn, &QPushButton::clicked, this, &CannyMainWindow::onThresholdBulk);

    // Enter → OK (when focus is on the panel)
    for (auto k : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* sc = new QShortcut(QKeySequence(k), panel);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, panel, [okBtn]() { okBtn->click(); });
    }

    addCb->setChecked(thrAddOn_);
    rmCb->setChecked(thrRemoveOn_);
    addSb->setValue(thrAddVal_);
    rmSb->setValue(thrRemoveVal_);
    finalCb->setChecked(thrFinalOn_);
    addSb->setMaximum(rmSb->value() - 1);
    rmSb->setMinimum(addSb->value() + 1);
    limitCb->setChecked(thrLimitOn_);
    regionModeCb->setCurrentIndex(thrRegionMode_);
    regionModeCb->setVisible(thrLimitOn_);
    regionHint->setVisible(thrLimitOn_);
    clearRegBtn->setVisible(thrLimitOn_);

    auto* title = new QLabel("<b>Threshold add/remove</b>", panel);
    auto* form = new QVBoxLayout;
    form->setSpacing(4);
    auto* row1 = new QHBoxLayout; row1->addWidget(addCb); row1->addWidget(addSb, 1);
    auto* row2 = new QHBoxLayout; row2->addWidget(rmCb);  row2->addWidget(rmSb, 1);
    form->addLayout(row1);
    form->addLayout(row2);
    form->addWidget(finalCb);
    form->addWidget(limitCb);
    form->addWidget(regionModeCb);
    form->addWidget(regionHint);
    form->addWidget(clearRegBtn);
    form->addWidget(countsLbl);

    auto* bulkRow = new QHBoxLayout;
    bulkRow->addStretch(1);
    bulkRow->addWidget(bulkBtn);
    auto* bbRow = new QHBoxLayout;
    bbRow->addStretch(1);
    bbRow->addWidget(cancelBtn);
    bbRow->addWidget(okBtn);

    auto* lay = new QVBoxLayout(panel);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    lay->addWidget(title);
    lay->addLayout(form);
    lay->addStretch(1);
    lay->addLayout(bulkRow);
    lay->addLayout(bbRow);

    auto pushToView = [this, addCb, addSb, rmCb, rmSb, finalCb]() {
        thrAddOn_     = addCb->isChecked();
        thrAddVal_    = addSb->value();
        thrRemoveOn_  = rmCb->isChecked();
        thrRemoveVal_ = rmSb->value();
        thrFinalOn_   = finalCb->isChecked();
        view_->setThresholdAdd(thrAddOn_, thrAddVal_);
        view_->setThresholdRemove(thrRemoveOn_, thrRemoveVal_);
        view_->setThresholdFinal(thrFinalOn_);
    };

    connect(addSb, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [rmSb, pushToView](int v){ rmSb->setMinimum(v + 1); pushToView(); });
    connect(rmSb, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [addSb, pushToView](int v){ addSb->setMaximum(v - 1); pushToView(); });
    connect(addCb, &QCheckBox::toggled, this, [pushToView](bool){ pushToView(); });
    connect(rmCb,  &QCheckBox::toggled, this, [pushToView](bool){ pushToView(); });
    connect(finalCb, &QCheckBox::toggled, this, [pushToView](bool){ pushToView(); });
    connect(limitCb, &QCheckBox::toggled, this,
            [this, regionModeCb, regionHint, clearRegBtn](bool on) {
        thrLimitOn_ = on;
        regionModeCb->setVisible(on);
        regionHint->setVisible(on);
        clearRegBtn->setVisible(on);
        view_->setThresholdLimitRegion(on);
        if (on) view_->setThresholdRegionMode(regionModeCb->currentData().toInt());
    });
    connect(regionModeCb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, regionModeCb](int) {
        thrRegionMode_ = regionModeCb->currentData().toInt();
        view_->setThresholdRegionMode(thrRegionMode_);
    });
    connect(clearRegBtn, &QPushButton::clicked, this,
            [this]() { view_->clearThresholdRegion(); });

    connect(view_, &CannyViewWidget::thresholdCountsChanged,
            this, [countsLbl](int a, int r){
        const QLocale loc = QLocale::system();
        countsLbl->setText(QString("adding %1 px,  removing %2 px")
                           .arg(loc.toString(a)).arg(loc.toString(r)));
    });

    auto saveValues = [this, addCb, addSb, rmCb, rmSb, finalCb, limitCb, regionModeCb]() {
        thrAddOn_     = addCb->isChecked();
        thrAddVal_    = addSb->value();
        thrRemoveOn_  = rmCb->isChecked();
        thrRemoveVal_ = rmSb->value();
        thrFinalOn_   = finalCb->isChecked();
        thrLimitOn_   = limitCb->isChecked();
        thrRegionMode_= regionModeCb->currentData().toInt();
    };
    connect(okBtn, &QPushButton::clicked, this, [this, panel, saveValues]() {
        saveValues();
        view_->commitThreshold();
        panel->setVisible(false);
    });
    connect(cancelBtn, &QPushButton::clicked, this, [this, panel, saveValues]() {
        saveValues();
        view_->exitThresholdMode();
        panel->setVisible(false);
    });

    panel->setMinimumWidth(140);
    thrPanel_ = panel;
    centralSplitter_->addWidget(panel);
    centralSplitter_->setStretchFactor(1, 0);
    centralSplitter_->setSizes({width() - 200, 200});

    enterMode();
}

void CannyMainWindow::updateUndoActions()
{
    aUndo_->setEnabled(!undoStack_.isEmpty());
    aRedo_->setEnabled(!redoStack_.isEmpty());
}

void CannyMainWindow::onSetProject()
{
    if (projectPath_.isEmpty()) {
        QMessageBox::information(this, "Set project dirs",
            "Create or open a project first (File → New/Open project).");
        return;
    }
    ProjectDialog dlg(project_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const ProjectConfig nc = dlg.config();
    if (!nc.isValid()) {
        QMessageBox::warning(this, "Set project dirs",
            "Source and Output must point to existing directories.");
        return;
    }
    project_ = nc;
    saveCurrentProject();
    scanProject();
}

void CannyMainWindow::onNewProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, "New project",
        startDir, "Canny project (*.ctoprj)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".ctoprj", Qt::CaseInsensitive)) path += ".ctoprj";
    createNewProjectAt(path);
}

void CannyMainWindow::onOpenProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Open project",
        startDir, "Canny project (*.ctoprj);;All files (*)");
    if (path.isEmpty()) return;
    loadProjectFromPath(path);
}

void CannyMainWindow::onFitBulk()
{
    if (!project_.isValid()) {
        QMessageBox::warning(this, "Bulk fit", "No valid project.");
        return;
    }
    if (project_.referenceDir.isEmpty()) {
        QMessageBox::warning(this, "Bulk fit",
            "No fit reference directory in the project (Set project dirs).");
        return;
    }
    if (!QDir(project_.referenceDir).exists()) {
        QMessageBox::warning(this, "Bulk fit",
            "Fit reference directory does not exist:\n" + project_.referenceDir);
        return;
    }
    QDir srcDir(project_.sourceDir);
    const QStringList sources = srcDir.entryList(
        QStringList{"*.png"}, QDir::Files, QDir::Name);
    if (sources.isEmpty()) {
        QMessageBox::warning(this, "Bulk fit", "No .png files in source.");
        return;
    }
    QDir refDir(project_.referenceDir);
    int matched = 0;
    for (const QString& f : sources)
        if (QFileInfo::exists(refDir.filePath(f))) ++matched;
    if (matched == 0) {
        QMessageBox::warning(this, "Bulk fit",
            "No source file has a matching fit reference.");
        return;
    }

    // snapshot dir (sibling of outputDir)
    const QString outDir = project_.outputDir;
    const QString outName = QDir(outDir).dirName();
    const QString parent = QFileInfo(outDir).absolutePath();
    QString snap;
    for (int i = 0; ; ++i) {
        const QString cand = QDir(parent).filePath(
            outName + "_" + QString::number(i));
        if (!QFileInfo::exists(cand)) { snap = cand; break; }
    }

    const QString msg = (matched == sources.size())
        ? QString("Process %1 files\nSnapshot → %2").arg(sources.size()).arg(snap)
        : QString("Process %1/%2 files (%3 skipped without fit-ref)\nSnapshot → %4")
            .arg(matched).arg(sources.size())
            .arg(sources.size() - matched).arg(snap);
    if (QMessageBox::question(this, "Bulk fit", msg,
            QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) return;

    if (!QDir().mkpath(snap)) {
        QMessageBox::warning(this, "Bulk fit", "Failed to create: " + snap);
        return;
    }
    QDir od(outDir);
    const QStringList existing = od.entryList(
        QStringList{"*.png"}, QDir::Files, QDir::Name);
    for (const QString& f : existing)
        QFile::copy(od.filePath(f), QDir(snap).filePath(f));

    QProgressDialog progress("Bulk fit...", "Cancel", 0, sources.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    const int savedIdx = fileIndex_;
    int processed = 0;
    view_->setUpdatesEnabled(false);
    {
        QSignalBlocker blocker(view_);
        for (int i = 0; i < sources.size(); ++i) {
            progress.setValue(i);
            QCoreApplication::processEvents();
            if (progress.wasCanceled()) break;
            const QString& f = sources[i];
            const QString refPath = refDir.filePath(f);
            if (!QFileInfo::exists(refPath)) continue;
            const QString srcPath = srcDir.filePath(f);
            cv::Mat src = cv::imread(srcPath.toStdString(), cv::IMREAD_GRAYSCALE);
            cv::Mat ref = cv::imread(refPath.toStdString(), cv::IMREAD_GRAYSCALE);
            if (src.empty() || ref.empty()) continue;
            const QString outPath = od.filePath(f);
            cv::Mat outl;
            if (QFileInfo::exists(outPath))
                outl = cv::imread(outPath.toStdString(), cv::IMREAD_GRAYSCALE);
            if (outl.empty())
                outl = cv::Mat(src.size(), CV_8UC1, cv::Scalar(255));

            view_->setSource(src);
            view_->setOutlineMask(outl);
            view_->enterFitMode(ref, true, fitLastCoverage_,
                                fitLastTol_, fitLastAppend_);
            view_->setFitMinGreen(fitLastMinGreen_);
            view_->commitFitMode();
            view_->saveOutline(outPath);
            ++processed;
        }
        progress.setValue(sources.size());
    }
    view_->setUpdatesEnabled(true);

    // reload current
    if (savedIdx >= 0) loadProjectIndex(savedIdx);

    QMessageBox::information(this, "Bulk fit",
        QString("Processed %1 files.\nBackup: %2").arg(processed).arg(snap));
}

void CannyMainWindow::onThresholdBulk()
{
    if (!project_.isValid()) {
        QMessageBox::warning(this, "Bulk threshold", "No valid project.");
        return;
    }
    QDir srcDir(project_.sourceDir);
    const QStringList sources = srcDir.entryList(
        QStringList{"*.png"}, QDir::Files, QDir::Name);
    if (sources.isEmpty()) {
        QMessageBox::warning(this, "Bulk threshold", "No .png files in source.");
        return;
    }

    // snapshot dir
    const QString outDir = project_.outputDir;
    const QString outName = QDir(outDir).dirName();
    const QString parent = QFileInfo(outDir).absolutePath();
    QString snap;
    for (int i = 0; ; ++i) {
        const QString cand = QDir(parent).filePath(
            outName + "_" + QString::number(i));
        if (!QFileInfo::exists(cand)) { snap = cand; break; }
    }

    const QString msg =
        QString("Process %1 files\nSnapshot → %2").arg(sources.size()).arg(snap);
    if (QMessageBox::question(this, "Bulk threshold", msg,
            QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) return;

    if (!QDir().mkpath(snap)) {
        QMessageBox::warning(this, "Bulk threshold",
            "Failed to create: " + snap);
        return;
    }
    QDir od(outDir);
    const QStringList existing = od.entryList(
        QStringList{"*.png"}, QDir::Files, QDir::Name);
    for (const QString& f : existing)
        QFile::copy(od.filePath(f), QDir(snap).filePath(f));

    QProgressDialog progress("Bulk threshold...", "Cancel", 0, sources.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    const int savedIdx = fileIndex_;
    int processed = 0;
    view_->setUpdatesEnabled(false);
    {
        QSignalBlocker blocker(view_);
        for (int i = 0; i < sources.size(); ++i) {
            progress.setValue(i);
            QCoreApplication::processEvents();
            if (progress.wasCanceled()) break;
            const QString& f = sources[i];
            const QString srcPath = srcDir.filePath(f);
            cv::Mat src = cv::imread(srcPath.toStdString(), cv::IMREAD_GRAYSCALE);
            if (src.empty()) continue;
            const QString outPath = od.filePath(f);
            cv::Mat outl;
            if (QFileInfo::exists(outPath))
                outl = cv::imread(outPath.toStdString(), cv::IMREAD_GRAYSCALE);
            if (outl.empty())
                outl = cv::Mat(src.size(), CV_8UC1, cv::Scalar(255));

            view_->setSource(src);
            view_->setOutlineMask(outl);
            // bulk: limit-to-region disabled, region undefined
            view_->enterThresholdMode(thrAddOn_, thrAddVal_,
                                      thrRemoveOn_, thrRemoveVal_, thrFinalOn_);
            view_->commitThreshold();
            view_->saveOutline(outPath);
            ++processed;
        }
        progress.setValue(sources.size());
    }
    view_->setUpdatesEnabled(true);

    if (savedIdx >= 0) loadProjectIndex(savedIdx);

    QMessageBox::information(this, "Bulk threshold",
        QString("Processed %1 files.\nBackup: %2").arg(processed).arg(snap));
}

void CannyMainWindow::scanProject()
{
    fileList_.clear();
    fileIndex_ = -1;
    const bool haveProject = !projectPath_.isEmpty() && project_.isValid();
    if (haveProject) {
        QDir d(project_.sourceDir);
        fileList_ = d.entryList(QStringList{"*.png"}, QDir::Files, QDir::Name);
    }
    const bool en = haveProject && !fileList_.isEmpty();
    aPrev_->setEnabled(en);
    aNext_->setEnabled(en);
    aFirstEmpty_->setEnabled(en);
    fileSpin_->setEnabled(en);
    guard_ = true;
    fileSpin_->setRange(fileList_.isEmpty() ? 0 : 1, std::max(1, int(fileList_.size())));
    fileSpin_->setValue(fileList_.isEmpty() ? 0 : 1);
    guard_ = false;
    if (fileList_.isEmpty()) {
        fileLabel_->setText(project_.isValid() ? "(no .png files in source)"
                                               : "(no project)");
        return;
    }
    loadProjectIndex(0);
}

bool CannyMainWindow::loadProjectIndex(int idx)
{
    if (idx < 0 || idx >= fileList_.size()) return false;
    fileIndex_ = idx;
    const QString name = fileList_[idx];
    const QString path = QDir(project_.sourceDir).filePath(name);
    if (!loadFile(path)) {
        QMessageBox::warning(this, "Open", "Failed to load: " + path);
        return false;
    }
    updateFileLabel();
    return true;
}

void CannyMainWindow::updateFileLabel()
{
    if (fileIndex_ < 0 || fileList_.isEmpty()) {
        fileLabel_->setText("(no project)");
        return;
    }
    fileLabel_->setText(QString("%1 / %2   %3")
                        .arg(fileIndex_ + 1)
                        .arg(fileList_.size())
                        .arg(fileList_[fileIndex_]));
    guard_ = true;
    fileSpin_->setValue(fileIndex_ + 1);
    guard_ = false;
}

void CannyMainWindow::onPrevFile()
{
    if (fileList_.isEmpty()) return;
    if (fileIndex_ <= 0) return;
    if (dirty_ && !doSave()) return;
    loadProjectIndex(fileIndex_ - 1);
}

void CannyMainWindow::onNextFile()
{
    if (fileList_.isEmpty()) return;
    if (fileIndex_ >= fileList_.size() - 1) return;
    if (dirty_ && !doSave()) return;
    loadProjectIndex(fileIndex_ + 1);
}

void CannyMainWindow::onFirstEmpty()
{
    if (fileList_.isEmpty() || !project_.isValid()) return;
    const QDir outDir(project_.outputDir);
    int target = -1;
    for (int i = 0; i < fileList_.size(); ++i) {
        if (!QFileInfo::exists(outDir.filePath(fileList_[i]))) { target = i; break; }
    }
    if (target < 0) {
        statusBar()->showMessage("All files have an outline", 3000);
        return;
    }
    if (target == fileIndex_) return;
    if (dirty_ && !doSave()) return;
    loadProjectIndex(target);
}