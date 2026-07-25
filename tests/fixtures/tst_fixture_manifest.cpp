#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <algorithm>

#ifndef PIMIO_FIXTURES_DIR
#error "PIMIO_FIXTURES_DIR must be defined by the build system"
#endif

namespace {

QString fixturesDir()
{
    return QStringLiteral(PIMIO_FIXTURES_DIR);
}

QByteArray readAll(const QString &path, bool *ok)
{
    QFile file(path);
    *ok = file.open(QIODevice::ReadOnly);
    if (!*ok) {
        return {};
    }
    return file.readAll();
}

} // namespace

/// Guards the test corpus.
///
/// The fixtures are committed binary files, so the manifest is the record of
/// what they are, where they came from, and what they are supposed to prove.
/// If a fixture changes, this test fails and the change has to be deliberate.
class TestFixtureManifest : public QObject
{
    Q_OBJECT

private slots:
    void manifestIsWellFormed();
    void everyFixtureMatchesItsRecordedHash();
    void everyFileOnDiskIsListedInTheManifest();
    void manifestRecordsProvenanceAndCoverage();
    void corpusCoversTheRequiredMediaCategories();
};

void TestFixtureManifest::manifestIsWellFormed()
{
    bool ok = false;
    const QByteArray contents =
            readAll(QDir(fixturesDir()).absoluteFilePath(QStringLiteral("manifest.json")), &ok);
    QVERIFY2(ok, "The fixture manifest is missing.");

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    const QJsonObject manifest = document.object();
    QCOMPARE(manifest.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(manifest.value(QStringLiteral("generator")).toString(),
             QStringLiteral("tools/fixture_generator"));
    QVERIFY(!manifest.value(QStringLiteral("fixtures")).toArray().isEmpty());
}

void TestFixtureManifest::everyFixtureMatchesItsRecordedHash()
{
    bool ok = false;
    const QJsonObject manifest =
            QJsonDocument::fromJson(
                    readAll(QDir(fixturesDir()).absoluteFilePath(QStringLiteral("manifest.json")),
                            &ok))
                    .object();
    QVERIFY(ok);

    const QJsonArray fixtures = manifest.value(QStringLiteral("fixtures")).toArray();
    for (const QJsonValue &value : fixtures) {
        const QJsonObject entry = value.toObject();
        const QString relativePath = entry.value(QStringLiteral("path")).toString();
        const QString absolutePath = QDir(fixturesDir()).absoluteFilePath(relativePath);

        bool readOk = false;
        const QByteArray contents = readAll(absolutePath, &readOk);
        QVERIFY2(readOk, qPrintable(QStringLiteral("Missing fixture: %1").arg(relativePath)));

        QCOMPARE(qint64(contents.size()),
                 qint64(entry.value(QStringLiteral("sizeBytes")).toDouble(-1)));

        const QString digest = QString::fromLatin1(
                QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
        QCOMPARE(digest, entry.value(QStringLiteral("sha256")).toString());

        // Fixtures must stay small enough to live in the repository.
        QVERIFY2(contents.size() <= 64 * 1024,
                 qPrintable(QStringLiteral("Fixture is too large: %1").arg(relativePath)));
    }
}

void TestFixtureManifest::everyFileOnDiskIsListedInTheManifest()
{
    bool ok = false;
    const QJsonObject manifest =
            QJsonDocument::fromJson(
                    readAll(QDir(fixturesDir()).absoluteFilePath(QStringLiteral("manifest.json")),
                            &ok))
                    .object();
    QVERIFY(ok);

    QSet<QString> listed;
    const QJsonArray fixtures = manifest.value(QStringLiteral("fixtures")).toArray();
    for (const QJsonValue &value : fixtures) {
        listed.insert(value.toObject().value(QStringLiteral("path")).toString());
    }

    const QDir root(fixturesDir());
    QDirIterator iterator(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        // Normalize to forward slashes so Windows paths match the manifest.
        const QString relativePath =
                root.relativeFilePath(iterator.next()).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (relativePath == QLatin1String("manifest.json")) {
            continue;
        }
        QVERIFY2(listed.contains(relativePath),
                 qPrintable(QStringLiteral("Unlisted fixture on disk: %1").arg(relativePath)));
    }
}

void TestFixtureManifest::manifestRecordsProvenanceAndCoverage()
{
    bool ok = false;
    const QJsonObject manifest =
            QJsonDocument::fromJson(
                    readAll(QDir(fixturesDir()).absoluteFilePath(QStringLiteral("manifest.json")),
                            &ok))
                    .object();
    QVERIFY(ok);

    const QJsonArray fixtures = manifest.value(QStringLiteral("fixtures")).toArray();
    for (const QJsonValue &value : fixtures) {
        const QJsonObject entry = value.toObject();
        const QString path = entry.value(QStringLiteral("path")).toString();
        QVERIFY2(!entry.value(QStringLiteral("provenance")).toString().isEmpty(),
                 qPrintable(QStringLiteral("Missing provenance: %1").arg(path)));
        QVERIFY2(!entry.value(QStringLiteral("covers")).toString().isEmpty(),
                 qPrintable(QStringLiteral("Missing coverage note: %1").arg(path)));
    }
}

void TestFixtureManifest::corpusCoversTheRequiredMediaCategories()
{
    // Increment 1 requires fixtures for each of these categories. Losing one
    // silently would weaken every later increment that depends on the corpus.
    const QStringList requiredPrefixes = {
        QStringLiteral("images/jpeg-"), QStringLiteral("images/png-"),
        QStringLiteral("raw/"),         QStringLiteral("video/"),
        QStringLiteral("malformed/"),   QStringLiteral("sidecars/"),
    };

    bool ok = false;
    const QJsonObject manifest =
            QJsonDocument::fromJson(
                    readAll(QDir(fixturesDir()).absoluteFilePath(QStringLiteral("manifest.json")),
                            &ok))
                    .object();
    QVERIFY(ok);

    QStringList paths;
    const QJsonArray fixtures = manifest.value(QStringLiteral("fixtures")).toArray();
    for (const QJsonValue &value : fixtures) {
        paths.append(value.toObject().value(QStringLiteral("path")).toString());
    }

    for (const QString &prefix : requiredPrefixes) {
        const bool found = std::any_of(paths.cbegin(), paths.cend(), [&prefix](const QString &p) {
            return p.startsWith(prefix);
        });
        QVERIFY2(found, qPrintable(QStringLiteral("No fixture covers %1").arg(prefix)));
    }

    // Timestamp edge cases are the reason several image fixtures exist.
    QVERIFY(paths.contains(QStringLiteral("images/jpeg-exif-no-offset.jpg")));
    QVERIFY(paths.contains(QStringLiteral("images/jpeg-exif-dst-gap.jpg")));
    QVERIFY(paths.contains(QStringLiteral("images/jpeg-exif-dst-fold.jpg")));
    QVERIFY(paths.contains(QStringLiteral("images/jpeg-exif-leap-day.jpg")));
}

QTEST_APPLESS_MAIN(TestFixtureManifest)

#include "tst_fixture_manifest.moc"
