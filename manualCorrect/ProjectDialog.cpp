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

    origEdit_     = new QLineEdit(cfg.originalDir, this);
    outlinesEdit_ = new QLineEdit(cfg.outlinesDir, this);
    outputEdit_   = new QLineEdit(cfg.outputDir, this);

    auto* origBtn     = new QPushButton("Browse...", this);
    auto* outlinesBtn = new QPushButton("Browse...", this);
    auto* outputBtn   = new QPushButton("Browse...", this);

    connect(origBtn,     &QPushButton::clicked, this,
            [this]{ browseFor(origEdit_, "Original RGB dir (optional)"); });
    connect(outlinesBtn, &QPushButton::clicked, this,
            [this]{ browseFor(outlinesEdit_, "Input outlines dir (with .dbgrg.png siblings)"); });
    connect(outputBtn,   &QPushButton::clicked, this,
            [this]{ browseFor(outputEdit_, "Output dir (created if missing)"); });

    auto* row1 = new QHBoxLayout; row1->addWidget(origEdit_, 1);     row1->addWidget(origBtn);
    auto* row2 = new QHBoxLayout; row2->addWidget(outlinesEdit_, 1); row2->addWidget(outlinesBtn);
    auto* row3 = new QHBoxLayout; row3->addWidget(outputEdit_, 1);   row3->addWidget(outputBtn);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(10, 10, 10, 10);
    form->addRow("Original (opt.):", row1);
    form->addRow("Outlines (in):",   row2);
    form->addRow("Output:",          row3);
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
    c.originalDir = origEdit_->text().trimmed();
    c.outlinesDir = outlinesEdit_->text().trimmed();
    c.outputDir   = outputEdit_->text().trimmed();
    return c;
}