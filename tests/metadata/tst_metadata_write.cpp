#include "pimio/metadata/builtin_metadata_reader.h"
#include "pimio/metadata/exiftool_metadata_writer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
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
    void writesAndRereadsEmbeddedMetadata_data();
    void writesAndRereadsEmbeddedMetadata();
    void conflictAndToolFailurePreserveBytes();
    void rejectsUnsupportedFormats();
};

void TestMetadataWrite::writesAndRereadsEmbeddedMetadata_data()
{
    QTest::addColumn<QString>("format");
    QTest::newRow("JPEG") << QStringLiteral("jpg");
    QTest::newRow("PNG") << QStringLiteral("png");
    QTest::newRow("TIFF") << QStringLiteral("tiff");
}

void TestMetadataWrite::writesAndRereadsEmbeddedMetadata()
{
    QFETCH(QString, format);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target =
            directory.filePath(QStringLiteral("photo.%1").arg(format));
    if (format == QLatin1String("jpg")) {
        QVERIFY(QFile::copy(fixture(), target));
    } else if (format == QLatin1String("png")) {
        const QString png = QDir(QStringLiteral(PIMIO_FIXTURES_DIR))
                                    .filePath(QStringLiteral("images/png-solid.png"));
        QVERIFY(QFile::copy(png, target));
    } else {
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY2(image.save(target, "TIFF"), "The pinned Qt image formats must write TIFF.");
    }
    const QByteArray sourceBytes = contents(target);

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
    QVERIFY(contents(target).contains("A portable caption"));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".xmp")));
    QVERIFY(!QFileInfo::exists(
            directory.filePath(QFileInfo(target).completeBaseName() + QStringLiteral(".xmp"))));
    QVERIFY(contents(target) != sourceBytes);
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
