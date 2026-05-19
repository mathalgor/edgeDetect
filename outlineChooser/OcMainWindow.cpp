#include "OcMainWindow.h"
#include "OcViewWidget.h"
#include "ProjectDialog.h"
#include "CountSnapSpinBox.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <opencv2/imgcodecs.hpp>

#include "OriginalLoader.h"
#include "ProjectTimeDialog.h"
#include "ViewPreset.h"

OcMainWindow::OcMainWindow(QWidget* parent) : QMainWindow(parent)
{
    createUi();
    setWindowTitle("outlineChooser");
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

void OcMainWindow::createUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);

    view_ = new OcViewWidget(central);
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
    fileSpin_->setToolTip("Jump to image number");
    tb->addWidget(fileSpin_);
    tb->addSeparator();
    auto* aFit = tb->addAction("Fit");
    auto* aOne = tb->addAction("1:1");
    tb->addSeparator();
    conn8Cb_ = new QCheckBox("8-conn");
    conn8Cb_->setChecked(true);
    conn8Cb_->setToolTip("Connectivity for segment flood-fill");
    tb->addWidget(conn8Cb_);
    tb->addSeparator();
    tb->addWidget(new QLabel("View:"));
    presetCb_ = new QComboBox();
    presetCb_->setToolTip("View preset — press 1..N or Tab to swap with previous");
    for (const auto& p : view_->presets()) presetCb_->addItem(p.name);
    presetCb_->setCurrentIndex(view_->presetIndex());
    tb->addWidget(presetCb_);

    // Expanding spacer pushes Done button to the right edge.
    auto* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    doneBtn_ = new QPushButton("Done");
    doneBtn_->setCheckable(true);
    doneBtn_->setToolTip("Mark this file as done (click to toggle)");
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
    auto* aExport = mTools->addAction("&Export to dataset...");
    connect(aExport, &QAction::triggered, this, &OcMainWindow::onExportToDataset);

    fileLabel_ = new QLabel("(no project)");
    statusBar()->addWidget(fileLabel_);
    timeLabel_ = new QLabel();
    timeLabel_->setToolTip("Active time spent on this file");
    statusBar()->addPermanentWidget(timeLabel_);
    hudLabel_ = new QLabel();
    statusBar()->addPermanentWidget(hudLabel_);

    connect(aNewProj,  &QAction::triggered, this, &OcMainWindow::onNewProject);
    connect(aOpenProj, &QAction::triggered, this, &OcMainWindow::onOpenProject);
    connect(aSetProj,  &QAction::triggered, this, &OcMainWindow::onSetProject);
    connect(aSave,     &QAction::triggered, this, &OcMainWindow::onSave);
    connect(aQuit,     &QAction::triggered, this, &QMainWindow::close);
    connect(aPrev_,    &QAction::triggered, this, &OcMainWindow::onPrevFile);
    connect(aNext_,    &QAction::triggered, this, &OcMainWindow::onNextFile);
    connect(aFirstNotDone_, &QAction::triggered, this, &OcMainWindow::onFirstNotDone);
    connect(aFit,      &QAction::triggered, this, &OcMainWindow::onFit);
    connect(aOne,      &QAction::triggered, this, &OcMainWindow::onOneToOne);
    connect(conn8Cb_,  &QCheckBox::toggled, this, &OcMainWindow::onConn8Toggled);

    connect(fileSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
        if (guard_) return;
        const int idx = v - 1;
        if (idx < 0 || idx >= fileList_.size()) return;
        if (idx == fileIndex_) return;
        autoSaveIfPossible();
        loadProjectIndex(idx);
    });

    connect(view_, &OcViewWidget::hudUpdate, this, &OcMainWindow::onHud);
    connect(view_, &OcViewWidget::dirtyChanged, this, &OcMainWindow::onDirtyChanged);
    connect(view_, &OcViewWidget::editOp, this, &OcMainWindow::onEditOp);
    connect(aUndo_, &QAction::triggered, this, &OcMainWindow::onUndo);
    connect(aRedo_, &QAction::triggered, this, &OcMainWindow::onRedo);
    connect(view_, &OcViewWidget::rectSelectionFinished,
            this, &OcMainWindow::onRectSelectionFinished);

    connect(presetCb_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) { view_->setPresetIndex(i); });
    connect(view_, &OcViewWidget::presetChanged, this, [this](int i) {
        if (presetCb_->currentIndex() != i) {
            QSignalBlocker b(presetCb_);
            presetCb_->setCurrentIndex(i);
        }
    });

    // Digit shortcuts 1..N for presets, Tab to swap with previous.
    const int nPresets = int(view_->presets().size());
    for (int i = 0; i < nPresets && i < 9; ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::Key_1 + i), this);
        connect(sc, &QShortcut::activated, this, [this, i]{ view_->setPresetIndex(i); });
    }
    auto* scTab = new QShortcut(QKeySequence(Qt::Key_Tab), this);
    scTab->setContext(Qt::ApplicationShortcut);
    connect(scTab, &QShortcut::activated, this, [this]{ view_->swapWithPrevPreset(); });

    // Time tracking: register activity from key/mouse-button/wheel events.
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

    connect(view_, &OcViewWidget::editBlocked, this, [this]() {
        QMessageBox::information(this, "Editing disabled",
            "This file is marked Done. Disable the Done toggle to edit.");
    });
}

