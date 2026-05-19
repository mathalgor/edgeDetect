#include <QApplication>
#include "McMainWindow.h"

int main(int argc, char** argv)
{
    QApplication::setApplicationName("manualCorrect");
    QApplication::setOrganizationName("edgeDetect");
    QApplication app(argc, argv);
    McMainWindow w;
    w.show();
    return QApplication::exec();
}