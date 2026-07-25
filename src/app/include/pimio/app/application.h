#pragma once

#include <QString>

class QQmlApplicationEngine;

namespace pimio::app {

/// Configures application-wide identity used for settings and cache paths.
void configureApplicationMetadata();

/// URL of the root QML component loaded by the application shell.
QString mainQmlUrl();

/// Loads the root QML component into \a engine.
///
/// Returns true once the root object exists. This is the shared entry point
/// used by both the executable and the offscreen smoke test so that the test
/// exercises the same startup path as the shipped application.
bool loadMainQml(QQmlApplicationEngine &engine);

} // namespace pimio::app