void OcMainWindow::updateDoneButton(bool done)
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

void OcMainWindow::onRectSelectionFinished()
{
    if (rectDialog_) { rectDialog_->close(); rectDialog_ = nullptr; }

    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlags(dlg->windowFlags() | Qt::Tool | Qt::WindowStaysOnTopHint);
    dlg->setWindowTitle("Rect threshold");
    dlg->setModal(false);

    auto* sb = new CountSnapSpinBox(dlg,
        [v = view_](int x){ return v->previewAddCountIf(x); });
    sb->setRange(0, 255);
    sb->setValue(lastRectThreshold_);
    sb->setAccelerated(true);
    sb->setToolTip("Components with src gray ≤ threshold are eligible. "
                   "Stepping jumps to the next value where the count changes.");

    auto* modeCb = new QComboBox(dlg);
    modeCb->addItem("Touching (components partially)", 0);
    modeCb->addItem("Inside (components fully)",       1);
    if (lastCandidateMode_ < modeCb->count())
        modeCb->setCurrentIndex(lastCandidateMode_);

    auto* colorCb = new QComboBox(dlg);
    colorCb->addItem("Red (only outline 2)",   int(OcViewWidget::CandColor::Red));
    colorCb->addItem("Green (only outline 1)", int(OcViewWidget::CandColor::Green));
    colorCb->addItem("Gray (no outline)",      int(OcViewWidget::CandColor::Gray));
    {
        const int idx = colorCb->findData(lastCandidateColor_);
        colorCb->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    auto* countsLbl = new QLabel(dlg);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);

    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(new QLabel("≤ threshold → eligible components"));
    lay->addWidget(sb);
    lay->addWidget(new QLabel("Spatial:"));
    lay->addWidget(modeCb);
    lay->addWidget(new QLabel("Add color:"));
    lay->addWidget(colorCb);
    lay->addWidget(countsLbl);
    lay->addWidget(bb);

    auto pushPreview = [this, sb, modeCb, colorCb, countsLbl]() {
        const auto mode  = (modeCb->currentIndex() == 0)
            ? OcViewWidget::CandMode::Touching
            : OcViewWidget::CandMode::Inside;
        const auto color = static_cast<OcViewWidget::CandColor>(
            colorCb->currentData().toInt());
        view_->setRectPreview(sb->value(), mode, color);
        const QLocale loc = QLocale::system();
        const int blue   = view_->previewAddCountIf(sb->value());
        const int yellow = view_->previewRejectCountIf(sb->value());
        countsLbl->setText(
            QString("adding %1 px,  past threshold %2 px")
                .arg(loc.toString(blue)).arg(loc.toString(yellow)));
    };
    // Initial preview with the current defaults.
    pushPreview();
    connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [pushPreview](int){ pushPreview(); });
    connect(modeCb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [pushPreview](int){ pushPreview(); });
    connect(colorCb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [pushPreview](int){ pushPreview(); });

    connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(dlg, &QDialog::accepted, this, [this, sb, modeCb, colorCb]() {
        lastRectThreshold_  = sb->value();
        lastCandidateMode_  = modeCb->currentIndex();
        lastCandidateColor_ = colorCb->currentData().toInt();
        const auto mode  = (lastCandidateMode_ == 0)
            ? OcViewWidget::CandMode::Touching
            : OcViewWidget::CandMode::Inside;
        const auto color = static_cast<OcViewWidget::CandColor>(lastCandidateColor_);
        view_->commitRectSelection(sb->value(), mode, color);
        rectDialog_ = nullptr;
    });
    connect(dlg, &QDialog::rejected, this, [this]() {
        view_->cancelRectSelection();
        rectDialog_ = nullptr;
    });

    rectDialog_ = dlg;
    dlg->adjustSize();
    dlg->show();
    sb->setFocus();
}

