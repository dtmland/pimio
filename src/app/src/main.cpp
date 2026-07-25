#include "pimio/app/application.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);

    pimio::app::configureApplicationMetadata();

    QQmlApplicationEngine engine;
    if (!pimio::app::loadMainQml(engine)) {
        return 1;
    }

    return QGuiApplication::exec();
}
