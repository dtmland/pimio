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

    static const QRegularExpression pattern(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
    QVERIFY2(pattern.match(version).hasMatch(), qPrintable(version));
}

QTEST_APPLESS_MAIN(TestCoreVersion)

#include "tst_core_version.moc"
