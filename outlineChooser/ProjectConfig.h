#ifndef OC_PROJECT_CONFIG_H
#define OC_PROJECT_CONFIG_H

#include <QString>

struct ProjectConfig {
    QString sourceDir;       // multi-canny gray
    QString outputDir;       // resulting outline; required, created on first save
    QString outlines1Dir;
    QString outlines2Dir;
    QString originalDir;     // optional

    bool isValid() const;
    // Returns an empty string when valid, otherwise a human-readable message
    // describing which required directory is missing or unset.
    QString validationError() const;
    bool load(const QString& path);
    bool save(const QString& path) const;
};

#endif