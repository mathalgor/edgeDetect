#include "AlignMainWindow.h"

#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QStatusBar>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QKeyEvent>
#include <QEvent>
#include <QSignalBlocker>
#include <QShortcut>
#include <QCheckBox>
#include <QComboBox>
#include <QMenuBar>
#include <QMenu>
#include <QTimer>
#include <QCloseEvent>
#include <QApplication>
#include <QStandardPaths>
#include <QMouseEvent>

#include "ProjectDialog.h"
#include "ProjectTimeDialog.h"
#include <QPushButton>
#include <QWheelEvent>
#include <QFileInfo>
#include <QInputDialog>
#include <cmath>
#include <cstring>
#include <limits>

AlignMainWindow::AlignMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    pins_.resize(pinCount_);
    createUi();

    qApp->installEventFilter(this);
    connect(&tracker_, &TimeTracker::tick, this,
            [this](const QString&, qint64 s) {
                if (timeLabel_)
                    timeLabel_->setText("Time: " + TimeTracker::formatHMS(s));
            });

    setWindowTitle("Align – gray + outline (dirs)");
    resize(1000, 700);

    appConfig_.load();
    rebuildRecentMenu();
    if (!appConfig_.currentProjectPath.isEmpty())
        loadProjectFromPath(appConfig_.currentProjectPath);
}


void AlignMainWindow::createUi()
{
    view_ = new AlignViewWidget(this);

    // MENU
    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* newProjAct  = fileMenu->addAction("&New project...");
    QAction* openProjAct = fileMenu->addAction("&Open project...");
    recentMenu_          = fileMenu->addMenu("&Recent projects");
    QAction* setProjAct  = fileMenu->addAction("&Set project dirs...");
    fileMenu->addSeparator();
    QAction* saveJsonAct = fileMenu->addAction("Save current");

    // MENU EDIT (undo/redo)
    QMenu* editMenu = menuBar()->addMenu("&Edit");

    undoAction_ = editMenu->addAction("Undo");
    undoAction_->setShortcut(QKeySequence::Undo);  // Ctrl+Z
    undoAction_->setEnabled(false);
    connect(undoAction_, &QAction::triggered, this, &AlignMainWindow::undo);

    redoAction_ = editMenu->addAction("Redo");
    redoAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));  // Ctrl+Shift+Z
    redoAction_->setEnabled(false);
    connect(redoAction_, &QAction::triggered, this, &AlignMainWindow::redo);

    restoreAction_ = editMenu->addAction("Restore");
    restoreAction_->setShortcut(QKeySequence("Ctrl+R"));
    restoreAction_->setEnabled(false);
    connect(restoreAction_, &QAction::triggered, this, &AlignMainWindow::restore);

    // MENU PINS
    QMenu* pinsMenu = menuBar()->addMenu("&Pins");
    restoreLastPinsAction_ = pinsMenu->addAction("Restore last");
    restoreLastPinsAction_->setEnabled(false);
    connect(restoreLastPinsAction_, &QAction::triggered, this, &AlignMainWindow::restoreLastPins);

    QAction* hidePinsAction = pinsMenu->addAction("Hide");
    connect(hidePinsAction, &QAction::triggered, this, &AlignMainWindow::hidePins);

    QAction* setPinCountAction = pinsMenu->addAction("Set pin count…");
    setPinCountAction->setToolTip("Number of pin slots (default 14, min 5). Current session only.");
    connect(setPinCountAction, &QAction::triggered, this, &AlignMainWindow::setPinCount);

    QAction* showResidualsAction = pinsMenu->addAction("Show residuals");
    showResidualsAction->setCheckable(true);
    showResidualsAction->setChecked(false);
    showResidualsAction->setToolTip(
        "Draws two segments from pin label: to outline position and to gray position "
        "(under current fit). When equal – single line as usual.");
    connect(showResidualsAction, &QAction::toggled, this, [this](bool on) {
        view_->setShowUncertainty(on);
    });

    // MENU TOOLS
    QMenu* toolsMenu = menuBar()->addMenu("&Tools");
    optimalModeAction_ = toolsMenu->addAction("Optimal mode");
    optimalModeAction_->setToolTip("Scans 16 combinations of X²/Y²/R/XY and selects the one with smallest RMS");
    optimalModeAction_->setEnabled(false);
    connect(optimalModeAction_, &QAction::triggered, this, &AlignMainWindow::runOptimalMode);

    toolsMenu->addSeparator();
    QAction* timingStatsAction = toolsMenu->addAction("Timing stats…");
    connect(timingStatsAction, &QAction::triggered, this, [this] {
        QStringList names;
        names.reserve(pairs_.size());
        for (const FilePair& p : pairs_)
            names << QFileInfo(p.outlinePath).fileName();
        ProjectTimeDialog dlg(&tracker_, names, this);
        dlg.exec();
    });

    // Timer for spinboxes – detecting end of edit
    spinboxTimer_ = new QTimer(this);
    spinboxTimer_->setSingleShot(true);
    spinboxTimer_->setInterval(2000);  // 2 sekundy
    connect(spinboxTimer_, &QTimer::timeout, this, &AlignMainWindow::onSpinboxEditTimeout);

    // TOOLBAR
    QToolBar* tb = addToolBar("Main");
    tb->addAction(saveJsonAct);

    connect(newProjAct,  &QAction::triggered, this, &AlignMainWindow::onNewProject);
    connect(openProjAct, &QAction::triggered, this, &AlignMainWindow::onOpenProject);
    connect(setProjAct,  &QAction::triggered, this, &AlignMainWindow::onSetProject);
    connect(saveJsonAct, &QAction::triggered, this, &AlignMainWindow::saveJsonlForCurrent);

    QAction* firstAct = tb->addAction("|<");
    QAction* prevAct  = tb->addAction("<");
    QAction* nextAct  = tb->addAction(">");
    QAction* lastAct  = tb->addAction(">|");
    QAction* firstUnalignedAct = tb->addAction("▶?");
    firstUnalignedAct->setToolTip("Go to first file not marked Done");

    connect(firstAct, &QAction::triggered, this, &AlignMainWindow::goFirst);
    connect(prevAct,  &QAction::triggered, this, &AlignMainWindow::goToPrev);
    connect(nextAct,  &QAction::triggered, this, &AlignMainWindow::goToNext);
    connect(lastAct,  &QAction::triggered, this, &AlignMainWindow::goLast);
    connect(firstUnalignedAct, &QAction::triggered, this, &AlignMainWindow::goFirstUnaligned);

    tb->addSeparator();
    tb->addWidget(new QLabel(" View: ", this));
    viewPresetCb_ = new QComboBox(this);
    viewPresetCb_->addItem("outline");        // 0
    viewPresetCb_->addItem("gray");           // 1
    viewPresetCb_->addItem("gray+outline");   // 2
    viewPresetCb_->setCurrentIndex(viewPresetIndex_);
    tb->addWidget(viewPresetCb_);
    connect(viewPresetCb_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i){
        if (i == viewPresetIndex_) return;
        prevViewPresetIndex_ = viewPresetIndex_;
        viewPresetIndex_ = i;
        applyViewPreset(i);
    });
    applyViewPreset(viewPresetIndex_);

    // Digit shortcuts 1/2/3 → presets; Tab → swap with previous preset
    for (int i = 0; i < 3; ++i) {
        QShortcut* sc = new QShortcut(QKeySequence(Qt::Key_1 + i), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, i]{
            viewPresetCb_->setCurrentIndex(i);
        });
    }
    {
        QShortcut* scTab = new QShortcut(QKeySequence(Qt::Key_Tab), this);
        scTab->setContext(Qt::ApplicationShortcut);
        connect(scTab, &QShortcut::activated, this, [this]{
            if (prevViewPresetIndex_ != viewPresetIndex_)
                viewPresetCb_->setCurrentIndex(prevViewPresetIndex_);
        });
    }

    // Spacer + Done button (top-right)
    {
        auto* spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        tb->addWidget(spacer);

        doneBtn_ = new QPushButton("Done", this);
        doneBtn_->setCheckable(true);
        updateDoneButton(false);
        tb->addWidget(doneBtn_);
        connect(doneBtn_, &QPushButton::toggled, this, [this](bool on) {
            if (currentIndex_ >= 0 && currentIndex_ < pairs_.size()) {
                const QString name = QFileInfo(pairs_[currentIndex_].outlinePath).fileName();
                tracker_.setDone(name, on);
            }
            updateDoneButton(on);
        });
    }

    timeLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(timeLabel_);

    // control panel
    QWidget* ctrlWidget = new QWidget(this);
    QHBoxLayout* ctrlLayout = new QHBoxLayout(ctrlWidget);

    // scaleX / scaleY (gray size in pixels)
    QLabel* sxLbl = new QLabel("scaleX:", ctrlWidget);
    scaleXSpin_ = new QSpinBox(ctrlWidget);
    scaleXSpin_->setRange(1, 5000);
    scaleXSpin_->setValue(100);

    QLabel* syLbl = new QLabel("scaleY:", ctrlWidget);
    scaleYSpin_ = new QSpinBox(ctrlWidget);
    scaleYSpin_->setRange(1, 5000);
    scaleYSpin_->setValue(100);

    connect(scaleXSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AlignMainWindow::onScaleXChanged);
    connect(scaleYSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AlignMainWindow::onScaleYChanged);

    // Delta X/Y (gray offset relative to outline)
    QLabel* dxLbl = new QLabel("deltaX:", ctrlWidget);
    deltaXSpin_ = new QSpinBox(ctrlWidget);
    deltaXSpin_->setRange(-2000, 2000);
    deltaXSpin_->setValue(0);

    QLabel* dyLbl = new QLabel("deltaY:", ctrlWidget);
    deltaYSpin_ = new QSpinBox(ctrlWidget);
    deltaYSpin_->setRange(-2000, 2000);
    deltaYSpin_->setValue(0);

    connect(deltaXSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AlignMainWindow::onDeltaXChanged);
    connect(deltaYSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AlignMainWindow::onDeltaYChanged);

    // Event filter on spinboxes – intercepts Ctrl+Z, Ctrl+Shift+Z, Esc
    scaleXSpin_->installEventFilter(this);
    scaleYSpin_->installEventFilter(this);
    deltaXSpin_->installEventFilter(this);
    deltaYSpin_->installEventFilter(this);

    ctrlLayout->addWidget(sxLbl);
    ctrlLayout->addWidget(scaleXSpin_);
    ctrlLayout->addSpacing(10);
    ctrlLayout->addWidget(syLbl);
    ctrlLayout->addWidget(scaleYSpin_);
    ctrlLayout->addSpacing(20);
    ctrlLayout->addWidget(dxLbl);
    ctrlLayout->addWidget(deltaXSpin_);
    ctrlLayout->addSpacing(10);
    ctrlLayout->addWidget(dyLbl);
    ctrlLayout->addWidget(deltaYSpin_);
    ctrlLayout->addSpacing(20);

    quadXCheck_ = new QCheckBox(QString::fromUtf8("X²"), ctrlWidget);
    quadXCheck_->setChecked(false);
    quadXCheck_->setToolTip("Quadratic term in X – recalculates last pins on every click");
    connect(quadXCheck_, &QCheckBox::toggled, this, &AlignMainWindow::onQuadXToggled);
    ctrlLayout->addWidget(quadXCheck_);

    quadYCheck_ = new QCheckBox(QString::fromUtf8("Y²"), ctrlWidget);
    quadYCheck_->setChecked(false);
    quadYCheck_->setToolTip("Quadratic term in Y – recalculates last pins on every click");
    connect(quadYCheck_, &QCheckBox::toggled, this, &AlignMainWindow::onQuadYToggled);
    ctrlLayout->addWidget(quadYCheck_);

    rotCheck_ = new QCheckBox("R", ctrlWidget);
    rotCheck_->setChecked(false);
    rotCheck_->setToolTip("Rotation/Shear: liniowe cross (gyc w X, gxc w Y)");
    connect(rotCheck_, &QCheckBox::toggled, this, &AlignMainWindow::onRotToggled);
    ctrlLayout->addWidget(rotCheck_);

    xyCheck_ = new QCheckBox("XY", ctrlWidget);
    xyCheck_->setChecked(false);
    xyCheck_->setToolTip("Quadratic XY cross: twist growing from center (gxc*gyc in both eq)");
    connect(xyCheck_, &QCheckBox::toggled, this, &AlignMainWindow::onXyToggled);
    ctrlLayout->addWidget(xyCheck_);

    ctrlLayout->addStretch(1);

    fitInfoLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(fitInfoLabel_);

    infoLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(infoLabel_);

    // central layout
    QWidget* central = new QWidget(this);
    QVBoxLayout* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0,0,0,0);
    vbox->addWidget(view_, 1);
    vbox->addWidget(ctrlWidget, 0);

    setCentralWidget(central);

    // Keyboard shortcuts for pair navigation
    {
        auto mkShortcut = [this](const QKeySequence& seq, auto slot) {
            QShortcut* sc = new QShortcut(seq, this);
            sc->setContext(Qt::ApplicationShortcut); // applies to the whole application
            connect(sc, &QShortcut::activated, this, slot);
        };

        mkShortcut(QKeySequence(Qt::Key_PageDown), &AlignMainWindow::goToNext);
        mkShortcut(QKeySequence(Qt::Key_PageUp),   &AlignMainWindow::goToPrev);
        mkShortcut(QKeySequence(Qt::Key_Home),     &AlignMainWindow::goFirst);
        mkShortcut(QKeySequence(Qt::Key_End),      &AlignMainWindow::goLast);
    }

    connect(view_, &AlignViewWidget::parametersChanged,
            this, &AlignMainWindow::updateInfoLabel);

    // Undo/redo signals
    connect(view_, &AlignViewWidget::beforePhysicalChange,
            this, &AlignMainWindow::pushUndoState);
    connect(view_, &AlignViewWidget::physicalDragFinished,
            this, &AlignMainWindow::onDragFinished);

    connect(view_, &AlignViewWidget::pinLabelClicked,
            this, &AlignMainWindow::onPinLabelClicked);

    // Context menu for pins
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QWidget::customContextMenuRequested,
            this, &AlignMainWindow::onPinContextMenu);
}

