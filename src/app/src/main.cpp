#include "pimio/app/application.h"
#include "pimio/app/library_session.h"

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
    const QCommandLineOption libraryOption(
        QStringLiteral("library"),
        QStringLiteral("Path to a library folder to scan, watch, and browse. May be given more "
                       "than once to index several folders together."),
        QStringLiteral("path"));
    parser.addOption(libraryOption);
    parser.process(application);

    QQmlApplicationEngine engine;

    // Registering the library session's context properties and image
    // provider happens before loadMainQml() unconditionally, even with no
    // --library at all, so QML never observes them appearing after the
    // fact — see LibrarySession::start().
    pimio::app::LibrarySession librarySession;
    librarySession.start(parser.values(libraryOption), engine);

    if (!pimio::app::loadMainQml(engine)) {
        return 1;
    }

    if (parser.isSet(selfCheckOption)) {
        return 0;
    }

    return QGuiApplication::exec();
}
