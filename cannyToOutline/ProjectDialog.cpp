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

    srcEdit_ = new QLineEdit(cfg.sourceDir, this);
    outEdit_ = new QLineEdit(cfg.outputDir, this);
    refEdit_ = new QLineEdit(cfg.referenceDir, this);
    origEdit_ = new QLineEdit(cfg.originalDir, this);

    auto* srcBtn = new QPushButton("Browse...", this);
    auto* outBtn = new QPushButton("Browse...", this);
    auto* refBtn = new QPushButton("Browse...", this);
    auto* origBtn = new QPushButton("Browse...", this);

    connect(srcBtn, &QPushButton::clicked, this,
            [this]{ browseFor(srcEdit_, "Source dir (multi-canny gray)"); });
    connect(outBtn, &QPushButton::clicked, this,
            [this]{ browseFor(outEdit_, "Output dir (outline)"); });
    connect(refBtn, &QPushButton::clicked, this,
            [this]{ browseFor(refEdit_, "Reference dir (fit) — optional"); });
    connect(origBtn, &QPushButton::clicked, this,
            [this]{ browseFor(origEdit_, "Original dir (color/gray photo) — optional"); });

    auto* row1 = new QHBoxLayout; row1->addWidget(srcEdit_, 1); row1->addWidget(srcBtn);
    auto* row2 = new QHBoxLayout; row2->addWidget(outEdit_, 1); row2->addWidget(outBtn);
    auto* row3 = new QHBoxLayout; row3->addWidget(refEdit_, 1); row3->addWidget(refBtn);
    auto* row4 = new QHBoxLayout; row4->addWidget(origEdit_, 1); row4->addWidget(origBtn);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(10, 10, 10, 10);
    form->addRow("Source (multi-canny gray):", row1);
    form->addRow("Output (outline):",          row2);
    form->addRow("Reference (fit, opt.):",     row3);
    form->addRow("Original (photo, opt.):",    row4);
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
    c.referenceDir = refEdit_->text().trimmed();
    c.originalDir  = origEdit_->text().trimmed();
    return c;
}
