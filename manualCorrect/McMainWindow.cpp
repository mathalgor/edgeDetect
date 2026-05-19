#include "McMainWindow.h"
#include "McViewWidget.h"
#include "ProjectDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <opencv2/imgcodecs.hpp>

#include "OriginalLoader.h"
#include "ProjectTimeDialog.h"

McMainWindow::McMainWindow(QWidget* parent) : QMainWindow(parent)
{
    createUi();
    setWindowTitle("manualCorrect");
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

void McMainWindow::createUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);

    view_ = new McViewWidget(central);
    root->addWidget(view_, 1);
    setCentralWidget(central);

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
    aFirstNotDone_ = tb->addAction("▶?");
    aFirstNotDone_->setToolTip("Go to first file not marked Done");
    aPrev_->setShortcut(QKeySequence(Qt::Key_PageUp));
    aNext_->setShortcut(QKeySequence(Qt::Key_PageDown));
    aFirstNotDone_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    aPrev_->setEnabled(false);
    aNext_->setEnabled(false);
    aFirstNotDone_->setEnabled(false);
    fileSpin_ = new QSpinBox();
    fileSpin_->setRange(0, 0);
    fileSpin_->setKeyboardTracking(false);
    fileSpin_->setEnabled(false);
    tb->addWidget(fileSpin_);
    tb->addSeparator();
    auto* aFit = tb->addAction("Fit");
    auto* aOne = tb->addAction("1:1");
    tb->addSeparator();
    conn8Cb_ = new QCheckBox("8-conn");
    conn8Cb_->setChecked(true);
    conn8Cb_->setToolTip("Connectivity for same-value segment build");
    tb->addWidget(conn8Cb_);
    tb->addSeparator();
    tb->addWidget(new QLabel("View:"));
    presetCb_ = new QComboBox();
    presetCb_->setToolTip("View preset — press 1..N to switch");
    for (const auto& pr : view_->presets()) presetCb_->addItem(pr.name);
    presetCb_->setCurrentIndex(view_->presetIndex());
    tb->addWidget(presetCb_);

    auto* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    doneBtn_ = new QPushButton("Done");
    doneBtn_->setCheckable(true);
    updateDoneButton(false);
    tb->addWidget(doneBtn_);

    auto* mFile = menuBar()->addMenu("&File");
    auto* aNewProj  = mFile->addAction("&New project...");
    auto* aOpenProj = mFile->addAction("&Open project...");
    recentMenu_ = mFile->addMenu("&Recent projects");
    auto* aSetProj = mFile->addAction("&Set project dirs...");
    mFile->addSeparator();
    auto* aSave = mFile->addAction("&Save");
    aSave->setShortcut(QKeySequence::Save);
    mFile->addSeparator();
    mFile->addAction(aPrev_);
    mFile->addAction(aNext_);
    mFile->addSeparator();
    auto* aQuit = mFile->addAction("&Quit");
    aQuit->setShortcut(QKeySequence::Quit);

    auto* mEdit = menuBar()->addMenu("&Edit");
    mEdit->addAction(aUndo_);
    mEdit->addAction(aRedo_);

    auto* mView = menuBar()->addMenu("&View");
    mView->addAction(aFit);
    mView->addAction(aOne);

    auto* mTools = menuBar()->addMenu("&Tools");
    auto* aProjTime = mTools->addAction("&Project time...");
    connect(aProjTime, &QAction::triggered, this, [this]{
        ProjectTimeDialog dlg(&tracker_, fileList_, this);
        dlg.exec();
    });

    fileLabel_ = new QLabel("(no project)");
    statusBar()->addWidget(fileLabel_);
    timeLabel_ = new QLabel();
    statusBar()->addPermanentWidget(timeLabel_);
    hudLabel_ = new QLabel();
    statusBar()->addPermanentWidget(hudLabel_);

    connect(aNewProj,  &QAction::triggered, this, &McMainWindow::onNewProject);
    connect(aOpenProj, &QAction::triggered, this, &McMainWindow::onOpenProject);
    connect(aSetProj,  &QAction::triggered, this, &McMainWindow::onSetProject);
    connect(aSave,     &QAction::triggered, this, &McMainWindow::onSave);
    connect(aQuit,     &QAction::triggered, this, &QMainWindow::close);
    connect(aPrev_,    &QAction::triggered, this, &McMainWindow::onPrevFile);
    connect(aNext_,    &QAction::triggered, this, &McMainWindow::onNextFile);
    connect(aFirstNotDone_, &QAction::triggered, this, &McMainWindow::onFirstNotDone);
    connect(aFit,      &QAction::triggered, this, &McMainWindow::onFit);
    connect(aOne,      &QAction::triggered, this, &McMainWindow::onOneToOne);
    connect(conn8Cb_,  &QCheckBox::toggled, this, &McMainWindow::onConn8Toggled);
    connect(presetCb_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &McMainWindow::onPresetChanged);

    connect(fileSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
        if (guard_) return;
        const int idx = v - 1;
        if (idx < 0 || idx >= fileList_.size()) return;
        if (idx == fileIndex_) return;
        autoSaveIfPossible();
        loadProjectIndex(idx);
    });

    connect(view_, &McViewWidget::hudUpdate,        this, &McMainWindow::onHud);
    connect(view_, &McViewWidget::dirtyChanged,     this, &McMainWindow::onDirtyChanged);
    connect(view_, &McViewWidget::editOp,           this, &McMainWindow::onEditOp);
    connect(view_, &McViewWidget::polygonFinished,  this, &McMainWindow::onPolygonFinished);
    connect(aUndo_, &QAction::triggered, this, &McMainWindow::onUndo);
    connect(aRedo_, &QAction::triggered, this, &McMainWindow::onRedo);

    for (int i = 0; i < presetCb_->count() && i < 9; ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::Key_1 + i), this);
        connect(sc, &QShortcut::activated, this,
                [this, i]{ presetCb_->setCurrentIndex(i); });
    }

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
        view_->setEditLocked(on);
    });
    connect(view_, &McViewWidget::editBlocked, this, [this]() {
        QMessageBox::information(this, "Editing disabled",
            "This file is marked Done. Disable the Done toggle to edit.");
    });
}

