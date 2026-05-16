#include "ProjectConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

bool ProjectConfig::isValid() const
{
    return !sourceDir.isEmpty()    && QDir(sourceDir).exists()
        && !outlines1Dir.isEmpty() && QDir(outlines1Dir).exists()
        && !outlines2Dir.isEmpty() && QDir(outlines2Dir).exists();
}

bool ProjectConfig::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const auto o = doc.object();
    sourceDir    = o.value("sourceDir").toString();
    outputDir    = o.value("outputDir").toString();
    outlines1Dir = o.value("outlines1Dir").toString();
    outlines2Dir = o.value("outlines2Dir").toString();
    originalDir  = o.value("originalDir").toString();
    return true;
}

bool ProjectConfig::save(const QString& path) const
{
    QJsonObject o;
    o["sourceDir"]    = sourceDir;
    o["outputDir"]    = outputDir;
    o["outlines1Dir"] = outlines1Dir;
    o["outlines2Dir"] = outlines2Dir;
    o["originalDir"]  = originalDir;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}