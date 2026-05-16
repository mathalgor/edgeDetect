#include "ProjectTimeDialog.h"

#include "TimeTracker.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

ProjectTimeDialog::ProjectTimeDialog(TimeTracker* tracker,
                                     const QStringList& fileList,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Project time");
    setModal(true);
    resize(560, 480);

    // Fold the in-flight running window into committed totals before we
    // read them, so the report matches what the status bar shows.
    if (tracker) tracker->commitNow();

    const auto snap = tracker ? tracker->snapshot()
                              : QHash<QString, TimeTracker::FileStat>();

    qint64 totalAll  = 0;
    qint64 totalDone = 0;
    int    nDone     = 0;
    int    nTotal    = fileList.size();
    int    nKnown    = 0;
    for (const auto& name : fileList) {
        auto it = snap.find(name);
        if (it == snap.end()) continue;
        nKnown += 1;
        totalAll += it.value().seconds;
        if (it.value().done) {
            nDone += 1;
            totalDone += it.value().seconds;
        }
    }
    const int nRemaining = std::max(0, nTotal - nDone);

    QString estimateStr = "—";
    if (nDone > 0 && nRemaining > 0) {
        const qint64 avg = totalDone / nDone;
        estimateStr = TimeTracker::formatHMS(avg * nRemaining);
    } else if (nDone == 0) {
        estimateStr = "— (no done files yet)";
    } else {
        estimateStr = TimeTracker::formatHMS(0);
    }

    auto* root = new QVBoxLayout(this);

    auto* total = new QLabel(QString("<b>Total active time:</b> %1")
                                 .arg(TimeTracker::formatHMS(totalAll)));
    auto* counts = new QLabel(QString("<b>Files:</b> %1 done / %2 total "
                                      "(%3 tracked)")
                                  .arg(nDone).arg(nTotal).arg(nKnown));
    auto* est = new QLabel(QString("<b>Estimated remaining:</b> %1")
                               .arg(estimateStr));
    est->setToolTip("Average seconds per done file × number of not-done files");
    root->addWidget(total);
    root->addWidget(counts);
    root->addWidget(est);

    auto* table = new QTableWidget(nTotal, 3, this);
    table->setHorizontalHeaderLabels({"File", "Time", "Done"});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    for (int i = 0; i < nTotal; ++i) {
        const QString& name = fileList[i];
        auto it = snap.find(name);
        const qint64 secs = (it == snap.end()) ? 0 : it.value().seconds;
        const bool   done = (it == snap.end()) ? false : it.value().done;

        auto* nameItem = new QTableWidgetItem(name);
        auto* timeItem = new QTableWidgetItem(TimeTracker::formatHMS(secs));
        timeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* doneItem = new QTableWidgetItem(done ? "✓" : QString());
        doneItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 0, nameItem);
        table->setItem(i, 1, timeItem);
        table->setItem(i, 2, doneItem);
    }
    root->addWidget(table, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(bb);
}
