#include "CursorUtils.h"

#include <QPainter>
#include <QPixmap>
#include <QPolygon>

QCursor makePickCursor(int radiusPx)
{
    const int r = std::max(1, radiusPx);
    const int W = 48, H = 48;
    const QPoint hot(16, 16);

    QPixmap pm(W, H);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPolygon arrow;
    arrow << hot
          << QPoint(hot.x() + 1,  hot.y() + 14)
          << QPoint(hot.x() + 5,  hot.y() + 11)
          << QPoint(hot.x() + 9,  hot.y() + 16)
          << QPoint(hot.x() + 11, hot.y() + 14)
          << QPoint(hot.x() + 7,  hot.y() + 9)
          << QPoint(hot.x() + 11, hot.y() + 7);
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    p.drawPolygon(arrow);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 220), 1));
    p.drawEllipse(hot, r, r);
    p.setPen(QPen(QColor(255, 255, 0, 220), 1, Qt::DotLine));
    p.drawEllipse(hot, r, r);
    p.end();

    return QCursor(pm, hot.x(), hot.y());
}
