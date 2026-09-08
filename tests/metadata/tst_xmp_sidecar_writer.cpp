#include "pimio/metadata/builtin_metadata_reader.h"
#include "pimio/metadata/xmp_sidecar_writer.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio;

class TestXmpSidecarWriter : public QObject
{
    Q_OBJECT

private slots:
    void preservesOriginalAndUnknownXmp();
    void detectsSidecarRaceAndPreservesPriorFile();
    void failedAtomicWriteDoesNotCreatePartialFile();
};

namespace {

core::MediaMetadata metadata()
{
    core::MediaMetadata value;
    value.caption = QStringLiteral("Changed caption");
    value.rating = 5;
    value.tags = {QStringLiteral("family"), QStringLiteral("travel")};
    return value;
}

QString imageFixture()
{
    return QDir(QStringLiteral(PIMIO_FIXTURES_DIR))
            .filePath(QStringLiteral("images/jpeg-exif-offset.jpg"));
}

} // namespace

void TestXmpSidecarWriter::preservesOriginalAndUnknownXmp()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString image = directory.filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::copy(imageFixture(), image));
    QFile source(image);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray original = source.readAll();
    source.close();

    const QString sidecar = directory.filePath(QStringLiteral("photo.xmp"));
    QFile xmp(sidecar);
    QVERIFY(xmp.open(QIODevice::WriteOnly));
    QVERIFY(xmp.write("<?xpacket begin=\"\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
                      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                      "<rdf:Description xmlns:vendor=\"urn:vendor\" vendor:keep=\"yes\"/>"
                      "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>") > 0);
    xmp.close();

    metadata::XmpSidecarWriter writer;
    core::Error error;
    const auto snapshot = writer.snapshot(image, &error);
    QVERIFY2(!error.isError(), qPrintable(error.message()));
    core::EditRecipe recipe;
    recipe.setRevision(3);
    recipe.append(core::EditOperation(core::EditOperationKind::Rotate,
                                      {{QStringLiteral("degrees"), 90}}));
    QVERIFY2(writer.write(snapshot, metadata(), recipe, &error), qPrintable(error.message()));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), original);
    QVERIFY(xmp.open(QIODevice::ReadOnly));
    const QByteArray written = xmp.readAll();
    QVERIFY(written.contains("vendor:keep=\"yes\""));

    metadata::BuiltinMetadataReader reader;
    const auto reread = reader.read(image, &error);
    QVERIFY2(reread.has_value(), qPrintable(error.message()));
    QCOMPARE(reread->metadata.caption, QStringLiteral("Changed caption"));
    QCOMPARE(reread->metadata.rating, 5);
    QCOMPARE(reread->metadata.tags, metadata().tags);
    QVERIFY(reread->hasRecipe);
    QCOMPARE(reread->recipe, recipe);
}

void TestXmpSidecarWriter::detectsSidecarRaceAndPreservesPriorFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString image = directory.filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::copy(imageFixture(), image));
    metadata::XmpSidecarWriter writer;
    core::Error error;
    const auto stale = writer.snapshot(image, &error);
    QFile external(writer.sidecarPathFor(image));
    QVERIFY(external.open(QIODevice::WriteOnly));
    QVERIFY(external.write("external update") > 0);
    external.close();

    QVERIFY(!writer.write(stale, metadata(), {}, &error));
    QCOMPARE(error.code(), core::ErrorCode::Conflict);
    QVERIFY(external.open(QIODevice::ReadOnly));
    QCOMPARE(external.readAll(), QByteArray("external update"));
}

void TestXmpSidecarWriter::failedAtomicWriteDoesNotCreatePartialFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    metadata::XmpSidecarWriter writer;
    metadata::XmpSidecarWriter::Snapshot snapshot;
    snapshot.path = directory.filePath(QStringLiteral("missing/photo.xmp"));
    core::Error error;
    QVERIFY(!writer.write(snapshot, metadata(), {}, &error));
    QVERIFY(error.isError());
    QVERIFY(!QFile::exists(snapshot.path));
}

QTEST_APPLESS_MAIN(TestXmpSidecarWriter)

#include "tst_xmp_sidecar_writer.moc"