void AlignMainWindow::applyViewPreset(int idx)
{
    const bool showOutline = (idx == 0 || idx == 2);
    const bool showGray    = (idx == 1 || idx == 2);
    view_->setShowOutline(showOutline);
    view_->setShowGray(showGray);
}

bool AlignMainWindow::loadImageFile(const QString& path, cv::Mat& outMat)
{
    cv::Mat img = cv::imread(path.toStdString(), cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        QMessageBox::warning(this, "Error", "Cannot load image: " + path);
        return false;
    }
    outMat = img;
    return true;
}

// ------------ PROJECT AND PAIR LIST ------------

void AlignMainWindow::rebuildRecentMenu()
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

void AlignMainWindow::applyProjectDirs()
{
    grayDir_    = project_.grayDir;
    outlineDir_ = project_.outlineDir;

    // Sibling of the tracker's times-json: share the project-identity hash
    // so both files travel together. e.g. "<stem>.<hash>.times.json" →
    // "<stem>.<hash>.align.jsonl".
    const QString timesPath = tracker_.jsonPath();
    if (timesPath.endsWith(".times.json", Qt::CaseInsensitive)) {
        jsonlPath_ = timesPath.left(timesPath.size() - int(strlen(".times.json")))
                     + ".align.jsonl";
        QDir().mkpath(QFileInfo(jsonlPath_).absolutePath());
    } else {
        // Fallback (shouldn't happen — tracker_ is bound before this).
        const QString dirName = QDir(outlineDir_).dirName();
        const QString dataDir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        jsonlPath_ = QDir(dataDir).filePath(dirName + ".align.jsonl");
    }

    jsonlData_.clear();
    loadJsonlFile();

    statusBar()->showMessage(
        QString("Project: gray=%1  outline=%2  JSONL=%3")
            .arg(grayDir_, outlineDir_, jsonlPath_),
        5000
    );

    refreshPairsIfReady();
}

bool AlignMainWindow::loadProjectFromPath(const QString& path)
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
    setWindowTitle(QString("Align – %1").arg(QFileInfo(path).fileName()));
    tracker_.bindToProject(QFileInfo(path).absoluteFilePath());
    applyProjectDirs();
    return true;
}

bool AlignMainWindow::createNewProjectAt(const QString& path)
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
    setWindowTitle(QString("Align – %1").arg(QFileInfo(path).fileName()));
    tracker_.bindToProject(QFileInfo(path).absoluteFilePath());
    applyProjectDirs();
    return true;
}

void AlignMainWindow::onNewProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, "New project", startDir,
        "Align project (*.alprj)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".alprj", Qt::CaseInsensitive)) path += ".alprj";
    createNewProjectAt(path);
}

void AlignMainWindow::onOpenProject()
{
    const QString startDir = projectPath_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(projectPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Open project", startDir,
        "Align project (*.alprj);;All files (*)");
    if (path.isEmpty()) return;
    loadProjectFromPath(path);
}

void AlignMainWindow::onSetProject()
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
    tracker_.bindToProject(QFileInfo(projectPath_).absoluteFilePath());
    applyProjectDirs();
}

