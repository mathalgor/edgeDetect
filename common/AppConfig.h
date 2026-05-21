#ifndef EDGEDETECT_APP_CONFIG_H
#define EDGEDETECT_APP_CONFIG_H

#include <QString>
#include <QStringList>

// MRU + last-opened-project state shared by cannyToOutline and outlineChooser.
// The on-disk filename is derived from QCoreApplication::applicationName(),
// so each app reads/writes a distinct JSON in its own config dir.
struct AppConfig {
    QString currentProjectPath;
    QStringList recentProjects;
    int mruSize = 20;
    QString lastDatasetRoot;
    QString lastDatasetSplit = "train";
    bool    lastDatasetToGray = false;  // align: convert source to grayscale on export
    QString lastDatasetMode = "only new"; // align: "only new" | "overwrite"

    static QString configPath();
    bool load();
    bool save() const;
    void addRecent(const QString& path);
};

#endif
