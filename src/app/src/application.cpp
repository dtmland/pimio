#include "pimio/app/application.h"

#include "pimio/core/version.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>

namespace pimio::app {

void configureApplicationMetadata()
{
    QCoreApplication::setOrganizationName(QStringLiteral("pimio"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("pimio.local"));
    QCoreApplication::setApplicationName(QStringLiteral("pimio"));
    QCoreApplication::setApplicationVersion(core::versionString());
}

QString mainQmlUrl()
{
    return QStringLiteral("qrc:/qt/qml/Pimio/qml/Main.qml");
}

bool loadMainQml(QQmlApplicationEngine &engine)
{
    engine.load(QUrl(mainQmlUrl()));
    return !engine.rootObjects().isEmpty();
}

} // namespace pimio::app
