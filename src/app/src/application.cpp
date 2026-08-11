#include "pimio/app/application.h"

#include "pimio/core/version.h"
#include "pimio/settings/settings.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace pimio::app {

void configureApplicationMetadata()
{
    QCoreApplication::setOrganizationName(QStringLiteral("pimio"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("pimio.local"));
    QCoreApplication::setApplicationName(QStringLiteral("pimio"));
    QCoreApplication::setApplicationVersion(core::versionString());
}

settings::Settings &applicationSettings()
{
    // Function-local so the configuration file path is resolved after
    // configureApplicationMetadata() has run, not at static-initialisation
    // time when the organisation and application names are still unset.
    static settings::Settings instance;
    return instance;
}

QString mainQmlUrl()
{
    return QStringLiteral("qrc:/qt/qml/Pimio/qml/Main.qml");
}

bool loadMainQml(QQmlApplicationEngine &engine)
{
    if (!engine.rootContext()->contextProperty(QStringLiteral("appSettings")).isValid()) {
        engine.rootContext()->setContextProperty(QStringLiteral("appSettings"),
                                                 &applicationSettings());
    }
    engine.load(QUrl(mainQmlUrl()));
    return !engine.rootObjects().isEmpty();
}

} // namespace pimio::app
