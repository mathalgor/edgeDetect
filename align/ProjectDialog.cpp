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

    grayEdit_    = new QLineEdit(cfg.grayDir, this);
    outlineEdit_ = new QLineEdit(cfg.outlineDir, this);

    auto* grayBtn    = new QPushButton("Browse...", this);
    auto* outlineBtn = new QPushButton("Browse...", this);

    connect(grayBtn, &QPushButton::clicked, this,
            [this]{ browseFor(grayEdit_, "Gray (source) dir"); });
    connect(outlineBtn, &QPushButton::clicked, this,
            [this]{ browseFor(outlineEdit_, "Outline (target) dir"); });

    auto* row1 = new QHBoxLayout; row1->addWidget(grayEdit_, 1);    row1->addWidget(grayBtn);
    auto* row2 = new QHBoxLayout; row2->addWidget(outlineEdit_, 1); row2->addWidget(outlineBtn);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(10, 10, 10, 10);
    form->addRow("Origin dir:",    row1);
    form->addRow("Outline dir:", row2);
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
    c.grayDir    = grayEdit_->text().trimmed();
    c.outlineDir = outlineEdit_->text().trimmed();
    return c;
}