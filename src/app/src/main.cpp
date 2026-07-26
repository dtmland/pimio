#include "pimio/app/application.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);

    pimio::app::configureApplicationMetadata();

    // --version and --self-check exist so a packaged build can be validated
    // without a human at the display: reaching either point already proves the
    // Qt platform plugin loaded, and --self-check additionally proves the QML
    // modules were deployed. The release smoke test and pimio-doctor use them.
    QCommandLineParser parser;
    parser.setApplicationDescription(QGuiApplication::applicationName());
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption selfCheckOption(
        QStringLiteral("self-check"),
        QStringLiteral("Load the application shell, then exit without showing a window."));
    parser.addOption(selfCheckOption);
    parser.process(application);

    QQmlApplicationEngine engine;
    if (!pimio::app::loadMainQml(engine)) {
        return 1;
    }

    if (parser.isSet(selfCheckOption)) {
        return 0;
    }

    return QGuiApplication::exec();
}
