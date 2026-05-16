#ifndef EDGEDETECT_PROJECT_TIME_DIALOG_H
#define EDGEDETECT_PROJECT_TIME_DIALOG_H

#include <QDialog>
#include <QStringList>

class TimeTracker;

// Modal report dialog: total active project time, per-file table
// (name / time / done), and an estimate of remaining time based on the
// average per-done-file time times the count of not-yet-done files in
// the supplied filename list.
class ProjectTimeDialog : public QDialog {
    Q_OBJECT
public:
    ProjectTimeDialog(TimeTracker* tracker,
                      const QStringList& fileList,
                      QWidget* parent = nullptr);
};

#endif