void AlignMainWindow::refreshPairsIfReady()
{
    pairs_.clear();
    currentIndex_ = -1;

    if (grayDir_.isEmpty() || outlineDir_.isEmpty())
        return;

    // load file lists from both directories
    auto listImagesInDir = [](const QString& dirPath) {
        QMap<QString, QString> map; // key = baseName, value = full path (QMap → sorted by name)
        QDir d(dirPath);
        QStringList filters;
        filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.tif" << "*.tiff";
        d.setNameFilters(filters);
        d.setFilter(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
        d.setSorting(QDir::Name);

        const QFileInfoList infos = d.entryInfoList();
        for (const QFileInfo& fi : infos) {
            QString base = fi.baseName(); // name without extension
            map.insert(base, fi.absoluteFilePath());
        }
        return map;
    };

    QMap<QString, QString> grayMap    = listImagesInDir(grayDir_);
    QMap<QString, QString> outlineMap = listImagesInDir(outlineDir_);

    // create pairs only for names present in both directories
    for (auto it = grayMap.cbegin(); it != grayMap.cend(); ++it) {
        const QString& base = it.key();
        if (!outlineMap.contains(base))
            continue;

        FilePair p;
        p.grayPath    = it.value();
        p.outlinePath = outlineMap.value(base);
        pairs_.push_back(p);
    }

    if (pairs_.isEmpty()) {
        statusBar()->showMessage("No matching pairs (same basename) between gray and outline dirs.", 5000);
        return;
    }

    currentIndex_ = 0;
    dirty_ = false;
    loadCurrentPair();
}

void AlignMainWindow::loadCurrentPair()
{
    if (currentIndex_ < 0 || currentIndex_ >= pairs_.size())
        return;

    // Clear undo/redo when switching image
    undoStack_.clear();
    redoStack_.clear();
    updateUndoRedoActions();

    // Reset saved state for new image
    savedStateValid_ = false;
    if (restoreAction_) restoreAction_->setEnabled(false);

    // Clear pins from view/menu (lastAppliedPins_ is kept – it could be
    // restored via Pins->Restore last, though it makes no sense for a different image).
    // Reset size to user-preferred pinCount_ (temporary expansion from Restore
    // last of the previous image disappears when switching image).
    pins_.clear();
    pins_.resize(pinCount_);
    refreshPinsInView();

    const FilePair& pair = pairs_[currentIndex_];
    const QString& grayPath    = pair.grayPath;
    const QString& outlinePath = pair.outlinePath;

    // Switch timing tracker key to new image (flush previous).
    const QString outlineName = QFileInfo(outlinePath).fileName();
    tracker_.setCurrentFile(outlineName);
    updateDoneButton(tracker_.isDone(outlineName));
    if (timeLabel_)
        timeLabel_->setText("Time: " + TimeTracker::formatHMS(tracker_.secondsFor(outlineName)));

    cv::Mat gMat, oMat;
    if (!loadImageFile(grayPath, gMat))
        return;
    if (!loadImageFile(outlinePath, oMat))
        return;

    // Detect black background in outline: compare sum of 30 darkest
    // and 30 brightest histogram values. If darks dominate,
    // background is black → invert image.
    {
        cv::Mat grayForHist;
        if (oMat.channels() == 1) {
            grayForHist = oMat;
        } else if (oMat.channels() == 3) {
            cv::cvtColor(oMat, grayForHist, cv::COLOR_BGR2GRAY);
        } else if (oMat.channels() == 4) {
            cv::cvtColor(oMat, grayForHist, cv::COLOR_BGRA2GRAY);
        }
        if (!grayForHist.empty() && grayForHist.depth() == CV_8U) {
            int hist[256] = {0};
            const int total = grayForHist.rows * grayForHist.cols;
            if (grayForHist.isContinuous()) {
                const uchar* p = grayForHist.ptr<uchar>(0);
                for (int i = 0; i < total; ++i) ++hist[p[i]];
            } else {
                for (int y = 0; y < grayForHist.rows; ++y) {
                    const uchar* p = grayForHist.ptr<uchar>(y);
                    for (int x = 0; x < grayForHist.cols; ++x) ++hist[p[x]];
                }
            }
            long long darkSum = 0, brightSum = 0;
            for (int i = 0; i < 30; ++i) {
                darkSum   += hist[i];
                brightSum += hist[255 - i];
            }
            if (darkSum > brightSum) {
                if (oMat.channels() == 4) {
                    std::vector<cv::Mat> ch;
                    cv::split(oMat, ch);
                    for (int i = 0; i < 3; ++i) cv::bitwise_not(ch[i], ch[i]);
                    cv::merge(ch, oMat);
                } else {
                    cv::bitwise_not(oMat, oMat);
                }
            }
        }
    }

    view_->setGrayImage(gMat);
    view_->setOutlineImage(oMat);

    // auto-fit gray to outline
    view_->fitGrayToOutline();

    // update GUI from view
    {
        QSignalBlocker b1(deltaXSpin_);
        QSignalBlocker b2(deltaYSpin_);
        QSignalBlocker b3(scaleXSpin_);
        QSignalBlocker b4(scaleYSpin_);

        deltaXSpin_->setValue(int(view_->deltaX()));
        deltaYSpin_->setValue(int(view_->deltaY()));
        scaleXSpin_->setValue(int(view_->scaleX()));
        scaleYSpin_->setValue(int(view_->scaleY()));
    }

    // try to load parameters from JSONL map.
    // When no entry (fresh image) – disable 4 fit checkboxes (X²/Y²/R/XY)
    // and clear lastAppliedPins_ (from previous image it makes no sense).
    if (!loadParamsFromMap()) {
        auto resetBox = [](QCheckBox* cb) {
            if (cb) { QSignalBlocker b(cb); cb->setChecked(false); }
        };
        resetBox(quadXCheck_);
        resetBox(quadYCheck_);
        resetBox(rotCheck_);
        resetBox(xyCheck_);
        lastAppliedPins_.clear();
        if (restoreLastPinsAction_) restoreLastPinsAction_->setEnabled(false);
        if (optimalModeAction_)     optimalModeAction_->setEnabled(false);
    }
    updateCheckboxesEnabled();

    // Remember loaded / initial state as baseline for "Restore"
    savedState_ = currentState();
    savedStateValid_ = true;
    if (restoreAction_) restoreAction_->setEnabled(true);

    dirty_ = false;

    updateInfoLabel();
}

// ------------ PAGE UP / PAGE DOWN NAVIGATION (with auto-save) ------------

void AlignMainWindow::goToNext()
{
    int n = pairs_.size();
    if (n == 0 || currentIndex_ < 0)
        return;

    if (currentIndex_ + 1 >= n)
        return; // already at list end

    ++currentIndex_;
    dirty_ = false;
    loadCurrentPair();
}

void AlignMainWindow::goToPrev()
{
    int n = pairs_.size();
    if (n == 0 || currentIndex_ < 0)
        return;

    if (currentIndex_ == 0)
        return; // already at list start

    --currentIndex_;
    dirty_ = false;
    loadCurrentPair();
}

// ------------ PARAMETER CHANGE → DIRTY ------------

void AlignMainWindow::markDirty()
{
    dirty_ = true;
    updateInfoLabel();
}

void AlignMainWindow::onDeltaXChanged(int value)
{
    if (suppressUndoPush_)
        return;

    // Begin/continue spinbox edit
    if (!spinboxEditInProgress_) {
        spinboxEditInProgress_ = true;
        stateBeforeSpinboxEdit_ = currentState();
    }
    spinboxTimer_->start();  // restart timer

    view_->setDelta(value, view_->deltaY());
    markDirty();
}

void AlignMainWindow::onDeltaYChanged(int value)
{
    if (suppressUndoPush_)
        return;

    // Begin/continue spinbox edit
    if (!spinboxEditInProgress_) {
        spinboxEditInProgress_ = true;
        stateBeforeSpinboxEdit_ = currentState();
    }
    spinboxTimer_->start();  // restart timer

    view_->setDelta(view_->deltaX(), value);
    markDirty();
}

void AlignMainWindow::onScaleXChanged(int value)
{
    if (suppressUndoPush_)
        return;

    // Begin/continue spinbox edit
    if (!spinboxEditInProgress_) {
        spinboxEditInProgress_ = true;
        stateBeforeSpinboxEdit_ = currentState();
    }
    spinboxTimer_->start();  // restart timer

    view_->setGrayScale(value, view_->scaleY());
    markDirty();
}

void AlignMainWindow::onScaleYChanged(int value)
{
    if (suppressUndoPush_)
        return;

    // Begin/continue spinbox edit
    if (!spinboxEditInProgress_) {
        spinboxEditInProgress_ = true;
        stateBeforeSpinboxEdit_ = currentState();
    }
    spinboxTimer_->start();  // restart timer

    view_->setGrayScale(view_->scaleX(), value);
    markDirty();
}

// ------------ MARGINS AND JSONL ------------

void AlignMainWindow::computeMargins(
    double& grayLeft, double& grayRight,
    double& grayTop, double& grayBottom,
    double& outlineLeft, double& outlineRight,
    double& outlineTop, double& outlineBottom
) const
{
    grayLeft = grayRight = grayTop = grayBottom = 0.0;
    outlineLeft = outlineRight = outlineTop = outlineBottom = 0.0;

    if (!view_->hasBothImages())
        return;

    QRectF gr = view_->grayRectOnWidget();
    QRectF orc = view_->outlineRectOnWidget();
    QRectF inter = gr.intersected(orc);

    if (inter.isEmpty())
        return;

    grayLeft   = inter.left()  - gr.left();
    grayRight  = gr.right()    - inter.right();
    grayTop    = inter.top()   - gr.top();
    grayBottom = gr.bottom()   - inter.bottom();

    outlineLeft   = inter.left()  - orc.left();
    outlineRight  = orc.right()   - inter.right();
    outlineTop    = inter.top()   - orc.top();
    outlineBottom = orc.bottom()  - inter.bottom();
}

// ------------ NEW JSONL LOGIC (map) ------------

void AlignMainWindow::loadJsonlFile()
{
    // Load JSONL file into jsonlData_ map
    if (jsonlPath_.isEmpty())
        return;

    QFile file(jsonlPath_);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    while (!file.atEnd()) {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        QJsonObject obj = doc.object();
        QString outlineName = obj["outline_name"].toString();
        if (outlineName.isEmpty())
            continue;

        // Store full JSON line under outline_name key
        jsonlData_[outlineName] = QString::fromUtf8(line);
    }
}

void AlignMainWindow::saveJsonlFile()
{
    // Save entire map to file (overwrite)
    if (jsonlPath_.isEmpty())
        return;

    QFile file(jsonlPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Error", "Cannot write JSONL file: " + jsonlPath_);
        return;
    }

    QTextStream out(&file);

    // QMap is sorted by keys
    for (auto it = jsonlData_.cbegin(); it != jsonlData_.cend(); ++it) {
        out << it.value() << "\n";
    }
}

void AlignMainWindow::updateCurrentInMap()
{
    // Update current entry in jsonlData_ map (new *.align.jsonl format).
    // Saves only pins + checkboxes (fitext) + file sizes and names.
    // Scale/delta/quad/rot/cross are restored on load by refitting.
    if (currentIndex_ < 0 || currentIndex_ >= pairs_.size())
        return;
    if (!view_->hasBothImages())
        return;
    if (lastAppliedPins_.isEmpty())
        return;   // no canonical pins to save

    const FilePair& pair = pairs_[currentIndex_];
    QString outlineName = QFileInfo(pair.outlinePath).fileName();
    QString grayName    = QFileInfo(pair.grayPath).fileName();

    QSize outlineSize = view_->originalOutlineSize();
    QSize graySize    = view_->originalGraySize();

    QJsonArray pinsArr;
    for (const PinPoint& p : lastAppliedPins_) {
        QJsonObject po;
        po["rX"] = int(std::round(p.outlineX));
        po["rY"] = int(std::round(p.outlineY));
        po["sX"] = int(std::round(p.grayX));
        po["sY"] = int(std::round(p.grayY));
        pinsArr.append(po);
    }

    QJsonObject fitext;
    fitext["X2"] = quadXCheck_ && quadXCheck_->isChecked();
    fitext["Y2"] = quadYCheck_ && quadYCheck_->isChecked();
    fitext["R"]  = rotCheck_   && rotCheck_->isChecked();
    fitext["XY"] = xyCheck_    && xyCheck_->isChecked();

    QJsonObject obj;
    obj["outline_name"] = outlineName;
    obj["gray_name"]    = grayName;
    obj["outlineW"]     = outlineSize.width();
    obj["outlineH"]     = outlineSize.height();
    obj["grayW"]        = graySize.width();
    obj["grayH"]        = graySize.height();
    obj["pins"]         = pinsArr;
    obj["fitext"]       = fitext;

    QJsonDocument doc(obj);
    jsonlData_[outlineName] = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

bool AlignMainWindow::loadParamsFromMap()
{
    // Load pins + fitext from map for current image and reconstruct transform.
    // Format *.align.jsonl: {pins:[{rX,rY,sX,sY},...], fitext:{X2,Y2,R,XY}, ...}.
    // Loaded pins become lastAppliedPins_ (confirmed) – view does not show them
    // until the user clicks Pins->Restore last.
    if (currentIndex_ < 0 || currentIndex_ >= pairs_.size())
        return false;

    const FilePair& pair = pairs_[currentIndex_];
    QString outlineName = QFileInfo(pair.outlinePath).fileName();

    if (!jsonlData_.contains(outlineName))
        return false;

    QString jsonLine = jsonlData_[outlineName];
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonLine.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    QJsonObject obj = doc.object();
    QJsonArray pinsArr = obj["pins"].toArray();
    if (pinsArr.isEmpty())
        return false;

    // --- Pin validation ---
    QSize outlineSize = view_->originalOutlineSize();
    QSize graySize    = view_->originalGraySize();
    double oW = outlineSize.width(),  oH = outlineSize.height();
    double gW = graySize.width(),     gH = graySize.height();

    QJsonObject fx = obj["fitext"].toObject();
    bool useQX = fx["X2"].toBool(false);
    bool useQY = fx["Y2"].toBool(false);
    bool useR  = fx["R"].toBool(false);
    bool useXY = fx["XY"].toBool(false);

    // 1) Range check
    QVector<PinPoint> ranged;
    int rangeDropped = 0;
    for (const QJsonValue& v : pinsArr) {
        QJsonObject po = v.toObject();
        PinPoint p;
        p.active = true;
        p.confirmed = true;
        p.outlineX = po["rX"].toDouble();
        p.outlineY = po["rY"].toDouble();
        p.grayX    = po["sX"].toDouble();
        p.grayY    = po["sY"].toDouble();
        bool inRange =
            p.outlineX >= -1 && p.outlineX <= oW + 1 &&
            p.outlineY >= -1 && p.outlineY <= oH + 1 &&
            p.grayX    >= -1 && p.grayX    <= gW + 1 &&
            p.grayY    >= -1 && p.grayY    <= gH + 1;
        if (inRange) ranged.append(p);
        else         ++rangeDropped;
    }

    // 2) First try fit with all in-range pins – usually succeeds.
    QVector<PinPoint> accepted;
    int sanityDropped = 0;
    {
        bool rej = false;
        computeAndApplyFromPins(ranged, useQX, useQY, useR, useXY,
                                /*applyResult=*/false, nullptr, nullptr, &rej);
        if (!rej) {
            accepted = ranged;
        } else {
            // Fallback: sequential addition rejecting degenerate sets.
            accepted.reserve(ranged.size());
            for (const PinPoint& candidate : ranged) {
                QVector<PinPoint> trial = accepted;
                trial.append(candidate);
                bool r = false;
                computeAndApplyFromPins(trial, useQX, useQY, useR, useXY,
                                        /*applyResult=*/false, nullptr, nullptr, &r);
                if (r) ++sanityDropped;
                else   accepted = trial;
            }
        }
    }

    if (accepted.isEmpty())
        return false;

    // Sync checkboxes without triggering slots (which would trigger a fit).
    if (quadXCheck_) { QSignalBlocker b(quadXCheck_); quadXCheck_->setChecked(useQX); }
    if (quadYCheck_) { QSignalBlocker b(quadYCheck_); quadYCheck_->setChecked(useQY); }
    if (rotCheck_)   { QSignalBlocker b(rotCheck_);   rotCheck_->setChecked(useR);   }
    if (xyCheck_)    { QSignalBlocker b(xyCheck_);    xyCheck_->setChecked(useXY);   }

    lastAppliedPins_ = accepted;
    if (restoreLastPinsAction_) restoreLastPinsAction_->setEnabled(true);
    if (optimalModeAction_)     optimalModeAction_->setEnabled(true);

    // Final apply z zaakceptowanymi (zapisuje do JSONL).
    computeAndApplyFromPins(lastAppliedPins_, useQX, useQY, useR, useXY);

    if (rangeDropped > 0 || sanityDropped > 0) {
        statusBar()->showMessage(
            QString("Loaded %1/%2 pins (range:%3, sanity:%4)")
                .arg(accepted.size()).arg(pinsArr.size())
                .arg(rangeDropped).arg(sanityDropped),
            8000);
    }

    // pins_[] remain empty – view and pin context menu show no pins
    // until the user selects Pins->Restore last.

    dirty_ = false;
    return true;
}

void AlignMainWindow::saveJsonlForCurrent()
{
    // Update map and save file
    if (!view_->hasBothImages()) {
        QMessageBox::information(this, "Info", "Both gray and outline must be loaded.");
        return;
    }
    if (currentIndex_ < 0 || currentIndex_ >= pairs_.size()) {
        QMessageBox::information(this, "Info", "No current pair.");
        return;
    }
    if (jsonlPath_.isEmpty()) {
        QMessageBox::information(this, "Info", "No JSONL file configured. Open outline directory first.");
        return;
    }

    updateCurrentInMap();
    saveJsonlFile();

    // After manual save, current state becomes "savedState"
    savedState_ = currentState();
    savedStateValid_ = true;
    if (restoreAction_) restoreAction_->setEnabled(true);

    dirty_ = false;
    statusBar()->showMessage("Saved to " + jsonlPath_, 3000);
    updateInfoLabel();
}

void AlignMainWindow::closeEvent(QCloseEvent* event)
{
    // TimeTracker flushes in its destructor.
    QMainWindow::closeEvent(event);
}

void AlignMainWindow::updateInfoLabel()
{
    // Sync spinboxes with view (without triggering slots)
    {
        QSignalBlocker b1(scaleXSpin_);
        QSignalBlocker b2(scaleYSpin_);
        QSignalBlocker b3(deltaXSpin_);
        QSignalBlocker b4(deltaYSpin_);

        scaleXSpin_->setValue(int(view_->scaleX()));
        scaleYSpin_->setValue(int(view_->scaleY()));
        deltaXSpin_->setValue(int(view_->deltaX()));
        deltaYSpin_->setValue(int(view_->deltaY()));
    }

    double gL, gR, gT, gB;
    double oL, oR, oT, oB;
    computeMargins(gL, gR, gT, gB, oL, oR, oT, oB);

    QString idxStr;
    int n = pairs_.size();
    if (currentIndex_ >= 0 && n > 0) {
        idxStr = QString("%1/%2").arg(currentIndex_ + 1).arg(n);
    } else {
        idxStr = "-/-";
    }

    // Outline filename
    QString fileName;
    if (currentIndex_ >= 0 && currentIndex_ < pairs_.size()) {
        fileName = QFileInfo(pairs_[currentIndex_].outlinePath).fileName();
    }

    QString txt = QString("%1  idx %2%3")
        .arg(fileName)
        .arg(idxStr)
        .arg(dirty_ ? "  [DIRTY]" : "");

    infoLabel_->setText(txt);
}

void AlignMainWindow::goFirst()
{
    if (pairs_.isEmpty())
        return;

    if (currentIndex_ == 0)
        return;  // already at start

    currentIndex_ = 0;
    dirty_ = false;
    loadCurrentPair();
}

void AlignMainWindow::goLast()
{
    if (pairs_.isEmpty())
        return;

    if (currentIndex_ == pairs_.size() - 1)
        return;  // already at end

    currentIndex_ = pairs_.size() - 1;
    dirty_ = false;
    loadCurrentPair();
}

void AlignMainWindow::goFirstUnaligned()
{
    if (pairs_.isEmpty())
        return;

    // First image not marked Done in the timing tracker.
    for (int i = 0; i < pairs_.size(); ++i) {
        const QString outlineName = QFileInfo(pairs_[i].outlinePath).fileName();
        if (tracker_.isDone(outlineName))
            continue;

        if (i == currentIndex_) {
            statusBar()->showMessage(
                QString("You are already on the first not-Done pair (%1/%2)")
                    .arg(i + 1).arg(pairs_.size()),
                3000);
            return;
        }
        currentIndex_ = i;
        dirty_ = false;
        loadCurrentPair();
        return;
    }

    statusBar()->showMessage("All pairs marked Done", 3000);
}

// ------------ UNDO / REDO ------------

AlignState AlignMainWindow::currentState()
{
    return AlignState{
        view_->scaleX(),
        view_->scaleY(),
        view_->deltaX(),
        view_->deltaY(),
        view_->quadX(),
        view_->quadY(),
        view_->rotXY(),
        view_->rotYX(),
        view_->crossXY(),
        view_->crossYX()
    };
}

void AlignMainWindow::applyState(const AlignState& state)
{
    suppressUndoPush_ = true;

    view_->setGrayScale(state.scaleX, state.scaleY);
    view_->setDelta(state.deltaX, state.deltaY);
    view_->setQuadX(state.quadX);
    view_->setQuadY(state.quadY);
    view_->setRotXY(state.rotXY);
    view_->setRotYX(state.rotYX);
    view_->setCrossXY(state.crossXY);
    view_->setCrossYX(state.crossYX);

    // Sync spinboxes
    {
        QSignalBlocker b1(scaleXSpin_);
        QSignalBlocker b2(scaleYSpin_);
        QSignalBlocker b3(deltaXSpin_);
        QSignalBlocker b4(deltaYSpin_);

        scaleXSpin_->setValue(int(state.scaleX));
        scaleYSpin_->setValue(int(state.scaleY));
        deltaXSpin_->setValue(int(state.deltaX));
        deltaYSpin_->setValue(int(state.deltaY));
    }

    suppressUndoPush_ = false;

    markDirty();
    updateUndoRedoActions();
}

void AlignMainWindow::updateUndoRedoActions()
{
    if (undoAction_)
        undoAction_->setEnabled(!undoStack_.isEmpty());
    if (redoAction_)
        redoAction_->setEnabled(!redoStack_.isEmpty());
}

void AlignMainWindow::pushUndoState()
{
    if (suppressUndoPush_)
        return;

    AlignState state = currentState();

    // Don't add duplicate
    if (!undoStack_.isEmpty() && undoStack_.top() == state)
        return;

    undoStack_.push(state);
    redoStack_.clear();  // new change clears redo stack

    updateUndoRedoActions();
}

void AlignMainWindow::onDragFinished()
{
    // Ctrl+drag finished – nothing to do, state was already saved in beforePhysicalChange
    // (we only emit dragFinished signal for possible future extensions)
    markDirty();
}

void AlignMainWindow::undo()
{
    if (undoStack_.isEmpty())
        return;

    // Save current state to redo
    redoStack_.push(currentState());

    // Restore previous state
    AlignState prev = undoStack_.pop();
    applyState(prev);
}

void AlignMainWindow::redo()
{
    if (redoStack_.isEmpty())
        return;

    // Save current state to undo
    undoStack_.push(currentState());

    // Restore next state
    AlignState next = redoStack_.pop();
    applyState(next);
}

void AlignMainWindow::restore()
{
    // Revert to state saved in JSON (or initial from fitGrayToOutline)
    if (!savedStateValid_)
        return;

    AlignState now = currentState();
    if (now == savedState_)
        return;

    // Preserve ability to undo the restore
    undoStack_.push(now);
    redoStack_.clear();

    applyState(savedState_);
    statusBar()->showMessage("Saved state restored", 2000);
}

void AlignMainWindow::onSpinboxEditTimeout()
{
    // Timer expired – ending spinbox edit
    if (!spinboxEditInProgress_)
        return;

    spinboxEditInProgress_ = false;

    AlignState now = currentState();

    // If state changed since edit began, push to undo
    if (now != stateBeforeSpinboxEdit_) {
        // Save state BEFORE edit (not current!)
        if (undoStack_.isEmpty() || undoStack_.top() != stateBeforeSpinboxEdit_) {
            undoStack_.push(stateBeforeSpinboxEdit_);
            redoStack_.clear();
            updateUndoRedoActions();
        }
    }
}

bool AlignMainWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Activity tracking – count only real input (no MouseMove)
    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel:
        case QEvent::KeyPress:
            tracker_.registerActivity();
            break;
        default: break;
    }

    // Intercept keys in spinboxes
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // Ctrl+Z → global undo (instead of spinbox undo)
        if (keyEvent->matches(QKeySequence::Undo)) {
            undo();
            return true;  // event handled
        }

        // Ctrl+Shift+Z → global redo
        if ((keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) &&
            keyEvent->key() == Qt::Key_Z) {
            redo();
            return true;
        }

        // Esc → move focus to view
        if (keyEvent->key() == Qt::Key_Escape) {
            view_->setFocus();
            return true;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

// ------------ PINS ------------

void AlignMainWindow::onPinContextMenu(const QPoint& pos)
{
    if (!view_->hasBothImages())
        return;

    // Remember click position (in widget coordinates)
    lastContextMenuPos_ = pos;

    // Create context menu
    QMenu menu(this);

    // Sections depend on the number of unknowns in the current checkbox configuration.
    //   section 1 (under-determined / heuristic): too few pins for full solution
    //   section 2 (exact solution): pin count = unknowns / 2
    //   section 3 (least squares): more pins than minimum
    bool qX = quadXCheck_ && quadXCheck_->isChecked();
    bool qY = quadYCheck_ && quadYCheck_->isChecked();
    bool rR = rotCheck_   && rotCheck_->isChecked();
    bool xY = xyCheck_    && xyCheck_->isChecked();

    int sec1End, sec2End;
    bool coupled = rR || xY;

    if (coupled) {
        // Coupled system: dX, dY, aX, aY + optional quad/rot/cross
        int total = 4;                       // dX, dY, aX, aY
        if (qX) total += 1;
        if (qY) total += 1;
        if (rR) total += 2;                  // bXY, bYX
        if (xY) total += 2;                  // eXY, eYX
        int minN = (total + 1) / 2;
        sec1End = std::max(0, minN - 1);
        sec2End = (total % 2 == 0) ? (total / 2) : sec1End;
    } else {
        // Separable: per axis (dX,aX,?cX) and (dY,aY,?cY)
        int uX = 2 + (qX ? 1 : 0);
        int uY = 2 + (qY ? 1 : 0);
        int maxU = std::max(uX, uY);
        sec1End = std::max(0, maxU - 1);
        sec2End = (uX == uY) ? maxU : sec1End;
    }

    auto addPinAction = [&](int i) {
        QAction* act = menu.addAction(QString("Pin %1").arg(i + 1));
        act->setCheckable(true);
        act->setChecked(pins_[i].active);
        connect(act, &QAction::triggered, this, [this, i]() {
            togglePin(i);
        });
    };

    for (int i = 0; i < pins_.size(); ++i) {
        bool sep1 = (i == sec1End) && sec1End > 0;
        bool sep2 = (i == sec2End) && sec2End > sec1End;
        if (sep1 || sep2)
            menu.addSeparator();
        addPinAction(i);
    }

    menu.addSeparator();

    // "Apply pins" action – grayed out when not enough active pins for
    // the current checkbox configuration.
    int nActive = 0;
    for (int i = 0; i < pins_.size(); ++i) if (pins_[i].active) ++nActive;
    int req = requiredPins(qX, qY, rR, xY);
    QString applyLabel = (nActive >= req)
        ? QString("Apply pins")
        : QString("Apply pins (need %1, have %2)").arg(req).arg(nActive);
    QAction* applyAct = menu.addAction(applyLabel);
    applyAct->setEnabled(nActive >= req);
    connect(applyAct, &QAction::triggered, this, &AlignMainWindow::applyPins);

    // Show menu at global position
    menu.exec(view_->mapToGlobal(pos));
}

void AlignMainWindow::refreshPinsInView()
{
    if (!view_) return;
    QVector<AlignViewWidget::DrawPin> v;
    v.reserve(pins_.size());
    for (int i = 0; i < pins_.size(); ++i) {
        v.append({ pins_[i].active, pins_[i].confirmed,
                   pins_[i].outlineX, pins_[i].outlineY,
                   pins_[i].grayX,    pins_[i].grayY });
    }
    view_->setPinsToDraw(v);
}

int AlignMainWindow::requiredPins(bool qX, bool qY, bool R, bool XY)
{
    int M = 4;          // dX, dY, aX, aY (always fitted when n >= 2)
    if (qX) ++M;
    if (qY) ++M;
    if (R)  M += 2;     // bXY, bYX
    if (XY) M += 2;     // eXY, eYX
    return (M + 1) / 2; // ceil(M/2) – each pin contributes 2 equations
}

void AlignMainWindow::updateCheckboxesEnabled()
{
    // Pin count used for the decision: lastAppliedPins_ has priority
    // (these are the "canonical" pins for the last Apply). When absent – fallback to
    // active pins in pins_[] (e.g. user adding fresh pins before Apply).
    int n = lastAppliedPins_.size();
    if (n == 0) {
        for (int i = 0; i < pins_.size(); ++i)
            if (pins_[i].active) ++n;
    }

    bool qX = quadXCheck_ && quadXCheck_->isChecked();
    bool qY = quadYCheck_ && quadYCheck_->isChecked();
    bool R  = rotCheck_   && rotCheck_->isChecked();
    bool XY = xyCheck_    && xyCheck_->isChecked();

    // Cascade force-off: if currently checked require more pins than n,
    // uncheck in order from most expensive (XY, R, qY, qX).
    while (requiredPins(qX, qY, R, XY) > n) {
        if (XY)      { XY = false; if (xyCheck_)    { QSignalBlocker b(xyCheck_);    xyCheck_->setChecked(false); } continue; }
        if (R)       { R  = false; if (rotCheck_)   { QSignalBlocker b(rotCheck_);   rotCheck_->setChecked(false); } continue; }
        if (qY)      { qY = false; if (quadYCheck_) { QSignalBlocker b(quadYCheck_); quadYCheck_->setChecked(false); } continue; }
        if (qX)      { qX = false; if (quadXCheck_) { QSignalBlocker b(quadXCheck_); quadXCheck_->setChecked(false); } continue; }
        break;        // n < 2 – not enough even without checkboxes
    }

    // For each checkbox: enabled iff it can be turned on (or is already on).
    if (quadXCheck_) quadXCheck_->setEnabled(qX || requiredPins(true, qY, R, XY) <= n);
    if (quadYCheck_) quadYCheck_->setEnabled(qY || requiredPins(qX, true, R, XY) <= n);
    if (rotCheck_)   rotCheck_->setEnabled  (R  || requiredPins(qX, qY, true, XY) <= n);
    if (xyCheck_)    xyCheck_->setEnabled   (XY || requiredPins(qX, qY, R, true) <= n);
}

void AlignMainWindow::setPinCount()
{
    bool ok = false;
    int v = QInputDialog::getInt(this, "Pin count",
        "Number of pin slots (min 5):",
        pinCount_, 5, 200, 1, &ok);
    if (!ok)
        return;
    pinCount_ = v;
    // Apply to current image: resize pins_ to pinCount_,
    // unless active slots are currently higher (preserve contents).
    int active = 0;
    for (int i = 0; i < pins_.size(); ++i) if (pins_[i].active) active = i + 1;
    int newSize = std::max(pinCount_, active);
    pins_.resize(newSize);   // QVector::resize preserves existing elements, fills new ones with PinPoint{}
    refreshPinsInView();
    updateCheckboxesEnabled();
    statusBar()->showMessage(QString("Pin count = %1").arg(pinCount_), 3000);
}

void AlignMainWindow::hidePins()
{
    for (int i = 0; i < pins_.size(); ++i) {
        pins_[i] = PinPoint{};
    }
    refreshPinsInView();
    updateCheckboxesEnabled();
}

void AlignMainWindow::restoreLastPins()
{
    if (lastAppliedPins_.isEmpty()) {
        statusBar()->showMessage("No previous pins to restore", 3000);
        return;
    }
    int needed = int(lastAppliedPins_.size());
    // Temporarily increase slot count for this image if saved > pinCount_.
    if (needed > int(pins_.size())) {
        pins_.resize(needed);
    }
    for (int i = 0; i < pins_.size(); ++i) {
        pins_[i] = PinPoint{};
    }
    int n = std::min(needed, int(pins_.size()));
    for (int i = 0; i < n; ++i) {
        pins_[i] = lastAppliedPins_[i];
        pins_[i].confirmed = true;        // restored = blue (were previously confirmed)
    }
    refreshPinsInView();
    updateCheckboxesEnabled();
    statusBar()->showMessage(QString("Restored %1 pins").arg(n), 3000);
}

void AlignMainWindow::onPinLabelClicked(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= pins_.size())
        return;
    if (!pins_[slotIndex].active)
        return;

    QSize graySize = view_->originalGraySize();
    QSize outlineSize = view_->originalOutlineSize();
    if (graySize.isEmpty() || outlineSize.isEmpty())
        return;

    const PinPoint& p = pins_[slotIndex];
    double grayW = graySize.width();
    double grayH = graySize.height();
    double outlineW = outlineSize.width();
    double outlineH = outlineSize.height();

    double gxc = p.grayX - grayW / 2.0;
    double gyc = p.grayY - grayH / 2.0;
    double oxc = p.outlineX - outlineW / 2.0;
    double oyc = p.outlineY - outlineH / 2.0;

    double aX = view_->scaleX() / grayW;
    double aY = view_->scaleY() / grayH;

    // forward T(p) bez delty
    double ox_no_delta = aX*gxc + view_->rotXY()*gyc
                       + view_->quadX()*gxc*gxc + view_->crossXY()*gxc*gyc;
    double oy_no_delta = view_->rotYX()*gxc + aY*gyc
                       + view_->quadY()*gyc*gyc + view_->crossYX()*gxc*gyc;

    double newDeltaX = oxc - ox_no_delta;
    double newDeltaY = oyc - oy_no_delta;

    pushUndoState();
    view_->setDelta(newDeltaX, newDeltaY);
    {
        QSignalBlocker b1(deltaXSpin_);
        QSignalBlocker b2(deltaYSpin_);
        deltaXSpin_->setValue(int(newDeltaX));
        deltaYSpin_->setValue(int(newDeltaY));
    }
    markDirty();
    statusBar()->showMessage(QString("Snapped delta to pin #%1").arg(slotIndex + 1), 3000);
}

void AlignMainWindow::togglePin(int pinIndex)
{
    if (pinIndex < 0 || pinIndex >= pins_.size())
        return;

    PinPoint& pin = pins_[pinIndex];

    if (pin.active) {
        // Deactivate pin
        pin.active = false;
        statusBar()->showMessage(QString("Pin %1 deactivated").arg(pinIndex + 1), 2000);
        refreshPinsInView();
    } else {
        // Activate pin – save coordinates
        QPointF grayCoords = view_->screenToGrayImageCoords(lastContextMenuPos_);
        QPointF outlineCoords = view_->screenToOutlineImageCoords(lastContextMenuPos_);

        pin.active = true;
        pin.confirmed = false;        // new pin = unconfirmed (green)
        pin.grayX = grayCoords.x();
        pin.grayY = grayCoords.y();
        pin.outlineX = outlineCoords.x();
        pin.outlineY = outlineCoords.y();

        statusBar()->showMessage(
            QString("Pin %1: gray(%2,%3) → outline(%4,%5)")
                .arg(pinIndex + 1)
                .arg(pin.grayX, 0, 'f', 1)
                .arg(pin.grayY, 0, 'f', 1)
                .arg(pin.outlineX, 0, 'f', 1)
                .arg(pin.outlineY, 0, 'f', 1),
            3000
        );
        refreshPinsInView();
    }
    updateCheckboxesEnabled();
}

void AlignMainWindow::applyPins()
{
    // Collect active pins
    QVector<PinPoint> activePins;
    for (int i = 0; i < pins_.size(); ++i) {
        if (pins_[i].active) {
            activePins.append(pins_[i]);
        }
    }

    if (activePins.isEmpty()) {
        statusBar()->showMessage("No active pins!", 3000);
        return;
    }

    // Remember for recalculation after checkbox toggle
    lastAppliedPins_ = activePins;
    if (restoreLastPinsAction_) restoreLastPinsAction_->setEnabled(true);
    if (optimalModeAction_) optimalModeAction_->setEnabled(true);

    bool useQX = quadXCheck_ && quadXCheck_->isChecked();
    bool useQY = quadYCheck_ && quadYCheck_->isChecked();
    bool useR  = rotCheck_   && rotCheck_->isChecked();
    bool useXY = xyCheck_    && xyCheck_->isChecked();
    computeAndApplyFromPins(activePins, useQX, useQY, useR, useXY);

    // Compact pins: active ones into slots 0..n-1, rest cleared.
    // Acts like an immediate Restore last – pins remain visible.
    int n = activePins.size();
    for (int i = 0; i < pins_.size(); ++i) {
        if (i < n) {
            pins_[i] = activePins[i];
            pins_[i].confirmed = true;        // confirmed = blue
        } else {
            pins_[i] = PinPoint{};
        }
    }
    refreshPinsInView();
    updateCheckboxesEnabled();
}

namespace {
// Gauss elimination with partial pivoting: solves A*x=b for M<=12.
// Max M in our solver = 10 (Free + X² + Y² + R + XY).
// Returns false when matrix is singular or M > MAX.
bool solveLinearSystem(int M, double* A, double* b, double* x)
{
    constexpr int MAX = 12;
    if (M > MAX) return false;
    double a[MAX][MAX + 1];
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < M; ++j) a[i][j] = A[i * M + j];
        a[i][M] = b[i];
    }
    for (int k = 0; k < M; ++k) {
        int piv = k;
        double maxAbs = std::abs(a[k][k]);
        for (int i = k + 1; i < M; ++i) {
            if (std::abs(a[i][k]) > maxAbs) { maxAbs = std::abs(a[i][k]); piv = i; }
        }
        if (maxAbs < 1e-12) return false;
        if (piv != k) for (int j = k; j <= M; ++j) std::swap(a[k][j], a[piv][j]);
        for (int i = k + 1; i < M; ++i) {
            double f = a[i][k] / a[k][k];
            for (int j = k; j <= M; ++j) a[i][j] -= f * a[k][j];
        }
    }
    for (int i = M - 1; i >= 0; --i) {
        double s = a[i][M];
        for (int j = i + 1; j < M; ++j) s -= a[i][j] * x[j];
        x[i] = s / a[i][i];
    }
    return true;
}
}  // anon