void OcMainWindow::onEditOp(std::vector<cv::Point> pts, bool add)
{
    undoStack_.push_back({ std::move(pts), add });
    redoStack_.clear();
    updateUndoActions();
}

void OcMainWindow::onUndo()
{
    if (undoStack_.isEmpty()) return;
    const EditOp op = undoStack_.takeLast();
    view_->applyOp(op.pts, !op.add);
    redoStack_.push_back(op);
    updateUndoActions();
}

void OcMainWindow::onRedo()
{
    if (redoStack_.isEmpty()) return;
    const EditOp op = redoStack_.takeLast();
    view_->applyOp(op.pts, op.add);
    undoStack_.push_back(op);
    updateUndoActions();
}

void OcMainWindow::clearUndoStacks()
{
    undoStack_.clear();
    redoStack_.clear();
    updateUndoActions();
}

void OcMainWindow::updateUndoActions()
{
    if (aUndo_) aUndo_->setEnabled(!undoStack_.isEmpty());
    if (aRedo_) aRedo_->setEnabled(!redoStack_.isEmpty());
}

bool OcMainWindow::eventFilter(QObject* obj, QEvent* e)
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

void OcMainWindow::rebuildRecentMenu()
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

bool OcMainWindow::loadProjectFromPath(const QString& path)
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

bool OcMainWindow::createNewProjectAt(const QString& path)
{
    ProjectDialog dlg(ProjectConfig(), this);
    if (dlg.exec() != QDialog::Accepted) return false;
    const ProjectConfig nc = dlg.config();
    if (!nc.isValid()) {
        QMessageBox::warning(this, "New project",
            "Source, Outlines 1, Outlines 2 must point to existing directories, "
            "and Output must be set.");
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

void OcMainWindow::onNewProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, "New project", startDir,
        "Outline Chooser project (*.ocproj)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".ocproj", Qt::CaseInsensitive)) path += ".ocproj";
    createNewProjectAt(path);
}

void OcMainWindow::onOpenProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Open project", startDir,
        "Outline Chooser project (*.ocproj);;All files (*)");
    if (path.isEmpty()) return;
    loadProjectFromPath(path);
}

void OcMainWindow::onSetProject()
{
    if (projectPath_.isEmpty()) {
        QMessageBox::information(this, "Set project dirs",
            "First create or open a project (File → New/Open project).");
        return;
    }
    ProjectDialog dlg(project_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const ProjectConfig nc = dlg.config();
    if (!nc.isValid()) {
        QMessageBox::warning(this, "Set project dirs",
            "Source, Outlines 1 and Outlines 2 must point to existing directories.");
        return;
    }
    project_ = nc;
    project_.save(projectPath_);
    scanProject();
}

void OcMainWindow::scanProject()
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
    aFirstNotDone_->setEnabled(en);
    fileSpin_->setEnabled(en);
    guard_ = true;
    fileSpin_->setRange(fileList_.isEmpty() ? 0 : 1,
                        std::max(1, int(fileList_.size())));
    fileSpin_->setValue(fileList_.isEmpty() ? 0 : 1);
    guard_ = false;
    if (fileList_.isEmpty()) {
        fileLabel_->setText(haveProject ? "(no .png files in source)"
                                        : "(no project)");
        return;
    }
    loadProjectIndex(0);
}

static cv::Mat readGray(const QString& path)
{
    if (!QFileInfo::exists(path)) return {};
    return cv::imread(path.toStdString(), cv::IMREAD_GRAYSCALE);
}

