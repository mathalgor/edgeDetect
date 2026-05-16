#include <QApplication>
#include "OcMainWindow.h"

int main(int argc, char** argv)
{
    QApplication::setApplicationName("outlineChooser");
    QApplication::setOrganizationName("edgeDetect");
    QApplication app(argc, argv);
    OcMainWindow w;
    w.show();
    return QApplication::exec();
}