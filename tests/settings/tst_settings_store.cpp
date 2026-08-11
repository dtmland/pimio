#include "pimio/settings/settings.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using pimio::settings::Settings;
using pimio::settings::SortKey;

namespace {
/// QCOMPARE needs a printable type, and a scoped enum is not one; comparing
/// the underlying value also makes a failure message say which key it got.
int value(SortKey key)
{
    return static_cast<int>(key);
}
} // namespace

class TestSettingsStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void defaultsAreUsedWhenNothingIsStored();
    void storedSettingsSurviveANewInstance();
    void sessionSettingsDoNotSurviveANewInstance();
    void outOfRangeValuesAreClamped();
    void unreadableStoredValuesFallBackToDefaults_data();
    void unreadableStoredValuesFallBackToDefaults();
    void unknownSortKeyFallsBackToTheDefault();
    void settingAValueEmitsExactlyOneChange();
    void settingTheSameValueEmitsNothing();
    void resetToDefaultsRestoresAndRewritesEveryValue();
    void sortKeyLabelsExistForEveryKey();
    void theScanBatchSizeIsStoredClampedAndResettable();

private:
    QString configPath() const;

    QTemporaryDir m_dir;
};

QString TestSettingsStore::configPath() const
{
    return QDir(m_dir.path()).filePath(QStringLiteral("pimio.conf"));
}

void TestSettingsStore::init()
{
    QFile::remove(configPath());
}

void TestSettingsStore::defaultsAreUsedWhenNothingIsStored()
{
    Settings settings(configPath());

    QCOMPARE(settings.tileSize(), 176);
    QCOMPARE(value(settings.sortKey()), value(SortKey::CaptureTime));
    QCOMPARE(settings.sortDescending(), false);
    QCOMPARE(settings.scrollAcceleration(), true);
    QCOMPARE(settings.keyRepeatAcceleration(), true);
    QCOMPARE(settings.showTileDiagnostics(), false);
    QVERIFY(settings.scrollSpeed() >= Settings::minimumScrollSpeed());
    QVERIFY(settings.scrollSpeed() <= Settings::maximumScrollSpeed());
    QCOMPARE(settings.filePath(), configPath());
}

void TestSettingsStore::storedSettingsSurviveANewInstance()
{
    {
        Settings settings(configPath());
        settings.setTileSize(240);
        settings.setSortKey(SortKey::FileSize);
        settings.setSortDescending(true);
        settings.setScrollSpeed(3.5);
        settings.setScrollAcceleration(false);
        settings.setKeyRepeatAcceleration(false);
        settings.flush();
    }

    QVERIFY2(QFile::exists(configPath()), "the configuration file should have been written");

    Settings reloaded(configPath());
    QCOMPARE(reloaded.tileSize(), 240);
    QCOMPARE(value(reloaded.sortKey()), value(SortKey::FileSize));
    QCOMPARE(reloaded.sortDescending(), true);
    QCOMPARE(reloaded.scrollSpeed(), 3.5);
    QCOMPARE(reloaded.scrollAcceleration(), false);
    QCOMPARE(reloaded.keyRepeatAcceleration(), false);
}

void TestSettingsStore::sessionSettingsDoNotSurviveANewInstance()
{
    {
        Settings settings(configPath());
        settings.setShowTileDiagnostics(true);
        QCOMPARE(settings.showTileDiagnostics(), true);
        // A stored setting as well, so the file exists and the check below is
        // about what is in it rather than about whether it was written.
        settings.setTileSize(200);
        settings.flush();
    }

    Settings reloaded(configPath());
    QCOMPARE(reloaded.showTileDiagnostics(), false);
    QCOMPARE(reloaded.tileSize(), 200);

    // Nor should it have reached the file under any spelling.
    QFile file(configPath());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString contents = QString::fromUtf8(file.readAll());
    QVERIFY2(!contents.contains(QStringLiteral("TileDiagnostics"), Qt::CaseInsensitive),
             qPrintable(contents));
}

void TestSettingsStore::outOfRangeValuesAreClamped()
{
    Settings settings(configPath());

    settings.setTileSize(Settings::minimumTileSize() - 50);
    QCOMPARE(settings.tileSize(), Settings::minimumTileSize());
    settings.setTileSize(Settings::maximumTileSize() + 500);
    QCOMPARE(settings.tileSize(), Settings::maximumTileSize());

    settings.setScrollSpeed(-4.0);
    QCOMPARE(settings.scrollSpeed(), Settings::minimumScrollSpeed());
    settings.setScrollSpeed(1000.0);
    QCOMPARE(settings.scrollSpeed(), Settings::maximumScrollSpeed());
}

void TestSettingsStore::unreadableStoredValuesFallBackToDefaults_data()
{
    QTest::addColumn<QString>("fileContents");

    QTest::newRow("not a number") << QStringLiteral("[view]\ntileSize=huge\n");
    QTest::newRow("empty value") << QStringLiteral("[view]\ntileSize=\n");
    QTest::newRow("out of range") << QStringLiteral("[view]\ntileSize=100000\n");
    QTest::newRow("nonsense bool")
            << QStringLiteral("[input]\nscrollAcceleration=perhaps\n");
    QTest::newRow("nonsense real") << QStringLiteral("[input]\nscrollSpeed=fast\n");
}