bool OcMainWindow::loadProjectIndex(int idx)
{
    if (idx < 0 || idx >= fileList_.size()) return false;
    autoSaveIfPossible();
    const QString name = fileList_[idx];
    const QString src = QDir(project_.sourceDir).filePath(name);
    cv::Mat g = readGray(src);
    if (g.empty()) {
        QMessageBox::warning(this, "Open", "Failed to load: " + src);
        return false;
    }
    cv::Mat o1 = readGray(QDir(project_.outlines1Dir).filePath(name));
    cv::Mat o2 = readGray(QDir(project_.outlines2Dir).filePath(name));
    cv::Mat existingOut;
    if (!project_.outputDir.isEmpty()) {
        existingOut = readGray(QDir(project_.outputDir).filePath(name));
    }
    cv::Mat original;
    if (!project_.originalDir.isEmpty()) {
        const QString stem = QFileInfo(name).completeBaseName();
        original = loadOriginalForStem(project_.originalDir, stem);
    }
    view_->setData(g, o1, o2, existingOut, original);
    fileIndex_ = idx;
    currentPath_ = src;
    clearUndoStacks();
    tracker_.setCurrentFile(name);
    const bool done = tracker_.isDone(name);
    updateDoneButton(done);
    view_->setEditLocked(done);
    timeLabel_->setText("Time: " + TimeTracker::formatHMS(tracker_.secondsFor(name)));
    updateFileLabel();
    updateTitle();
    return true;
}

bool OcMainWindow::doSave()
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

void OcMainWindow::autoSaveIfPossible()
{
    if (!view_->dirty()) return;
    if (!doSave()) {
        statusBar()->showMessage("Auto-save failed.", 5000);
    }
}

void OcMainWindow::onSave()
{
    if (!view_->dirty()) {
        statusBar()->showMessage("Nothing to save.", 2000);
        return;
    }
    if (doSave()) statusBar()->showMessage("Saved.", 2000);
    else QMessageBox::warning(this, "Save", "Save failed.");
}

void OcMainWindow::onPrevFile()
{
    if (fileIndex_ <= 0) return;
    loadProjectIndex(fileIndex_ - 1);
}

void OcMainWindow::onNextFile()
{
    if (fileIndex_ < 0 || fileIndex_ >= fileList_.size() - 1) return;
    loadProjectIndex(fileIndex_ + 1);
}

void OcMainWindow::onFirstNotDone()
{
    if (fileList_.isEmpty()) return;
    int target = -1;
    for (int i = 0; i < fileList_.size(); ++i) {
        if (!tracker_.isDone(fileList_[i])) { target = i; break; }
    }
    if (target < 0) {
        statusBar()->showMessage("All files are marked Done.", 3000);
        return;
    }
    if (target == fileIndex_) return;
    autoSaveIfPossible();
    loadProjectIndex(target);
}

