#include "pimio/metadata/builtin_metadata_reader.h"
#include "pimio/metadata/exiftool_metadata_writer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio;

namespace {

QString fixture()
{
    return QDir(QStringLiteral(PIMIO_FIXTURES_DIR))
            .filePath(QStringLiteral("images/jpeg-exif-offset.jpg"));
}

QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

core::ContentFingerprint fingerprint(const QString &path)
{
    return core::ContentFingerprint(
            QStringLiteral("sha256"),
            QString::fromLatin1(QCryptographicHash::hash(contents(path),
                                                         QCryptographicHash::Sha256)
                                        .toHex()));
}

} // namespace

class TestMetadataWrite : public QObject
{
    Q_OBJECT

private slots:
    void writesAndRereadsEmbeddedMetadata();
    void conflictAndToolFailurePreserveBytes();
    void rejectsUnsupportedFormats();
};

void TestMetadataWrite::writesAndRereadsEmbeddedMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::copy(fixture(), target));
    const QByteArray fixtureBytes = contents(fixture());

    metadata::BuiltinMetadataReader reader;
    core::Error error;
    const auto before = reader.read(target, &error);
    QVERIFY2(before.has_value(), qPrintable(error.message()));

    core::MediaMetadata edited = before->metadata;
    edited.rating = 4;
    edited.caption = QStringLiteral("A portable caption");
    edited.tags = {QStringLiteral("alpha"), QStringLiteral("βeta")};

    metadata::ExifToolMetadataWriter writer;
    QVERIFY(writer.isAvailable());
    QVERIFY2(writer.write(target, edited, fingerprint(target), &error),
             qPrintable(error.message()));

    const auto after = reader.read(target, &error);
    QVERIFY2(after.has_value(), qPrintable(error.message()));
    QCOMPARE(after->metadata.rating, 4);
    QCOMPARE(after->metadata.caption, QStringLiteral("A portable caption"));
    QCOMPARE(after->metadata.tags, edited.tags);
    QCOMPARE(after->metadata.cameraMake, before->metadata.cameraMake);
    QCOMPARE(after->metadata.cameraModel, before->metadata.cameraModel);
    QCOMPARE(contents(fixture()), fixtureBytes);
}

void TestMetadataWrite::conflictAndToolFailurePreserveBytes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::copy(fixture(), target));
    const QByteArray before = contents(target);

    metadata::ExifToolMetadataWriter writer;
    core::MediaMetadata edited;
    edited.rating = 5;
    core::Error error;
    QVERIFY(!writer.write(target, edited,
                          core::ContentFingerprint(QStringLiteral("sha256"),
                                                   QStringLiteral("wrong")),
                          &error));
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(core::ErrorCode::Conflict));
    QCOMPARE(contents(target), before);

    metadata::ExifToolMetadataWriter missing(QStringLiteral("/missing/exiftool"), {});
    QVERIFY(!missing.write(target, edited, fingerprint(target), &error));
    QCOMPARE(static_cast<int>(error.code()),
             static_cast<int>(core::ErrorCode::StorageUnavailable));
    QCOMPARE(contents(target), before);
}

void TestMetadataWrite::rejectsUnsupportedFormats()
{
    QTemporaryDir directory;
    const QString target = directory.filePath(QStringLiteral("photo.raw"));
    QVERIFY(QFile::copy(fixture(), target));
    const QByteArray before = contents(target);

    metadata::ExifToolMetadataWriter writer;
    core::Error error;
    QVERIFY(!writer.write(target, {}, fingerprint(target), &error));
    QCOMPARE(static_cast<int>(error.code()),
             static_cast<int>(core::ErrorCode::UnsupportedMedia));
    QCOMPARE(contents(target), before);
}

QTEST_APPLESS_MAIN(TestMetadataWrite)

#include "tst_metadata_write.moc"
