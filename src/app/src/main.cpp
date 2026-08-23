#include "pimio/app/application.h"
#include "pimio/app/library_session.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

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

    // Register the library session's context properties and image provider
    // before QML is loaded, but leave storage and recursive-watch setup until
    // the first frame has given the user visible startup feedback.
    pimio::app::LibrarySession librarySession;
    librarySession.prepare(parser.values(libraryOption), engine);

    if (!pimio::app::loadMainQml(engine)) {
        return 1;
    }

    if (parser.isSet(selfCheckOption)) {
        return 0;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window) {
        QObject::connect(window, &QQuickWindow::frameSwapped, &librarySession,
                         [&librarySession] { librarySession.start(); },
                         Qt::SingleShotConnection);
    }
    // Some headless or unusual render loops may not report a swap. The
    // idempotent start() guard makes this a safe fallback without delaying a
    // normal first-frame start.
    QTimer::singleShot(250, &librarySession, [&librarySession] { librarySession.start(); });

    return QGuiApplication::exec();
}
