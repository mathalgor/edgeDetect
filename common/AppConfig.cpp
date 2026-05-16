#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

QString AppConfig::configPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString name = QCoreApplication::applicationName();
    return QDir(dir).filePath(name + ".json");
}

bool AppConfig::load()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const auto o = doc.object();
    currentProjectPath = o.value("currentProjectPath").toString();
    recentProjects.clear();
    for (const auto& v : o.value("recentProjects").toArray()) {
        const QString s = v.toString();
        if (!s.isEmpty()) recentProjects.append(s);
    }
    if (o.contains("mruSize")) mruSize = std::max(1, o.value("mruSize").toInt(20));
    return true;
}

bool AppConfig::save() const
{
    QJsonObject o;
    o["currentProjectPath"] = currentProjectPath;
    QJsonArray a;
    for (const auto& s : recentProjects) a.append(s);
    o["recentProjects"] = a;
    o["mruSize"] = mruSize;
    QDir().mkpath(QFileInfo(configPath()).absolutePath());
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

void AppConfig::addRecent(const QString& path)
{
    if (path.isEmpty()) return;
    recentProjects.removeAll(path);
    recentProjects.prepend(path);
    while (recentProjects.size() > mruSize) recentProjects.removeLast();
}
