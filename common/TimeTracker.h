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
// Counts seconds tick-based: every 1 s the tracker checks whether the last
// activity was within `idleSeconds_` of "now"; if so, +1 s is added to the
// current file. A long break (idleSeconds_+ without activity) is therefore
// excluded automatically, with the idleSeconds_ window itself counted as a
// tolerance buffer.
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

    qint64 secondsFor(const QString& name) const;
    qint64 totalSeconds() const;
    bool   isDone(const QString& name) const;
    void   setDone(const QString& name, bool on);

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
