#include "TimeTracker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

TimeTracker::TimeTracker(QObject* parent) : QObject(parent)
{
    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(1000);
    connect(tickTimer_, &QTimer::timeout, this, &TimeTracker::onTick);

    flushTimer_ = new QTimer(this);
    flushTimer_->setInterval(60 * 1000);
    connect(flushTimer_, &QTimer::timeout, this, &TimeTracker::onFlushTimer);
}

TimeTracker::~TimeTracker()
{
    flush();
}

void TimeTracker::setIdleSeconds(int s)
{
    idleSeconds_ = std::max(5, s);
}

QString TimeTracker::jsonPathFor(const QString& projectAbsolutePath) const
{
    const QString stem = QFileInfo(projectAbsolutePath).completeBaseName();

    // Hash the project's content (canonicalized as compact JSON when possible)
    // so the times-file ID follows what the project means, not where it lives.
    QByteArray payload;
    QFile pf(projectAbsolutePath);
    if (pf.open(QIODevice::ReadOnly)) {
        const QByteArray raw = pf.readAll();
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        payload = (err.error == QJsonParseError::NoError)
                      ? doc.toJson(QJsonDocument::Compact)
                      : raw;
    } else {
        payload = projectAbsolutePath.toUtf8();
    }
    const QByteArray hash = QCryptographicHash::hash(
        payload, QCryptographicHash::Md5).toHex();

    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    return QDir(base).filePath(stem + "." + QString::fromLatin1(hash.left(8))
                               + ".times.json");
}

void TimeTracker::bindToProject(const QString& projectAbsolutePath)
{
    flush();
    tickTimer_->stop();
    flushTimer_->stop();
    entries_.clear();
    currentFile_.clear();
    lastActivity_ = {};
    projectPath_ = projectAbsolutePath;
    jsonPath_.clear();
    if (projectPath_.isEmpty()) return;
    jsonPath_ = jsonPathFor(projectPath_);
    load();
    tickTimer_->start();
    flushTimer_->start();
}

void TimeTracker::setCurrentFile(const QString& name)
{
    if (name == currentFile_) return;
    commitNow();   // fold any in-flight gap into the file we're leaving
    flush();
    currentFile_ = name;
    // Don't auto-bump lastActivity_; require a real user event first so
    // we don't count time on a freshly-opened file the user is just
    // looking at.
    lastActivity_ = {};
    emit tick(currentFile_, secondsFor(currentFile_));
}

void TimeTracker::registerActivity()
{
    // Fold the gap that just ended into the committed total (if valid),
    // then anchor the running window to "now".
    commitNow();
    lastActivity_ = QDateTime::currentDateTime();
}

void TimeTracker::commitNow()
{
    if (currentFile_.isEmpty() || !lastActivity_.isValid()) return;
    const QDateTime now = QDateTime::currentDateTime();
    const auto it = entries_.find(currentFile_);
    const bool done = (it != entries_.end() && it.value().done);
    if (!done) {
        const qint64 gapMs = lastActivity_.msecsTo(now);
        if (gapMs >= 0 && gapMs <= qint64(idleSeconds_) * 1000) {
            // Within idle window — commit the gap as active time. Round to
            // nearest whole second so the display matches the saved value.
            entries_[currentFile_].seconds += (gapMs + 500) / 1000;
            dirty_ = true;
        }
    }
    // Either way: anchor "now" so the next gap is measured from here.
    lastActivity_ = now;
}

qint64 TimeTracker::secondsFor(const QString& name) const
{
    auto it = entries_.find(name);
    return it == entries_.end() ? 0 : it.value().seconds;
}

qint64 TimeTracker::liveSecondsFor(const QString& name) const
{
    qint64 base = secondsFor(name);
    if (name != currentFile_ || !lastActivity_.isValid()) return base;
    if (isDone(name)) return base;
    const qint64 gapMs = lastActivity_.msecsTo(QDateTime::currentDateTime());
    if (gapMs >= 0 && gapMs <= qint64(idleSeconds_) * 1000) {
        base += gapMs / 1000;
    }
    return base;
}

qint64 TimeTracker::totalSeconds() const
{
    qint64 t = 0;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        t += it.value().seconds;
    }
    return t;
}

QHash<QString, TimeTracker::FileStat> TimeTracker::snapshot() const
{
    QHash<QString, FileStat> out;
    out.reserve(entries_.size());
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        out.insert(it.key(), { it.value().seconds, it.value().done });
    }
    return out;
}

bool TimeTracker::isDone(const QString& name) const
{
    auto it = entries_.find(name);
    return it != entries_.end() && it.value().done;
}

void TimeTracker::setDone(const QString& name, bool on)
{
    if (name.isEmpty()) return;
    // When marking the current file Done, commit any in-flight gap so it
    // counts. When un-marking, restart the anchor so the idle gap accrued
    // while it was Done is not back-filled.
    if (name == currentFile_) {
        if (on) commitNow();
        else    lastActivity_ = {};
    }
    Entry& e = entries_[name];
    if (e.done == on) return;
    e.done = on;
    dirty_ = true;
    emit doneChanged(name, on);
    if (name == currentFile_) emit tick(currentFile_, liveSecondsFor(currentFile_));
}

void TimeTracker::onTick()
{
    // The display value is purely derived: committed + a running window
    // capped at idleSeconds_. Entries are only mutated in commitNow().
    emit tick(currentFile_, liveSecondsFor(currentFile_));
}

void TimeTracker::onFlushTimer()
{
    commitNow();    // fold in-flight gap before persisting
    if (dirty_) flush();
}

void TimeTracker::load()
{
    entries_.clear();
    QFile f(jsonPath_);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const auto root = doc.object();
    if (root.contains("idleSeconds"))
        idleSeconds_ = std::max(5, root.value("idleSeconds").toInt(idleSeconds_));
    const auto files = root.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto o = it.value().toObject();
        Entry e;
        e.seconds = o.value("seconds").toVariant().toLongLong();
        e.done    = o.value("done").toBool();
        entries_.insert(it.key(), e);
    }
}

void TimeTracker::save()
{
    if (jsonPath_.isEmpty()) return;
    QDir().mkpath(QFileInfo(jsonPath_).absolutePath());
    QJsonObject root;
    root["projectPath"] = projectPath_;
    root["idleSeconds"] = idleSeconds_;
    QJsonObject files;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        QJsonObject o;
        o["seconds"] = double(it.value().seconds);
        o["done"]    = it.value().done;
        files.insert(it.key(), o);
    }
    root["files"] = files;
    QFile f(jsonPath_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void TimeTracker::flush()
{
    commitNow();    // ensure persisted state reflects the in-flight gap
    if (!dirty_) return;
    save();
    dirty_ = false;
}

QString TimeTracker::formatHMS(qint64 seconds)
{
    if (seconds < 0) seconds = 0;
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    if (h > 0) return QString("%1:%2:%3").arg(h)
                        .arg(m, 2, 10, QChar('0'))
                        .arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0'))
                           .arg(s, 2, 10, QChar('0'));
}
