#pragma once

#include <QString>

class QQmlApplicationEngine;

namespace pimio::settings {
class Settings;
}

namespace pimio::app {

/// Configures application-wide identity used for settings and cache paths.
void configureApplicationMetadata();

/// The process-wide user settings.
///
/// Created on first use, after configureApplicationMetadata() has decided
/// where this user's configuration lives. Settings are pimio-wide rather than
/// per library root, so there is one instance and everything that reads or
/// writes a setting -- QML, the library session, a future command line --
/// shares it.
settings::Settings &applicationSettings();

/// URL of the root QML component loaded by the application shell.
QString mainQmlUrl();

/// Loads the root QML component into \a engine.
///
/// Returns true once the root object exists. This is the shared entry point
/// used by both the executable and the offscreen smoke test so that the test
/// exercises the same startup path as the shipped application.
///
/// Registers applicationSettings() as the "appSettings" context property
/// before loading, unless the caller already set one, so QML can always bind
/// to settings and a test can substitute its own instance.
bool loadMainQml(QQmlApplicationEngine &engine);

} // namespace pimio::app
