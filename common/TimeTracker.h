#ifndef EDGEDETECT_TIME_TRACKER_H
#define EDGEDETECT_TIME_TRACKER_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

class QTimer;

// Per-file active-time accounting for a single project.
//
// Activity is bumped by the host calling registerActivity() from an event
// filter that catches key presses, mouse button presses and mouse wheel —
// NOT plain mouse moves.
//
// Counting model — "commit gap on event, revert on overflow":
//   Per file, Entry::seconds is the committed total (idle gaps already
//   excluded). A "running gap" — wall-clock since the last activity — is
//   added to the display every 1 s for the current file, BUT only while
//   the gap is within `idleSeconds_`. When the gap exceeds the idle
//   threshold the display snaps back to the committed total: those
//   over-counted seconds are dropped, never persisted.
//   On the next activity, if the gap so far was within the idle window,
//   it is committed (added to Entry::seconds) and the window resets; if
//   the gap exceeded the window, it is dropped entirely.
//
// State (per file: accumulated seconds + done flag) persists to
// $XDG_CONFIG_HOME/edgeDetect/<app>/projects/<stem>.<hash>.times.json.
// The host calls bindToProject() once per project and setCurrentFile()
// whenever the active source changes. Flush is automatic (60 s timer +
// on every switch + on destruction).
class TimeTracker : public QObject {
    Q_OBJECT
public:
    explicit TimeTracker(QObject* parent = nullptr);
    ~TimeTracker() override;

    void setIdleSeconds(int s);
    int  idleSeconds() const { return idleSeconds_; }

    // Loads/creates the side JSON for this project. Pass an absolute path
    // to the *.ctoprj / *.ocproj file. Passing an empty string detaches.
    void bindToProject(const QString& projectAbsolutePath);
    QString projectPath() const { return projectPath_; }

    void setCurrentFile(const QString& name);
    QString currentFile() const { return currentFile_; }

    void registerActivity();

    // Folds any in-flight valid gap into the current file's committed
    // seconds and resets the running window. Call before reading the
    // committed value for reporting (the report dialog does this).
    void commitNow();

    qint64 secondsFor(const QString& name) const;        // committed only
    qint64 liveSecondsFor(const QString& name) const;    // committed + running
    qint64 totalSeconds() const;                         // sum of committed
    bool   isDone(const QString& name) const;
    void   setDone(const QString& name, bool on);

    // Read-only access for the report dialog.
    struct FileStat { qint64 seconds = 0; bool done = false; };
    QHash<QString, FileStat> snapshot() const;

    // Writes the JSON to disk. Called automatically by the periodic flush
    // timer and from setCurrentFile / bindToProject / destructor.
    void flush();

    // Formats a seconds value as "H:MM:SS" if hours > 0, else "MM:SS".
    static QString formatHMS(qint64 seconds);

signals:
    // Emitted every 1 s (regardless of idle) with the current file's total.
    // The host typically uses this to update a status-bar label.
    void tick(const QString& currentFile, qint64 seconds);
    // Emitted after setDone() changes state.
    void doneChanged(const QString& name, bool on);

private slots:
    void onTick();
    void onFlushTimer();

private:
    QString jsonPathFor(const QString& projectAbsolutePath) const;
    void load();
    void save();

    struct Entry {
        qint64 seconds = 0;
        bool   done = false;
    };

    int  idleSeconds_ = 60;
    QString projectPath_;       // absolute path to the project file
    QString jsonPath_;          // absolute path to our side JSON
    QString currentFile_;       // current source file name (not path)
    QHash<QString, Entry> entries_;
    QDateTime lastActivity_;
    QTimer* tickTimer_  = nullptr;
    QTimer* flushTimer_ = nullptr;
    bool    dirty_ = false;     // unsaved changes since last flush
};

#endif
