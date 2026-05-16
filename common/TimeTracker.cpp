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
    const QByteArray hash = QCryptographicHash::hash(
        projectAbsolutePath.toUtf8(), QCryptographicHash::Md5).toHex();
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    const QString dir = QDir(base).filePath("projects");
    return QDir(dir).filePath(stem + "." + QString::fromLatin1(hash.left(8))
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
    lastActivity_ = QDateTime::currentDateTime();
}

qint64 TimeTracker::secondsFor(const QString& name) const
{
    auto it = entries_.find(name);
    return it == entries_.end() ? 0 : it.value().seconds;
}

qint64 TimeTracker::totalSeconds() const
{
    qint64 t = 0;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        t += it.value().seconds;
    }
    return t;
}

bool TimeTracker::isDone(const QString& name) const
{
    auto it = entries_.find(name);
    return it != entries_.end() && it.value().done;
}

void TimeTracker::setDone(const QString& name, bool on)
{
    if (name.isEmpty()) return;
    Entry& e = entries_[name];
    if (e.done == on) return;
    e.done = on;
    dirty_ = true;
    emit doneChanged(name, on);
}

void TimeTracker::onTick()
{
    if (currentFile_.isEmpty()) return;
    if (lastActivity_.isValid()) {
        const qint64 sinceMs = lastActivity_.msecsTo(QDateTime::currentDateTime());
        if (sinceMs >= 0 && sinceMs < qint64(idleSeconds_) * 1000) {
            entries_[currentFile_].seconds += 1;
            dirty_ = true;
        }
    }
    emit tick(currentFile_, secondsFor(currentFile_));
}

void TimeTracker::onFlushTimer()
{
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
