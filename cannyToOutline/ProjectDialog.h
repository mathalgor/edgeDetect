#ifndef PROJECT_DIALOG_H
#define PROJECT_DIALOG_H

#include <QDialog>
#include "ProjectConfig.h"

class QLineEdit;

class ProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectDialog(const ProjectConfig& cfg, QWidget* parent = nullptr);
    ProjectConfig config() const;

private:
    QLineEdit* srcEdit_;
    QLineEdit* outEdit_;
    QLineEdit* refEdit_;
    QLineEdit* origEdit_;
    void browseFor(QLineEdit* le, const QString& title);
};

#endif