void OcMainWindow::onExportToDataset()
{
    if (fileList_.isEmpty()) {
        QMessageBox::information(this, "Export to dataset",
            "No files in the project.");
        return;
    }
    if (project_.originalDir.isEmpty() || project_.outputDir.isEmpty()) {
        QMessageBox::warning(this, "Export to dataset",
            "Project must have both originalDir and outputDir configured.");
        return;
    }

    // -------- dialog --------
    QDialog dlg(this);
    dlg.setWindowTitle("Export to dataset");

    auto* pathEdit = new QLineEdit(&dlg);
    pathEdit->setText(appConfig_.lastDatasetRoot);
    pathEdit->setPlaceholderText("dataset root (will be created if missing)");
    pathEdit->setMinimumWidth(400);

    auto* browseBtn = new QPushButton(&dlg);
    browseBtn->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    browseBtn->setToolTip("Choose folder…");
    connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString start = pathEdit->text().isEmpty()
            ? QDir::homePath() : pathEdit->text();
        const QString d = QFileDialog::getExistingDirectory(
            &dlg, "Dataset root", start);
        if (!d.isEmpty()) pathEdit->setText(d);
    });

    auto* splitCb = new QComboBox(&dlg);
    splitCb->addItem("train");
    splitCb->addItem("test");
    if (appConfig_.lastDatasetSplit == "test") splitCb->setCurrentIndex(1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* form = new QFormLayout;
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseBtn);
    auto* pathRowWidget = new QWidget(&dlg);
    pathRowWidget->setLayout(pathRow);
    form->addRow("Dataset root:", pathRowWidget);
    form->addRow("Split:", splitCb);

    auto* lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString root  = pathEdit->text().trimmed();
    const QString split = splitCb->currentText();
    if (root.isEmpty()) return;

    appConfig_.lastDatasetRoot  = root;
    appConfig_.lastDatasetSplit = split;
    appConfig_.save();

    // -------- target dirs --------
    const QString rgbDir = QDir(root).filePath(split + "/rgb");
    const QString maskDir = (split == "train")
        ? QDir(root).filePath("train/mask/real")
        : QDir(root).filePath("test/mask");
    QDir().mkpath(rgbDir);
    QDir().mkpath(maskDir);

    // -------- iterate Done files --------
    int doneCount = 0, copiedRgb = 0, copiedMask = 0;
    int skipNoOrig = 0, skipNoMask = 0;
    bool skipAllMissing = false;
    bool aborted = false;

    auto handleMissing = [&](const QString& what, const QString& name) -> int {
        // returns: 0 = skip this, 1 = abort
        if (skipAllMissing) return 0;
        QMessageBox box(QMessageBox::Warning, "Missing " + what,
            QString("Missing %1 for '%2'.\n\nSkip this file, skip all missing, or abort?")
                .arg(what, name),
            QMessageBox::NoButton, this);
        auto* skipBtn = box.addButton("Skip", QMessageBox::AcceptRole);
        auto* skipAllBtn = box.addButton("Skip all missing", QMessageBox::AcceptRole);
        auto* abortBtn = box.addButton("Abort", QMessageBox::RejectRole);
        box.setDefaultButton(skipBtn);
        box.exec();
        if (box.clickedButton() == abortBtn) return 1;
        if (box.clickedButton() == skipAllBtn) skipAllMissing = true;
        return 0;
    };

    for (const QString& name : fileList_) {
        if (!tracker_.isDone(name)) continue;
        ++doneCount;

        const QString stem = QFileInfo(name).completeBaseName();
        const QString origPath = findOriginalPathForStem(project_.originalDir, stem);
        const QString maskPath = QDir(project_.outputDir).filePath(name);
        const bool hasOrig = !origPath.isEmpty() && QFileInfo::exists(origPath);
        const bool hasMask = QFileInfo::exists(maskPath);

        if (!hasOrig) {
            ++skipNoOrig;
            if (handleMissing("original RGB", name) == 1) { aborted = true; break; }
            continue;
        }
        if (!hasMask) {
            ++skipNoMask;
            if (handleMissing("outline mask", name) == 1) { aborted = true; break; }
            continue;
        }

        const QString rgbDst = QDir(rgbDir).filePath(QFileInfo(origPath).fileName());
        const QString maskDst = QDir(maskDir).filePath(name);
        QFile::remove(rgbDst);
        QFile::remove(maskDst);
        if (QFile::copy(origPath, rgbDst))  ++copiedRgb;
        if (QFile::copy(maskPath, maskDst)) ++copiedMask;
    }

    QString msg;
    msg += QString("Exported %1 RGB + %2 masks (of %3 Done files).\n")
        .arg(copiedRgb).arg(copiedMask).arg(doneCount);
    msg += QString("Skipped: %1 (no original), %2 (no mask).")
        .arg(skipNoOrig).arg(skipNoMask);
    if (aborted) msg += "\n\nExport aborted by user.";
    QMessageBox::information(this, "Export to dataset", msg);
}

void OcMainWindow::onFit()      { view_->fitToWindow(); }
void OcMainWindow::onOneToOne() { view_->zoomOneToOne(); }
void OcMainWindow::onConn8Toggled(bool on) { view_->setConn8(on); }
void OcMainWindow::onHud(const QString& s) { hudLabel_->setText(s); }

void OcMainWindow::onDirtyChanged(bool)
{
    updateTitle();
}

void OcMainWindow::updateFileLabel()
{
    if (fileList_.isEmpty()) { fileLabel_->setText("(none)"); return; }
    fileLabel_->setText(QString("%1 / %2  %3")
        .arg(fileIndex_ + 1).arg(fileList_.size()).arg(fileList_[fileIndex_]));
    guard_ = true;
    fileSpin_->setValue(fileIndex_ + 1);
    guard_ = false;
}

void OcMainWindow::updateTitle()
{
    const QString proj = projectPath_.isEmpty()
        ? QString() : QFileInfo(projectPath_).completeBaseName();
    const QString dirty = view_->dirty() ? " *" : "";
    if (currentPath_.isEmpty()) {
        setWindowTitle(proj.isEmpty()
            ? "outlineChooser"
            : QString("outlineChooser — [%1]").arg(proj));
    } else {
        setWindowTitle(QString("outlineChooser — %1%2%3")
            .arg(currentPath_, dirty,
                 proj.isEmpty() ? QString() : QString("  [%1]").arg(proj)));
    }
}

void OcMainWindow::closeEvent(QCloseEvent* e)
{
    autoSaveIfPossible();
    tracker_.flush();
    appConfig_.save();
    QMainWindow::closeEvent(e);
}