#include "OriginalLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <opencv2/imgcodecs.hpp>

QString findOriginalPathForStem(const QString& dir, const QString& stem)
{
    if (dir.isEmpty() || stem.isEmpty()) return {};
    QDir od(dir);
    if (!od.exists()) return {};

    static const QStringList kExts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"
    };
    for (const QString& e : kExts) {
        const QString cand = od.filePath(stem + e);
        if (QFileInfo::exists(cand)) return cand;
    }
    const QStringList list = od.entryList(QStringList{stem + ".*"},
                                          QDir::Files, QDir::Name);
    if (!list.isEmpty()) return od.filePath(list.first());
    return {};
}

cv::Mat loadOriginalForStem(const QString& dir, const QString& stem)
{
    const QString path = findOriginalPathForStem(dir, stem);
    if (path.isEmpty()) return {};
    return cv::imread(path.toStdString(), cv::IMREAD_UNCHANGED);
}
