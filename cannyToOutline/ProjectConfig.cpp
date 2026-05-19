#include "ProjectConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

bool ProjectConfig::isValid() const
{
    return validationError().isEmpty();
}

QString ProjectConfig::validationError() const
{
    if (sourceDir.isEmpty()) return QStringLiteral("sourceDir is not set");
    if (!QDir(sourceDir).exists())
        return QStringLiteral("sourceDir does not exist: %1").arg(sourceDir);
    if (outputDir.isEmpty()) return QStringLiteral("outputDir is not set");
    if (!QDir(outputDir).exists() && !QDir().mkpath(outputDir))
        return QStringLiteral("outputDir cannot be created: %1").arg(outputDir);
    return {};
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
    referenceDir = o.value("referenceDir").toString();
    originalDir  = o.value("originalDir").toString();
    return true;
}

bool ProjectConfig::save(const QString& path) const
{
    QJsonObject o;
    o["sourceDir"]    = sourceDir;
    o["outputDir"]    = outputDir;
    o["referenceDir"] = referenceDir;
    o["originalDir"]  = originalDir;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}