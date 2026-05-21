#include <QApplication>
#include "AlignMainWindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    AlignMainWindow w;
    w.show();

    return app.exec();
}
