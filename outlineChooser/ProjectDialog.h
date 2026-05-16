#ifndef OC_PROJECT_DIALOG_H
#define OC_PROJECT_DIALOG_H

#include <QDialog>

#include "ProjectConfig.h"

class QLineEdit;

class ProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectDialog(const ProjectConfig& cfg, QWidget* parent = nullptr);
    ProjectConfig config() const;

private:
    void browseFor(QLineEdit* le, const QString& title);
    QLineEdit* srcEdit_;
    QLineEdit* outEdit_;
    QLineEdit* o1Edit_;
    QLineEdit* o2Edit_;
    QLineEdit* origEdit_;
};

#endif