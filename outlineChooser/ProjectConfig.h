#ifndef OC_PROJECT_CONFIG_H
#define OC_PROJECT_CONFIG_H

#include <QString>

struct ProjectConfig {
    QString sourceDir;       // multi-canny gray
    QString outputDir;       // resulting outline (may be empty at start)
    QString outlines1Dir;
    QString outlines2Dir;
    QString originalDir;     // optional

    bool isValid() const;
    bool load(const QString& path);
    bool save(const QString& path) const;
};

#endif