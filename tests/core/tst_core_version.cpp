#include "pimio/core/version.h"

#include <QRegularExpression>
#include <QTest>

class TestCoreVersion : public QObject
{
    Q_OBJECT

private slots:
    void versionStringIsSemanticVersion();
};

void TestCoreVersion::versionStringIsSemanticVersion()
{
    const QString version = pimio::core::versionString();
    QVERIFY(!version.isEmpty());

    // Semantic version with an optional pre-release suffix; development
    // builds report e.g. "0.1.11-dev" while releases report the bare tag.
    static const QRegularExpression pattern(
        QStringLiteral("^\\d+\\.\\d+\\.\\d+(-[0-9A-Za-z.]+)?$"));
    QVERIFY2(pattern.match(version).hasMatch(), qPrintable(version));
}

QTEST_APPLESS_MAIN(TestCoreVersion)

#include "tst_core_version.moc"