void TestSettingsStore::unreadableStoredValuesFallBackToDefaults()
{
    QFETCH(QString, fileContents);

    {
        QFile file(configPath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&file) << fileContents;
    }

    Settings settings(configPath());
    QVERIFY(settings.tileSize() >= Settings::minimumTileSize());
    QVERIFY(settings.tileSize() <= Settings::maximumTileSize());
    QVERIFY(settings.scrollSpeed() >= Settings::minimumScrollSpeed());
    QVERIFY(settings.scrollSpeed() <= Settings::maximumScrollSpeed());
    QCOMPARE(settings.scrollAcceleration(), true);
}

void TestSettingsStore::unknownSortKeyFallsBackToTheDefault()
{
    {
        QFile file(configPath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        // A value a newer build might write.
        QTextStream(&file) << QStringLiteral("[view]\nsortKey=faceCount\n");
    }

    Settings settings(configPath());
    QCOMPARE(value(settings.sortKey()), value(SortKey::CaptureTime));
}

void TestSettingsStore::settingAValueEmitsExactlyOneChange()
{
    Settings settings(configPath());
    QSignalSpy tileSpy(&settings, &Settings::tileSizeChanged);
    QSignalSpy sortSpy(&settings, &Settings::sortKeyChanged);
    QSignalSpy diagnosticsSpy(&settings, &Settings::showTileDiagnosticsChanged);

    settings.setTileSize(200);
    settings.setSortKey(SortKey::FileName);
    settings.setShowTileDiagnostics(true);

    QCOMPARE(tileSpy.size(), 1);
    QCOMPARE(sortSpy.size(), 1);
    QCOMPARE(diagnosticsSpy.size(), 1);
}

void TestSettingsStore::settingTheSameValueEmitsNothing()
{
    Settings settings(configPath());
    settings.setTileSize(200);

    QSignalSpy tileSpy(&settings, &Settings::tileSizeChanged);
    settings.setTileSize(200);
    // Clamped to the same value: still no change.
    settings.setTileSize(Settings::maximumTileSize() + 1);
    settings.setTileSize(Settings::maximumTileSize());
    QCOMPARE(tileSpy.size(), 1);
}

void TestSettingsStore::resetToDefaultsRestoresAndRewritesEveryValue()
{
    {
        Settings settings(configPath());
        settings.setTileSize(240);
        settings.setSortKey(SortKey::FileType);
        settings.setSortDescending(true);
        settings.setShowTileDiagnostics(true);
        settings.resetToDefaults();

        QCOMPARE(settings.tileSize(), 176);
        QCOMPARE(value(settings.sortKey()), value(SortKey::CaptureTime));
        QCOMPARE(settings.sortDescending(), false);
        QCOMPARE(settings.showTileDiagnostics(), false);
    }

    Settings reloaded(configPath());
    QCOMPARE(reloaded.tileSize(), 176);
    QCOMPARE(value(reloaded.sortKey()), value(SortKey::CaptureTime));
    QCOMPARE(reloaded.sortDescending(), false);
}

void TestSettingsStore::sortKeyLabelsExistForEveryKey()
{
    const QList<SortKey> keys = pimio::settings::allSortKeys();
    QCOMPARE(keys.size(), 5);

    QStringList labels;
    QStringList stored;
    for (const SortKey key : keys) {
        const QString label = pimio::settings::sortKeyLabel(key);
        QVERIFY(!label.isEmpty());
        labels << label;

        const QString text = pimio::settings::toString(key);
        QVERIFY(!text.isEmpty());
        stored << text;

        QVERIFY(pimio::settings::sortKeyFromString(text).has_value());
        QCOMPARE(value(*pimio::settings::sortKeyFromString(text)), value(key));
        QCOMPARE(Settings::sortKeyLabelFor(static_cast<int>(key)), label);
    }

    labels.removeDuplicates();
    stored.removeDuplicates();
    QCOMPARE(labels.size(), keys.size());
    QCOMPARE(stored.size(), keys.size());
    QCOMPARE(Settings::sortKeyValues().size(), keys.size());
    QVERIFY(!pimio::settings::sortKeyFromString(QStringLiteral("nope")).has_value());
}

void TestSettingsStore::theScanBatchSizeIsStoredClampedAndResettable()
{
    {
        Settings settings(configPath());
        QCOMPARE(settings.scanBatchSize(), 64);

        settings.setScanBatchSize(Settings::minimumScanBatchSize() - 1);
        QCOMPARE(settings.scanBatchSize(), Settings::minimumScanBatchSize());
        settings.setScanBatchSize(Settings::maximumScanBatchSize() + 1);
        QCOMPARE(settings.scanBatchSize(), Settings::maximumScanBatchSize());

        QSignalSpy spy(&settings, &Settings::scanBatchSizeChanged);
        settings.setScanBatchSize(128);
        QCOMPARE(spy.size(), 1);
        settings.setScanBatchSize(128);
        QCOMPARE(spy.size(), 1);
        settings.flush();
    }

    {
        Settings reloaded(configPath());
        QCOMPARE(reloaded.scanBatchSize(), 128);
        reloaded.resetToDefaults();
        QCOMPARE(reloaded.scanBatchSize(), 64);
    }

    Settings again(configPath());
    QCOMPARE(again.scanBatchSize(), 64);
}

QTEST_MAIN(TestSettingsStore)

#include "tst_settings_store.moc"