double AlignMainWindow::computeAndApplyFromPins(const QVector<PinPoint>& activePins,
                                                bool useQuadX, bool useQuadY,
                                                bool useRot, bool useXY,
                                                bool applyResult,
                                                double* maxAbsErr,
                                                int* maxAbsPin,
                                                bool* rejected)
{
    if (activePins.isEmpty())
        return 0.0;

    QSize graySize = view_->originalGraySize();
    QSize outlineSize = view_->originalOutlineSize();

    if (graySize.isEmpty() || outlineSize.isEmpty())
        return 0.0;

    double grayW = graySize.width();
    double grayH = graySize.height();
    double outlineW = outlineSize.width();
    double outlineH = outlineSize.height();
    int n = activePins.size();

    // Current scales (needed for pin scale / fallbacks)
    double currentAX = view_->scaleX() / grayW;
    double currentAY = view_->scaleY() / grayH;

    // Centered coordinates and sums
    std::vector<double> gxV(n), gyV(n), oxV(n), oyV(n);
    double sumGxc=0, sumGxc2=0, sumGxc3=0, sumGxc4=0;
    double sumOxc=0, sumGxcOxc=0, sumGxc2Oxc=0;
    double sumGyc=0, sumGyc2=0, sumGyc3=0, sumGyc4=0;
    double sumOyc=0, sumGycOyc=0, sumGyc2Oyc=0;

    for (int i = 0; i < n; ++i) {
        const PinPoint& p = activePins[i];
        double gxc = p.grayX - grayW / 2.0;
        double gyc = p.grayY - grayH / 2.0;
        double oxc = p.outlineX - outlineW / 2.0;
        double oyc = p.outlineY - outlineH / 2.0;
        gxV[i]=gxc; gyV[i]=gyc; oxV[i]=oxc; oyV[i]=oyc;
        double gxc2 = gxc*gxc, gyc2 = gyc*gyc;
        sumGxc   += gxc;   sumGxc2  += gxc2;
        sumGxc3  += gxc2*gxc;  sumGxc4 += gxc2*gxc2;
        sumOxc   += oxc;   sumGxcOxc += gxc*oxc;
        sumGxc2Oxc += gxc2*oxc;
        sumGyc   += gyc;   sumGyc2  += gyc2;
        sumGyc3  += gyc2*gyc;  sumGyc4 += gyc2*gyc2;
        sumOyc   += oyc;   sumGycOyc += gyc*oyc;
        sumGyc2Oyc += gyc2*oyc;
    }

    // Save state to undo (only when actually applying)
    if (applyResult)
        pushUndoState();

    double newScaleX = view_->scaleX();
    double newScaleY = view_->scaleY();
    double newDeltaX = 0.0, newDeltaY = 0.0;
    double newQuadX = 0.0, newQuadY = 0.0;

    double newRotXY = 0.0, newRotYX = 0.0;
    double newCrossXY = 0.0, newCrossYX = 0.0;

    // Special rules (user decision):
    //   n=1: fit delta only; rest = 0 (scale = current).
    //   n=2: fit delta + scaleX + scaleY; rest = 0.
    //   n>=3: full solver with optional qX/qY/R/XY (with fallback when 2n < M).
    if (n == 1) {
        // delta = oxc - currentAX*gxc ; reszta zerowana
        newDeltaX = oxV[0] - currentAX * gxV[0];
        newDeltaY = oyV[0] - currentAY * gyV[0];
        // scale bez zmian
    } else if (n == 2) {
        // Two pins, 4 unknowns (dX,dY,aX,aY) – exact, separable per axis.
        // Per X: dX + aX*gxc_i = oxc_i ; per Y: analogous.
        double detX = 2 * sumGxc2 - sumGxc * sumGxc;
        double detY = 2 * sumGyc2 - sumGyc * sumGyc;
        if (std::abs(detX) > 1e-9) {
            double aX = (2 * sumGxcOxc - sumGxc * sumOxc) / detX;
            newDeltaX = (sumOxc - aX * sumGxc) / 2.0;
            newScaleX = std::abs(aX * grayW);
        } else {
            newDeltaX = sumOxc / 2.0;
        }
        if (std::abs(detY) > 1e-9) {
            double aY = (2 * sumGycOyc - sumGyc * sumOyc) / detY;
            newDeltaY = (sumOyc - aY * sumGyc) / 2.0;
            newScaleY = std::abs(aY * grayH);
        } else {
            newDeltaY = sumOyc / 2.0;
        }
        if (newScaleX < 1.0) newScaleX = 1.0;
        if (newScaleY < 1.0) newScaleY = 1.0;
    } else if (useRot || useXY) {
        // Coupled solver for R/XY (+ optional quad).
        // Unknowns: dX, dY, aX, aY, [qX], [qY], [bXY,bYX], [eXY,eYX].
        bool fQuadX = useQuadX, fQuadY = useQuadY;
        bool fRot = useRot, fXY = useXY;

        auto countUnknowns = [&]() {
            int M = 4;                       // dX, dY, aX, aY
            if (fQuadX) M += 1;
            if (fQuadY) M += 1;
            if (fRot)   M += 2;
            if (fXY)    M += 2;
            return M;
        };

        // Fallback: drop optional features until 2N >= M
        while (2 * n < countUnknowns()) {
            if (fXY)    { fXY = false; continue; }
            if (fRot)   { fRot = false; continue; }
            if (fQuadY) { fQuadY = false; continue; }
            if (fQuadX) { fQuadX = false; continue; }
            break;
        }

        int M = countUnknowns();
        int colAX = 2, colAY = 3;
        int colCX = -1, colCY = -1;
        int colBXY = -1, colBYX = -1;
        int colEXY = -1, colEYX = -1;
        int nextCol = 4;
        if (fQuadX) colCX  = nextCol++;
        if (fQuadY) colCY  = nextCol++;
        if (fRot)   { colBXY = nextCol++; colBYX = nextCol++; }
        if (fXY)    { colEXY = nextCol++; colEYX = nextCol++; }

        std::vector<double> AtA(M*M, 0), Atb(M, 0);
        auto accumulate = [&](const std::vector<double>& row, double rhs) {
            for (int i = 0; i < M; ++i) {
                for (int j = 0; j < M; ++j) AtA[i*M+j] += row[i]*row[j];
                Atb[i] += rhs * row[i];
            }
        };
        for (int i = 0; i < n; ++i) {
            double gxc = gxV[i], gyc = gyV[i];
            std::vector<double> rowX(M, 0), rowY(M, 0);
            rowX[0] = 1.0;
            rowY[1] = 1.0;
            rowX[colAX] = gxc;
            rowY[colAY] = gyc;
            if (colCX  >= 0) rowX[colCX]  = gxc * gxc;
            if (colCY  >= 0) rowY[colCY]  = gyc * gyc;
            if (colBXY >= 0) rowX[colBXY] = gyc;
            if (colBYX >= 0) rowY[colBYX] = gxc;
            if (colEXY >= 0) rowX[colEXY] = gxc * gyc;
            if (colEYX >= 0) rowY[colEYX] = gxc * gyc;
            accumulate(rowX, oxV[i]);
            accumulate(rowY, oyV[i]);
        }

        std::vector<double> x(M, 0);
        if (solveLinearSystem(M, AtA.data(), Atb.data(), x.data())) {
            newDeltaX = x[0];
            newDeltaY = x[1];
            newScaleX = std::abs(x[colAX] * grayW);
            newScaleY = std::abs(x[colAY] * grayH);
            newQuadX  = (colCX  >= 0) ? x[colCX]  : 0.0;
            newQuadY  = (colCY  >= 0) ? x[colCY]  : 0.0;
            newRotXY  = (colBXY >= 0) ? x[colBXY] : 0.0;
            newRotYX  = (colBYX >= 0) ? x[colBYX] : 0.0;
            newCrossXY = (colEXY >= 0) ? x[colEXY] : 0.0;
            newCrossYX = (colEYX >= 0) ? x[colEYX] : 0.0;
        } else {
            // Singular - fallback do translacji
            newDeltaX = (sumOxc - currentAX * sumGxc) / n;
            newDeltaY = (sumOyc - currentAY * sumGyc) / n;
        }
        if (newScaleX < 1.0) newScaleX = 1.0;
        if (newScaleY < 1.0) newScaleY = 1.0;
    } else {
        // Separable mode (R and XY disabled) – per axis.
        auto solveAxis = [&](bool useQuad,
                             double curA,
                             double sG, double sG2, double sG3, double sG4,
                             double sO, double sGO, double sG2O,
                             double& outB, double& outA, double& outC)
        {
            outA = curA;
            outC = 0;
            outB = 0;

            int unknowns = 2 + (useQuad ? 1 : 0);
            if (n < unknowns && useQuad) {
                useQuad = false;
                unknowns = 2;
            }

            if (!useQuad) {
                double det = n * sG2 - sG * sG;
                if (std::abs(det) > 1e-9) {
                    outA = (n * sGO - sG * sO) / det;
                    outB = (sO - outA * sG) / n;
                } else {
                    outA = curA; outB = 0;
                }
                outC = 0;
                return;
            }
            // useQuad: 3 unknowns
            double A[9] = {
                double(n), sG,  sG2,
                sG,        sG2, sG3,
                sG2,       sG3, sG4
            };
            double rhs[3] = { sO, sGO, sG2O };
            double xs[3] = {0};
            if (solveLinearSystem(3, A, rhs, xs)) {
                outB = xs[0]; outA = xs[1]; outC = xs[2];
            } else {
                double det = n * sG2 - sG * sG;
                if (std::abs(det) > 1e-9) {
                    outA = (n * sGO - sG * sO) / det;
                    outB = (sO - outA * sG) / n;
                } else {
                    outA = curA; outB = 0;
                }
                outC = 0;
            }
        };

        double bX=0, aX=currentAX, cX=0;
        double bY=0, aY=currentAY, cY=0;
        solveAxis(useQuadX, currentAX,
                  sumGxc, sumGxc2, sumGxc3, sumGxc4,
                  sumOxc, sumGxcOxc, sumGxc2Oxc,
                  bX, aX, cX);
        solveAxis(useQuadY, currentAY,
                  sumGyc, sumGyc2, sumGyc3, sumGyc4,
                  sumOyc, sumGycOyc, sumGyc2Oyc,
                  bY, aY, cY);

        newDeltaX = bX;
        newDeltaY = bY;
        newScaleX = std::abs(aX * grayW);
        newScaleY = std::abs(aY * grayH);
        if (newScaleX < 1.0) newScaleX = 1.0;
        if (newScaleY < 1.0) newScaleY = 1.0;
        newQuadX = cX;
        newQuadY = cY;
    }


    // Compute RMS and max(|residual|) per pin (forward T)
    double newAX = newScaleX / grayW;
    double newAY = newScaleY / grayH;
    double sumSqErr = 0.0;
    double maxAbs = 0.0;
    int maxAbsIdx = 0;
    for (int i = 0; i < n; ++i) {
        double gxc = gxV[i], gyc = gyV[i];
        double predOx = newDeltaX + newAX*gxc + newRotXY*gyc
                      + newQuadX*gxc*gxc + newCrossXY*gxc*gyc;
        double predOy = newDeltaY + newRotYX*gxc + newAY*gyc
                      + newQuadY*gyc*gyc + newCrossYX*gxc*gyc;
        double ex = predOx - oxV[i];
        double ey = predOy - oyV[i];
        sumSqErr += ex*ex + ey*ey;
        double absErr = std::sqrt(ex*ex + ey*ey);
        if (absErr > maxAbs) { maxAbs = absErr; maxAbsIdx = i; }
    }
    double rms = std::sqrt(sumSqErr / (2.0 * n));
    if (maxAbsErr) *maxAbsErr = maxAbs;
    if (maxAbsPin) *maxAbsPin = maxAbsIdx + 1;  // 1-based compact index

    // --- Sanity check: per-parameter limits tuned to typical values --
    // (observed in existing jsonl: scale ~1× outline size, |delta| < 200px,
    //  |quad| < 2e-4, |rot| < 0.03, |cross| < 1.1e-4). Limits give ~5-10× headroom
    //  above p95 but catch orders-of-magnitude outliers (e.g. quadY=0.286, scaleY=31k).
    //
    // Checked ALWAYS (including compute-only mode) – sequential loading relies
    // on the 'rejected' flag to filter pins.
    bool sanityFailed = false;
    QString sanityOffending;
    {
        struct Check { const char* name; double val; double limit; };
        double maxDim = std::max(outlineW, outlineH);
        bool scaleOk = std::isfinite(newScaleX) && std::isfinite(newScaleY)
                    && newScaleX >= outlineW / 3.0  && newScaleX <= outlineW * 3.0
                    && newScaleY >= outlineH / 3.0  && newScaleY <= outlineH * 3.0;
        Check checks[] = {
            {"|deltaX|",  std::abs(newDeltaX),  maxDim},
            {"|deltaY|",  std::abs(newDeltaY),  maxDim},
            {"|quadX|",   std::abs(newQuadX),   1e-3},
            {"|quadY|",   std::abs(newQuadY),   1e-3},
            {"|rotXY|",   std::abs(newRotXY),   0.1},
            {"|rotYX|",   std::abs(newRotYX),   0.1},
            {"|crossXY|", std::abs(newCrossXY), 1e-3},
            {"|crossYX|", std::abs(newCrossYX), 1e-3},
        };
        sanityFailed = !scaleOk;
        if (!scaleOk) {
            sanityOffending = QString("scale=(%1,%2) outline=(%3,%4)")
                .arg(int(newScaleX)).arg(int(newScaleY))
                .arg(int(outlineW)).arg(int(outlineH));
        }
        for (auto& c : checks) {
            if (!std::isfinite(c.val) || c.val > c.limit) {
                sanityFailed = true;
                if (!sanityOffending.isEmpty()) sanityOffending += "; ";
                sanityOffending += QString("%1=%2 > %3")
                    .arg(c.name).arg(c.val, 0, 'g', 3).arg(c.limit, 0, 'g', 3);
                break;
            }
        }
    }
    if (rejected) *rejected = sanityFailed;

    if (!applyResult)
        return rms;   // compute-only mode – no side effects (undo/view/JSONL)

    if (sanityFailed) {
        if (!undoStack_.isEmpty()) undoStack_.pop();
        statusBar()->showMessage(QString("Fit rejected: %1").arg(sanityOffending), 8000);
        if (fitInfoLabel_) fitInfoLabel_->setText("fit rejected: " + sanityOffending);
        return rms;
    }

    // Apply new values
    view_->setGrayScale(newScaleX, newScaleY);
    view_->setDelta(newDeltaX, newDeltaY);
    view_->setQuadX(newQuadX);
    view_->setQuadY(newQuadY);
    view_->setRotXY(newRotXY);
    view_->setRotYX(newRotYX);
    view_->setCrossXY(newCrossXY);
    view_->setCrossYX(newCrossYX);

    // Update spinboxes
    {
        QSignalBlocker b1(scaleXSpin_);
        QSignalBlocker b2(scaleYSpin_);
        QSignalBlocker b3(deltaXSpin_);
        QSignalBlocker b4(deltaYSpin_);

        scaleXSpin_->setValue(int(newScaleX));
        scaleYSpin_->setValue(int(newScaleY));
        deltaXSpin_->setValue(int(newDeltaX));
        deltaYSpin_->setValue(int(newDeltaY));
    }

    // Save to JSON only when applying pins
    updateCurrentInMap();
    saveJsonlFile();

    // New saved state = after applying pins
    savedState_ = currentState();
    savedStateValid_ = true;
    if (restoreAction_) restoreAction_->setEnabled(true);

    dirty_ = false;
    updateInfoLabel();

    QString fitText = QString("n=%1 RMS=%2 max=%3 px @pin#%4")
            .arg(activePins.size())
            .arg(rms, 0, 'f', 3)
            .arg(maxAbs, 0, 'f', 3)
            .arg(maxAbsIdx + 1);
    if (fitInfoLabel_) fitInfoLabel_->setText(fitText);
    return rms;
}

