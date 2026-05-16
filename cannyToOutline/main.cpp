#include <QApplication>
#include "CannyMainWindow.h"

int main(int argc, char** argv)
{
    QApplication::setApplicationName("cannyToOutline");
    QApplication::setOrganizationName("edgeDetect");
    QApplication app(argc, argv);
    CannyMainWindow w;
    w.show();
    if (argc >= 2) {
        w.loadFile(QString::fromLocal8Bit(argv[1]));
    }
    return QApplication::exec();
}