#ifndef MC_PROJECT_CONFIG_H
#define MC_PROJECT_CONFIG_H

#include <QString>

struct ProjectConfig {
    QString originalDir;     // optional — original RGB images
    QString outlinesDir;     // required — input outlines (with sibling .dbgrg.png)
    QString outputDir;       // required — corrected outlines; created if missing

    bool isValid() const;
    QString validationError() const;
    bool load(const QString& path);
    bool save(const QString& path) const;
};

#endif