void AlignMainWindow::onQuadXToggled(bool checked)
{
    if (lastAppliedPins_.isEmpty()) {
        statusBar()->showMessage("X²: no previous pins – apply pins first", 3000);
        return;
    }
    bool useQY = quadYCheck_ && quadYCheck_->isChecked();
    bool useR  = rotCheck_   && rotCheck_->isChecked();
    bool useXY = xyCheck_    && xyCheck_->isChecked();
    bool rejected = false;
    computeAndApplyFromPins(lastAppliedPins_, checked, useQY, useR, useXY,
                            true, nullptr, nullptr, &rejected);
    if (rejected && quadXCheck_) {
        QSignalBlocker b(quadXCheck_);
        quadXCheck_->setChecked(!checked);
    }
    updateCheckboxesEnabled();
}

void AlignMainWindow::onQuadYToggled(bool checked)
{
    if (lastAppliedPins_.isEmpty()) {
        statusBar()->showMessage("Y²: no previous pins – apply pins first", 3000);
        return;
    }
    bool useQX = quadXCheck_ && quadXCheck_->isChecked();
    bool useR  = rotCheck_   && rotCheck_->isChecked();
    bool useXY = xyCheck_    && xyCheck_->isChecked();
    bool rejected = false;
    computeAndApplyFromPins(lastAppliedPins_, useQX, checked, useR, useXY,
                            true, nullptr, nullptr, &rejected);
    if (rejected && quadYCheck_) {
        QSignalBlocker b(quadYCheck_);
        quadYCheck_->setChecked(!checked);
    }
    updateCheckboxesEnabled();
}

