#include "OriginalLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <opencv2/imgcodecs.hpp>

cv::Mat loadOriginalForStem(const QString& dir, const QString& stem)
{
    if (dir.isEmpty() || stem.isEmpty()) return {};
    QDir od(dir);
    if (!od.exists()) return {};

    static const QStringList kExts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"
    };
    QString found;
    for (const QString& e : kExts) {
        const QString cand = od.filePath(stem + e);
        if (QFileInfo::exists(cand)) { found = cand; break; }
    }
    if (found.isEmpty()) {
        const QStringList list = od.entryList(QStringList{stem + ".*"},
                                              QDir::Files, QDir::Name);
        if (!list.isEmpty()) found = od.filePath(list.first());
    }
    if (found.isEmpty()) return {};
    return cv::imread(found.toStdString(), cv::IMREAD_UNCHANGED);
}
