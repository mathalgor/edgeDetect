#include "ProjectDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

ProjectDialog::ProjectDialog(const ProjectConfig& cfg, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Set project");
    setModal(true);

    srcEdit_  = new QLineEdit(cfg.sourceDir, this);
    outEdit_  = new QLineEdit(cfg.outputDir, this);
    o1Edit_   = new QLineEdit(cfg.outlines1Dir, this);
    o2Edit_   = new QLineEdit(cfg.outlines2Dir, this);
    origEdit_ = new QLineEdit(cfg.originalDir, this);

    auto* srcBtn  = new QPushButton("Browse...", this);
    auto* outBtn  = new QPushButton("Browse...", this);
    auto* o1Btn   = new QPushButton("Browse...", this);
    auto* o2Btn   = new QPushButton("Browse...", this);
    auto* origBtn = new QPushButton("Browse...", this);

    connect(srcBtn,  &QPushButton::clicked, this,
            [this]{ browseFor(srcEdit_, "Source dir (multi-canny gray)"); });
    connect(outBtn,  &QPushButton::clicked, this,
            [this]{ browseFor(outEdit_, "Output dir (resulting outline)"); });
    connect(o1Btn,   &QPushButton::clicked, this,
            [this]{ browseFor(o1Edit_, "Outlines 1"); });
    connect(o2Btn,   &QPushButton::clicked, this,
            [this]{ browseFor(o2Edit_, "Outlines 2"); });
    connect(origBtn, &QPushButton::clicked, this,
            [this]{ browseFor(origEdit_, "Original (photo) — optional"); });

    auto* row1 = new QHBoxLayout; row1->addWidget(srcEdit_, 1);  row1->addWidget(srcBtn);
    auto* row2 = new QHBoxLayout; row2->addWidget(outEdit_, 1);  row2->addWidget(outBtn);
    auto* row3 = new QHBoxLayout; row3->addWidget(o1Edit_, 1);   row3->addWidget(o1Btn);
    auto* row4 = new QHBoxLayout; row4->addWidget(o2Edit_, 1);   row4->addWidget(o2Btn);
    auto* row5 = new QHBoxLayout; row5->addWidget(origEdit_, 1); row5->addWidget(origBtn);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(10, 10, 10, 10);
    form->addRow("Source (multi-canny gray):", row1);
    form->addRow("Output (result):",           row2);
    form->addRow("Outlines 1:",                row3);
    form->addRow("Outlines 2:",                row4);
    form->addRow("Original (opt.):",           row5);
    form->addRow(bb);

    resize(640, sizeHint().height());
}

void ProjectDialog::browseFor(QLineEdit* le, const QString& title)
{
    const QString d = QFileDialog::getExistingDirectory(
        this, title, le->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (!d.isEmpty()) le->setText(d);
}

ProjectConfig ProjectDialog::config() const
{
    ProjectConfig c;
    c.sourceDir    = srcEdit_->text().trimmed();
    c.outputDir    = outEdit_->text().trimmed();
    c.outlines1Dir = o1Edit_->text().trimmed();
    c.outlines2Dir = o2Edit_->text().trimmed();
    c.originalDir  = origEdit_->text().trimmed();
    return c;
}