void AlignMainWindow::onRotToggled(bool checked)
{
    if (lastAppliedPins_.isEmpty()) {
        statusBar()->showMessage("R: no previous pins – apply pins first", 3000);
        return;
    }
    bool useQX = quadXCheck_ && quadXCheck_->isChecked();
    bool useQY = quadYCheck_ && quadYCheck_->isChecked();
    bool useXY = xyCheck_    && xyCheck_->isChecked();
    bool rejected = false;
    computeAndApplyFromPins(lastAppliedPins_, useQX, useQY, checked, useXY,
                            true, nullptr, nullptr, &rejected);
    if (rejected && rotCheck_) {
        QSignalBlocker b(rotCheck_);
        rotCheck_->setChecked(!checked);
    }
    updateCheckboxesEnabled();
}

void AlignMainWindow::onXyToggled(bool checked)
{
    if (lastAppliedPins_.isEmpty()) {
        statusBar()->showMessage("XY: no previous pins – apply pins first", 3000);
        return;
    }
    bool useQX = quadXCheck_ && quadXCheck_->isChecked();
    bool useQY = quadYCheck_ && quadYCheck_->isChecked();
    bool useR  = rotCheck_   && rotCheck_->isChecked();
    bool rejected = false;
    computeAndApplyFromPins(lastAppliedPins_, useQX, useQY, useR, checked,
                            true, nullptr, nullptr, &rejected);
    if (rejected && xyCheck_) {
        QSignalBlocker b(xyCheck_);
        xyCheck_->setChecked(!checked);
    }
    updateCheckboxesEnabled();
}

