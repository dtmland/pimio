// Studio test (Tests B): drives the real application window on a real
// display, verifies what a user would see on first launch, and saves
// screenshots and a log so the run can be reported back from a machine the
// developers cannot see.
//
// Results are written to the directory named by the PIMIO_STUDIO_RESULTS
// environment variable (created if needed); without it they go to a
// pimio-studio-results directory under the system temp path.

#include "pimio/app/application.h"
#include "pimio/core/version.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSysInfo>
#include <QTest>
#include <QTextStream>

class TestStudioEmptyLibrary : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void windowOpensOnTheRealDisplay();
    void titleBarShowsTheBuildVersion();
    void emptyLibraryPlaceholderIsVisible();
    void cleanupTestCase();

private:
    QString resultsDir() const;
    void saveScreenshot(const QString &name);
    void log(const QString &line);

    QQmlApplicationEngine m_engine;
    QQuickWindow *m_window = nullptr;
    QStringList m_logLines;
};

QString TestStudioEmptyLibrary::resultsDir() const
{
    QString dir = qEnvironmentVariable("PIMIO_STUDIO_RESULTS");
    if (dir.isEmpty()) {
        dir = QDir::tempPath() + QStringLiteral("/pimio-studio-results");
    }
    QDir().mkpath(dir);
    return dir;
}

void TestStudioEmptyLibrary::saveScreenshot(const QString &name)
{
    QVERIFY(m_window != nullptr);
    const QImage frame = m_window->grabWindow();
    QVERIFY2(!frame.isNull(), "grabWindow() returned a null image");
    const QString path = resultsDir() + QStringLiteral("/") + name;
    QVERIFY2(frame.save(path), qPrintable(QStringLiteral("could not save %1").arg(path)));
    log(QStringLiteral("screenshot: %1 (%2x%3)")
            .arg(path).arg(frame.width()).arg(frame.height()));
}

void TestStudioEmptyLibrary::log(const QString &line)
{
    m_logLines.append(line);
    qInfo().noquote() << line;
}

void TestStudioEmptyLibrary::initTestCase()
{
    pimio::app::configureApplicationMetadata();
    log(QStringLiteral("pimio version: %1").arg(pimio::core::versionString()));
    log(QStringLiteral("platform: %1 (%2), Qt %3, QPA %4")
            .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                 QString::fromLatin1(qVersion()),
                 QGuiApplication::platformName()));

    QVERIFY(pimio::app::loadMainQml(m_engine));
    m_window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().constFirst());
    QVERIFY(m_window != nullptr);
}

void TestStudioEmptyLibrary::windowOpensOnTheRealDisplay()
{
    QVERIFY2(QTest::qWaitForWindowExposed(m_window),
             "the main window was never exposed on the display");
    log(QStringLiteral("window exposed at %1x%2")
            .arg(m_window->width()).arg(m_window->height()));
    saveScreenshot(QStringLiteral("01-first-launch.png"));
}

void TestStudioEmptyLibrary::titleBarShowsTheBuildVersion()
{
    auto *label = m_window->findChild<QQuickItem *>(QStringLiteral("placeholderLabel"));
    QVERIFY2(label != nullptr, "toolbar version label not found");
    const QString text = label->property("text").toString();
    log(QStringLiteral("toolbar text: \"%1\"").arg(text));
    QVERIFY2(text.contains(pimio::core::versionString()),
             qPrintable(QStringLiteral("toolbar shows \"%1\" but the build is %2")
                            .arg(text, pimio::core::versionString())));
}

void TestStudioEmptyLibrary::emptyLibraryPlaceholderIsVisible()
{
    auto *placeholder =
        m_window->findChild<QQuickItem *>(QStringLiteral("emptyLibraryPlaceholder"));
    QVERIFY2(placeholder != nullptr, "empty-library placeholder not found");
    QVERIFY2(placeholder->isVisible(), "placeholder is hidden although the library is empty");

    auto *icon = m_window->findChild<QQuickItem *>(QStringLiteral("emptyLibraryIcon"));
    QVERIFY2(icon != nullptr, "empty-library camera icon not found");
    QVERIFY2(icon->isVisible(), "camera icon is hidden");
    QVERIFY2(icon->width() > 0 && icon->height() > 0, "camera icon has no size");
    log(QStringLiteral("empty-library placeholder and icon visible"));
    saveScreenshot(QStringLiteral("02-empty-library-placeholder.png"));
}

void TestStudioEmptyLibrary::cleanupTestCase()
{
    const QString path = resultsDir() + QStringLiteral("/studio-empty-library.log");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text),
             qPrintable(QStringLiteral("could not write %1").arg(path)));
    QTextStream out(&file);
    out << QStringLiteral("studio.empty_library run at %1\n")
               .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    for (const QString &line : std::as_const(m_logLines)) {
        out << line << '\n';
    }
    qInfo().noquote() << QStringLiteral("results written to %1").arg(resultsDir());
}

QTEST_MAIN(TestStudioEmptyLibrary)

#include "tst_studio_empty_library.moc"