void McMainWindow::updateDoneButton(bool done)
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

bool McMainWindow::eventFilter(QObject* obj, QEvent* e)
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

void McMainWindow::closeEvent(QCloseEvent* e)
{
    autoSaveIfPossible();
    tracker_.flush();
    e->accept();
}

void McMainWindow::rebuildRecentMenu()
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
    connect(clr, &QAction::triggered, this, [this] {
        appConfig_.recentProjects.clear();
        appConfig_.save();
        rebuildRecentMenu();
    });
}

bool McMainWindow::loadProjectFromPath(const QString& path)
{
    ProjectConfig nc;
    if (!nc.load(path)) {
        QMessageBox::warning(this, "Open project",
            QString("Failed to read project file:\n%1").arg(path));
        return false;
    }
    const QString err = nc.validationError();
    if (!err.isEmpty()) {
        QMessageBox::warning(this, "Open project",
            QString("Project %1 is invalid:\n%2").arg(path, err));
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

bool McMainWindow::createNewProjectAt(const QString& path)
{
    ProjectDialog dlg(ProjectConfig(), this);
    if (dlg.exec() != QDialog::Accepted) return false;
    ProjectConfig nc = dlg.config();
    const QString err = nc.validationError();
    if (!err.isEmpty()) {
        QMessageBox::warning(this, "New project", err);
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
    tracker_.bindToProject(QFileInfo(path).absoluteFilePath());
    scanProject();
    return true;
}

void McMainWindow::onNewProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, "New project", startDir,
        "Manual Correct project (*.mcproj)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".mcproj", Qt::CaseInsensitive)) path += ".mcproj";
    createNewProjectAt(path);
}

void McMainWindow::onOpenProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Open project", startDir,
        "Manual Correct project (*.mcproj);;All files (*)");
    if (path.isEmpty()) return;
    loadProjectFromPath(path);
}