void AlignMainWindow::runOptimalMode()
{
    if (lastAppliedPins_.isEmpty()) {
        statusBar()->showMessage("Optimal: no previous pins – apply pins first", 3000);
        return;
    }

    struct Cand {
        int rank;
        double maxAbs;
        int maxAbsPin;
        double rounded;        // maxAbs rounded to full pixel
        double rms;
        bool qX, qY, rR, xY;
    };
    QVector<Cand> cands;
    cands.reserve(16);

    for (int mask = 0; mask < 16; ++mask) {
        bool qX = (mask & 1) != 0;
        bool qY = (mask & 2) != 0;
        bool rR = (mask & 4) != 0;
        bool xY = (mask & 8) != 0;
        double maxAbs = 0.0;
        int maxPin = 0;
        double rms = computeAndApplyFromPins(lastAppliedPins_, qX, qY, rR, xY,
                                              /*applyResult=*/false, &maxAbs, &maxPin);
        int rank = (xY ? 8 : 0) + (rR ? 4 : 0) + (qY ? 2 : 0) + (qX ? 1 : 0);
        double rounded = std::round(maxAbs);     // full pixel – more combinations in tie
        cands.append({rank, maxAbs, maxPin, rounded, rms, qX, qY, rR, xY});
    }

    // First minimize rounded, break ties by smallest rank
    double minRounded = cands[0].rounded;
    for (const Cand& c : cands) if (c.rounded < minRounded) minRounded = c.rounded;

    const Cand* best = nullptr;
    int bestRank = std::numeric_limits<int>::max();
    for (const Cand& c : cands) {
        if (c.rounded == minRounded && c.rank < bestRank) {
            best = &c;
            bestRank = c.rank;
        }
    }
    if (!best) best = &cands[0];

    // Set checkboxes without triggering refit from signal
    {
        QSignalBlocker b1(quadXCheck_);
        QSignalBlocker b2(quadYCheck_);
        QSignalBlocker b3(rotCheck_);
        QSignalBlocker b4(xyCheck_);
        if (quadXCheck_) quadXCheck_->setChecked(best->qX);
        if (quadYCheck_) quadYCheck_->setChecked(best->qY);
        if (rotCheck_)   rotCheck_->setChecked(best->rR);
        if (xyCheck_)    xyCheck_->setChecked(best->xY);
    }

    // Apply winning combination (saves JSONL and updates fitInfoLabel_)
    computeAndApplyFromPins(lastAppliedPins_, best->qX, best->qY, best->rR, best->xY, /*applyResult=*/true);

    statusBar()->showMessage(
        QString("Optimal: X²=%1 Y²=%2 R=%3 XY=%4 | max=%5 px @pin#%6 (round %7)")
            .arg(best->qX ? "1" : "0")
            .arg(best->qY ? "1" : "0")
            .arg(best->rR ? "1" : "0")
            .arg(best->xY ? "1" : "0")
            .arg(best->maxAbs, 0, 'f', 3)
            .arg(best->maxAbsPin)
            .arg(best->rounded, 0, 'f', 0),
        10000
    );
}

void AlignMainWindow::updateDoneButton(bool done)
{
    if (!doneBtn_) return;
    QSignalBlocker b(doneBtn_);
    doneBtn_->setChecked(done);
    if (done) {
        doneBtn_->setText("Done ✓");
        doneBtn_->setStyleSheet(
            "QPushButton{background:#c8e6c9;font-weight:bold;}");
    } else {
        doneBtn_->setText("Not done");
        doneBtn_->setStyleSheet(
            "QPushButton{background:#ffe0b2;}");
    }
}
