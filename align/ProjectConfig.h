#ifndef AL_PROJECT_CONFIG_H
#define AL_PROJECT_CONFIG_H

#include <QString>

struct ProjectConfig {
    QString grayDir;     // required — gray (source) images
    QString outlineDir;  // required — target outlines (same basenames)

    bool isValid() const;
    QString validationError() const;
    bool load(const QString& path);
    bool save(const QString& path) const;
};

#endif