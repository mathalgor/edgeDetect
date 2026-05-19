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
    if (outlinesDir.isEmpty()) return QStringLiteral("outlinesDir is not set");
    if (!QDir(outlinesDir).exists())
        return QStringLiteral("outlinesDir does not exist: %1").arg(outlinesDir);
    if (outputDir.isEmpty()) return QStringLiteral("outputDir is not set");
    if (!QDir(outputDir).exists() && !QDir().mkpath(outputDir))
        return QStringLiteral("outputDir cannot be created: %1").arg(outputDir);
    if (!originalDir.isEmpty() && !QDir(originalDir).exists())
        return QStringLiteral("originalDir does not exist: %1").arg(originalDir);
    return {};
}

bool ProjectConfig::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const auto o = doc.object();
    originalDir = o.value("originalDir").toString();
    outlinesDir = o.value("outlinesDir").toString();
    outputDir   = o.value("outputDir").toString();
    return true;
}

bool ProjectConfig::save(const QString& path) const
{
    QJsonObject o;
    o["originalDir"] = originalDir;
    o["outlinesDir"] = outlinesDir;
    o["outputDir"]   = outputDir;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}