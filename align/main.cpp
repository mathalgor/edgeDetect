#include <QApplication>
#include "AlignMainWindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("align");
    QApplication::setOrganizationName("edgeDetect");

    AlignMainWindow w;
    w.show();

    return app.exec();
}
