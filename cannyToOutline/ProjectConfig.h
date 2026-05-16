#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <QString>

struct ProjectConfig {
    QString sourceDir;
    QString outputDir;
    QString referenceDir;   // optional
    QString originalDir;    // optional — original images (color or gray, source of multi-canny)

    bool isValid() const;
    bool load(const QString& path);
    bool save(const QString& path) const;
};

#endif