void McMainWindow::onSetProject()
{
    if (projectPath_.isEmpty()) {
        QMessageBox::information(this, "Set project dirs",
            "First create or open a project (File → New/Open project).");
        return;
    }
    ProjectDialog dlg(project_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    ProjectConfig nc = dlg.config();
    const QString err = nc.validationError();
    if (!err.isEmpty()) {
        QMessageBox::warning(this, "Set project dirs", err);
        return;
    }
    project_ = nc;
    project_.save(projectPath_);
    scanProject();
}

void McMainWindow::scanProject()
{
    fileList_.clear();
    fileIndex_ = -1;
    const bool haveProject = !projectPath_.isEmpty() && project_.isValid();
    if (haveProject) {
        QDir d(project_.outlinesDir);
        const auto all = d.entryList(QStringList{"*.png"}, QDir::Files, QDir::Name);
        for (const QString& n : all) {
            if (!n.endsWith(".dbgrg.png", Qt::CaseInsensitive))
                fileList_.append(n);
        }
    }
    const bool en = haveProject && !fileList_.isEmpty();
    aPrev_->setEnabled(en);
    aNext_->setEnabled(en);
    aFirstNotDone_->setEnabled(en);
    fileSpin_->setEnabled(en);
    guard_ = true;
    fileSpin_->setRange(fileList_.isEmpty() ? 0 : 1,
                        std::max(1, int(fileList_.size())));
    fileSpin_->setValue(fileList_.isEmpty() ? 0 : 1);
    guard_ = false;
    if (fileList_.isEmpty()) {
        fileLabel_->setText(haveProject ? "(no outlines)" : "(no project)");
        return;
    }
    loadProjectIndex(0);
}

bool McMainWindow::loadProjectIndex(int idx)
{
    if (idx < 0 || idx >= fileList_.size()) return false;
    autoSaveIfPossible();
    const QString name = fileList_[idx];
    const QString outlinesPath = QDir(project_.outlinesDir).filePath(name);
    const QString stem = QFileInfo(name).completeBaseName();
    const QString dbgrgPath = QDir(project_.outlinesDir).filePath(stem + ".dbgrg.png");

    cv::Mat outline = cv::imread(outlinesPath.toStdString(), cv::IMREAD_GRAYSCALE);
    if (outline.empty()) {
        QMessageBox::warning(this, "Open", "Failed to load outline: " + outlinesPath);
        return false;
    }
    cv::Mat dbgrg = cv::imread(dbgrgPath.toStdString(), cv::IMREAD_COLOR);
    if (dbgrg.empty()) {
        QMessageBox::warning(this, "Open",
            "Failed to load .dbgrg.png next to outline:\n" + dbgrgPath);
        return false;
    }
    cv::Mat existingOut;
    if (!project_.outputDir.isEmpty()) {
        const QString op = QDir(project_.outputDir).filePath(name);
        if (QFileInfo::exists(op))
            existingOut = cv::imread(op.toStdString(), cv::IMREAD_GRAYSCALE);
    }
    cv::Mat original;
    if (!project_.originalDir.isEmpty())
        original = loadOriginalForStem(project_.originalDir, stem);

    view_->setData(outline, dbgrg, original, existingOut);
    fileIndex_ = idx;
    currentPath_ = outlinesPath;
    clearUndoStacks();
    tracker_.setCurrentFile(name);
    const bool done = tracker_.isDone(name);
    updateDoneButton(done);
    view_->setEditLocked(done);
    timeLabel_->setText("Time: " + TimeTracker::formatHMS(tracker_.secondsFor(name)));
    updateFileLabel();
    updateTitle();
    guard_ = true;
    fileSpin_->setValue(idx + 1);
    guard_ = false;
    return true;
}

bool McMainWindow::doSave()
{
    if (currentPath_.isEmpty()) return false;
    if (project_.outputDir.isEmpty()) return false;
    QDir().mkpath(project_.outputDir);
    const QString name = QFileInfo(currentPath_).fileName();
    const QString outPath = QDir(project_.outputDir).filePath(name);
    const cv::Mat out = view_->outputFileFmt();
    if (out.empty()) return false;
    const bool ok = cv::imwrite(outPath.toStdString(), out,
        {cv::IMWRITE_PNG_COMPRESSION, 8, cv::IMWRITE_PNG_BILEVEL, 1});
    if (ok) view_->markSaved();
    return ok;
}

void McMainWindow::autoSaveIfPossible()
{
    if (!view_->dirty()) return;
    if (!doSave()) statusBar()->showMessage("Auto-save failed.", 5000);
}

void McMainWindow::onSave()
{
    if (!view_->dirty()) {
        statusBar()->showMessage("Nothing to save.", 2000);
        return;
    }
    if (doSave()) statusBar()->showMessage("Saved.", 2000);
    else QMessageBox::warning(this, "Save", "Save failed.");
}

void McMainWindow::onPrevFile()
{
    if (fileIndex_ <= 0) return;
    loadProjectIndex(fileIndex_ - 1);
}

void McMainWindow::onNextFile()
{
    if (fileIndex_ < 0 || fileIndex_ >= fileList_.size() - 1) return;
    loadProjectIndex(fileIndex_ + 1);
}

void McMainWindow::onFirstNotDone()
{
    if (fileList_.isEmpty()) return;
    int target = -1;
    for (int i = 0; i < fileList_.size(); ++i) {
        if (!tracker_.isDone(fileList_[i])) { target = i; break; }
    }
    if (target < 0) {
        statusBar()->showMessage("All files marked Done.", 3000);
        return;
    }
    if (target == fileIndex_) return;
    autoSaveIfPossible();
    loadProjectIndex(target);
}

void McMainWindow::onFit()      { view_->fitToWindow(); }
void McMainWindow::onOneToOne() { view_->zoomOneToOne(); }

void McMainWindow::onConn8Toggled(bool on) { view_->setConn8(on); }

void McMainWindow::onPresetChanged(int i)
{
    if (i < 0) return;
    view_->setPresetIndex(i);
}

void McMainWindow::onHud(const QString& s) { hudLabel_->setText(s); }

void McMainWindow::onDirtyChanged(bool) { updateTitle(); }

void McMainWindow::onEditOp(std::vector<cv::Point> pts, bool add)
{
    undoStack_.push_back({ std::move(pts), add });
    redoStack_.clear();
    updateUndoActions();
}

void McMainWindow::onUndo()
{
    if (undoStack_.isEmpty()) return;
    const EditOp op = undoStack_.takeLast();
    view_->applyOp(op.pts, !op.add);
    redoStack_.push_back(op);
    updateUndoActions();
}

void McMainWindow::onRedo()
{
    if (redoStack_.isEmpty()) return;
    const EditOp op = redoStack_.takeLast();
    view_->applyOp(op.pts, op.add);
    undoStack_.push_back(op);
    updateUndoActions();
}

void McMainWindow::clearUndoStacks()
{
    undoStack_.clear();
    redoStack_.clear();
    updateUndoActions();
}

void McMainWindow::updateUndoActions()
{
    if (aUndo_) aUndo_->setEnabled(!undoStack_.isEmpty());
    if (aRedo_) aRedo_->setEnabled(!redoStack_.isEmpty());
}

void McMainWindow::onPolygonFinished()
{
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlags(dlg->windowFlags() | Qt::Tool | Qt::WindowStaysOnTopHint);
    dlg->setWindowTitle("Polygon filter");
    dlg->setModal(false);

    auto* modeCb = new QComboBox(dlg);
    modeCb->addItem("Touching (segments overlapping)", 0);
    modeCb->addItem("Inside (segments fully inside)",  1);
    modeCb->setCurrentIndex(lastMode_);

    auto* actCb = new QComboBox(dlg);
    actCb->addItem("Remove from result", 0);
    actCb->addItem("Add to result",      1);
    actCb->setCurrentIndex(lastAction_);

    auto* gSb = new QSpinBox(dlg);
    gSb->setRange(0, 255); gSb->setValue(lastGMax_);
    gSb->setToolTip("Max segment G (edge gray) — strict ≤; lower keeps stronger edges only");

    auto* rSb = new QSpinBox(dlg);
    rSb->setRange(0, 255); rSb->setValue(lastRMax_);
    rSb->setToolTip("Max segment avg R (prob inverted) — strict ≤; lower keeps more-confident edges only");

    auto* countLbl = new QLabel(dlg);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);

    auto* lay = new QFormLayout(dlg);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addRow("Action:",         actCb);
    lay->addRow("Spatial:",        modeCb);
    lay->addRow("G ≤ (edge):",     gSb);
    lay->addRow("avg R ≤ (prob):", rSb);
    lay->addRow(countLbl);
    lay->addRow(bb);

    auto refresh = [this, modeCb, actCb, gSb, rSb, countLbl]() {
        const auto mode = (modeCb->currentIndex() == 0)
            ? McViewWidget::FilterMode::Touching
            : McViewWidget::FilterMode::Inside;
        const auto act = (actCb->currentIndex() == 0)
            ? McViewWidget::FilterAction::Remove
            : McViewWidget::FilterAction::Add;
        const int n = view_->setFilterPreview(mode, act, gSb->value(), rSb->value());
        const QLocale loc = QLocale::system();
        countLbl->setText(QString("would affect %1 px").arg(loc.toString(n)));
    };
    refresh();
    connect(modeCb, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
            [refresh](int){ refresh(); });
    connect(actCb,  QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
            [refresh](int){ refresh(); });
    connect(gSb,    QOverload<int>::of(&QSpinBox::valueChanged), dlg,
            [refresh](int){ refresh(); });
    connect(rSb,    QOverload<int>::of(&QSpinBox::valueChanged), dlg,
            [refresh](int){ refresh(); });

    connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(dlg, &QDialog::accepted, this,
            [this, modeCb, actCb, gSb, rSb]() {
        lastMode_   = modeCb->currentIndex();
        lastAction_ = actCb->currentIndex();
        lastGMax_   = gSb->value();
        lastRMax_   = rSb->value();
        const auto mode = (lastMode_ == 0)
            ? McViewWidget::FilterMode::Touching
            : McViewWidget::FilterMode::Inside;
        const auto act = (lastAction_ == 0)
            ? McViewWidget::FilterAction::Remove
            : McViewWidget::FilterAction::Add;
        view_->commitFilter(mode, act, lastGMax_, lastRMax_);
    });
    connect(dlg, &QDialog::rejected, this, [this]() { view_->cancelPolygon(); });

    dlg->adjustSize();
    dlg->show();
}

void McMainWindow::updateFileLabel()
{
    if (fileList_.isEmpty() || fileIndex_ < 0) {
        fileLabel_->setText("(no file)");
        return;
    }
    fileLabel_->setText(QString("[%1/%2] %3")
        .arg(fileIndex_ + 1).arg(fileList_.size()).arg(fileList_[fileIndex_]));
}

void McMainWindow::updateTitle()
{
    QString t = "manualCorrect";
    if (!projectPath_.isEmpty()) t += " — " + QFileInfo(projectPath_).fileName();
    if (view_ && view_->dirty()) t += " *";
    setWindowTitle(t);
}
