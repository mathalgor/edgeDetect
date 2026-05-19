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
    // outputDir is required but does not need to exist yet — it's created
    // on first save. The three input directories must exist.
    if (sourceDir.isEmpty()) return QStringLiteral("sourceDir is not set");
    if (!QDir(sourceDir).exists())
        return QStringLiteral("sourceDir does not exist: %1").arg(sourceDir);
    if (outlines1Dir.isEmpty()) return QStringLiteral("outlines1Dir is not set");
    if (!QDir(outlines1Dir).exists())
        return QStringLiteral("outlines1Dir does not exist: %1").arg(outlines1Dir);
    if (outlines2Dir.isEmpty()) return QStringLiteral("outlines2Dir is not set");
    if (!QDir(outlines2Dir).exists())
        return QStringLiteral("outlines2Dir does not exist: %1").arg(outlines2Dir);
    if (outputDir.isEmpty()) return QStringLiteral("outputDir is not set");
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