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
    if (grayDir.isEmpty())    return QStringLiteral("grayDir is not set");
    if (!QDir(grayDir).exists())
        return QStringLiteral("grayDir does not exist: %1").arg(grayDir);
    if (outlineDir.isEmpty()) return QStringLiteral("outlineDir is not set");
    if (!QDir(outlineDir).exists())
        return QStringLiteral("outlineDir does not exist: %1").arg(outlineDir);
    return {};
}

bool ProjectConfig::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const auto o = doc.object();
    grayDir    = o.value("grayDir").toString();
    outlineDir = o.value("outlineDir").toString();
    return true;
}

bool ProjectConfig::save(const QString& path) const
{
    QJsonObject o;
    o["grayDir"]    = grayDir;
    o["outlineDir"] = outlineDir;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}