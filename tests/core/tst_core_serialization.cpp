#include "pimio/core/durable_store.h"
#include "pimio/core/edit_recipe.h"
#include "pimio/core/error.h"
#include "pimio/core/job.h"
#include "pimio/core/metadata.h"
#include "pimio/core/serialization.h"
#include "pimio/core/types.h"

#include "pimio/testing/qtest_printers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace pimio::core;

namespace {

MediaMetadata sampleMetadata()
{
    MediaMetadata metadata;
    metadata.kind = MediaKind::Image;
    metadata.fileName = QStringLiteral("DSC_0001.jpg");
    metadata.folderPath = QStringLiteral("/library/2019/05");
    metadata.captureTime = CaptureTime::fromOffset(
            QDateTime(QDate(2019, 5, 4), QTime(13, 45, 12, 250)), 2 * 3600);
    metadata.captureTimeOrigin = MetadataOrigin::Embedded;
    metadata.cameraMake = QStringLiteral("Nikon");
    metadata.cameraModel = QStringLiteral("D750");
    metadata.lensModel = QStringLiteral("50mm f/1.8");
    metadata.pixelWidth = 6016;
    metadata.pixelHeight = 4016;
    metadata.rotationDegrees = 90;
    metadata.rating = 4;
    metadata.caption = QStringLiteral("Ünïcode caption 日本語");
    metadata.tags = {QStringLiteral("holiday"), QStringLiteral("família")};
    metadata.location = GeoLocation::create(48.8584, 2.2945);

    MetadataConflict conflict;
    conflict.field = QStringLiteral("captureTime");
    conflict.preferredOrigin = MetadataOrigin::Embedded;
    conflict.conflictingOrigin = MetadataOrigin::Sidecar;
    conflict.preferredValue = QStringLiteral("2019-05-04T13:45:12");
    conflict.conflictingValue = QStringLiteral("2019-05-04T11:45:12");
    metadata.conflicts = {conflict};

    return metadata;
}

EditRecipe sampleRecipe()
{
    EditRecipe recipe;
    recipe.setRevision(3);
    recipe.append(EditOperation(EditOperationKind::Crop,
                                QJsonObject{{QStringLiteral("x"), 10},
                                            {QStringLiteral("y"), 20},
                                            {QStringLiteral("width"), 800},
                                            {QStringLiteral("height"), 600}}));
    recipe.append(EditOperation(EditOperationKind::Rotate,
                                QJsonObject{{QStringLiteral("degrees"), 90}}));
    return recipe;
}

QJsonObject reparse(const QJsonObject &object)
{
    // Going through the text form proves the record survives real storage,
    // not just an in-process copy.
    const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return QJsonDocument::fromJson(encoded).object();
}

} // namespace

class TestCoreSerialization : public QObject
{
    Q_OBJECT

private slots:
    void metadataRoundTripPreservesValues();
    void metadataRoundTripPreservesUnknownFields();
    void metadataNormalizeClampsValues();
    void captureTimeDistinguishesMissingOffset();
    void captureTimeOrderingIsDeterministic();
    void geoLocationRejectsOutOfRangeValues();
    void editRecipeRoundTripPreservesUnknownOperations();
    void jobRecordRoundTripPreservesUnknownFields();
    void jobOrderingIsDeterministic();
    void errorRoundTripPreservesCodeAndContext();
    void mediaRecordRoundTrip();
    void everyRecordWritesSchemaVersion();
};

void TestCoreSerialization::metadataRoundTripPreservesValues()
{
    const MediaMetadata original = sampleMetadata();
    const MediaMetadata restored = MediaMetadata::fromJson(reparse(original.toJson()));

    PIMIO_COMPARE_ENUM(restored.kind, original.kind);
    QCOMPARE(restored.fileName, original.fileName);
    QCOMPARE(restored.folderPath, original.folderPath);
    QCOMPARE(restored.captureTime, original.captureTime);
    PIMIO_COMPARE_ENUM(restored.captureTimeOrigin, original.captureTimeOrigin);
    QCOMPARE(restored.cameraMake, original.cameraMake);
    QCOMPARE(restored.cameraModel, original.cameraModel);
    QCOMPARE(restored.lensModel, original.lensModel);
    QCOMPARE(restored.pixelWidth, original.pixelWidth);
    QCOMPARE(restored.pixelHeight, original.pixelHeight);
    QCOMPARE(restored.rotationDegrees, original.rotationDegrees);
    QCOMPARE(restored.rating, original.rating);
    QCOMPARE(restored.caption, original.caption);
    QCOMPARE(restored.tags, original.tags);
    QCOMPARE(restored.location, original.location);
    QCOMPARE(restored.conflicts, original.conflicts);
    QCOMPARE(restored, original);
}

void TestCoreSerialization::metadataRoundTripPreservesUnknownFields()
{
    QJsonObject written = sampleMetadata().toJson();
    // Simulate a record produced by a newer pimio release.
    written.insert(QStringLiteral("schemaVersion"), kRecordSchemaVersion + 1);
    written.insert(QStringLiteral("faceRegions"),
                   QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Ada")}}});
    written.insert(QStringLiteral("futureScalar"), 42);

    const MediaMetadata restored = MediaMetadata::fromJson(reparse(written));
    QCOMPARE(restored.unrecognizedFields().size(), 2);
    QVERIFY(restored.unrecognizedFields().contains(QStringLiteral("faceRegions")));
    QVERIFY(restored.unrecognizedFields().contains(QStringLiteral("futureScalar")));

    const QJsonObject rewritten = restored.toJson();
    QCOMPARE(rewritten.value(QStringLiteral("futureScalar")).toInt(), 42);
    QCOMPARE(rewritten.value(QStringLiteral("faceRegions")),
             written.value(QStringLiteral("faceRegions")));

    // The writer records its own schema version rather than echoing a version
    // it cannot actually produce.
    QCOMPARE(rewritten.value(QStringLiteral("schemaVersion")).toInt(), kRecordSchemaVersion);
}

void TestCoreSerialization::metadataNormalizeClampsValues()
{
    MediaMetadata metadata;
    metadata.rotationDegrees = -90;
    metadata.rating = 9;
    metadata.pixelWidth = -5;
    metadata.durationMs = -1;
    metadata.location = GeoLocation();
    metadata.normalize();

    QCOMPARE(metadata.rotationDegrees, 270);
    QCOMPARE(metadata.rating, 5);
    QCOMPARE(metadata.pixelWidth, 0);
    QCOMPARE(metadata.durationMs, 0);
    QVERIFY(!metadata.location.has_value());

    metadata.rotationDegrees = 450;
    metadata.rating = -3;
    metadata.normalize();
    QCOMPARE(metadata.rotationDegrees, 90);
    QCOMPARE(metadata.rating, 0);
}

void TestCoreSerialization::captureTimeDistinguishesMissingOffset()
{
    const QDateTime wallClock(QDate(2019, 5, 4), QTime(13, 45, 12));

    const CaptureTime withoutOffset = CaptureTime::fromLocalWallClock(wallClock);
    QVERIFY(withoutOffset.isValid());
    QVERIFY(!withoutOffset.hasKnownOffset());
    QVERIFY(!withoutOffset.toUtc().has_value());

    const CaptureTime withOffset = CaptureTime::fromOffset(wallClock, -5 * 3600);
    QVERIFY(withOffset.hasKnownOffset());
    const auto asUtc = withOffset.toUtc();
    QVERIFY(asUtc.has_value());
    QCOMPARE(*asUtc, QDateTime(QDate(2019, 5, 4), QTime(18, 45, 12), Qt::UTC));

    // The distinction must survive storage; an absent offset never becomes
    // a zero offset.
    const CaptureTime restored = CaptureTime::fromJson(reparse(withoutOffset.toJson()));
    QVERIFY(!restored.hasKnownOffset());
    QCOMPARE(restored.wallClock(), withoutOffset.wallClock());
}

void TestCoreSerialization::captureTimeOrderingIsDeterministic()
{
    const CaptureTime invalid;
    const CaptureTime earlier =
            CaptureTime::fromLocalWallClock(QDateTime(QDate(2019, 5, 4), QTime(10, 0)));
    const CaptureTime later =
            CaptureTime::fromOffset(QDateTime(QDate(2019, 5, 4), QTime(11, 0)), 3600);

    QVERIFY(invalid.sortKeyMSecs() < earlier.sortKeyMSecs());
    QVERIFY(earlier.sortKeyMSecs() < later.sortKeyMSecs());

    // Equal wall clocks produce equal keys regardless of offset knowledge, so
    // callers must break ties themselves rather than relying on chance.
    const CaptureTime sameWallClockNoOffset =
            CaptureTime::fromLocalWallClock(QDateTime(QDate(2019, 5, 4), QTime(11, 0)));
    QCOMPARE(sameWallClockNoOffset.sortKeyMSecs(), later.sortKeyMSecs());
}

void TestCoreSerialization::geoLocationRejectsOutOfRangeValues()
{
    QVERIFY(GeoLocation::create(0.0, 0.0).has_value());
    QVERIFY(GeoLocation::create(90.0, 180.0).has_value());
    QVERIFY(!GeoLocation::create(90.1, 0.0).has_value());
    QVERIFY(!GeoLocation::create(0.0, -180.1).has_value());
    QVERIFY(!GeoLocation::create(qQNaN(), 0.0).has_value());

    auto location = GeoLocation::create(51.5, -0.12);
    QVERIFY(location.has_value());
    location->setAltitudeMetres(35.5);

    const auto restored = GeoLocation::fromJson(reparse(location->toJson()));
    QVERIFY(restored.has_value());
    QCOMPARE(*restored, *location);
    QVERIFY(restored->altitudeMetres().has_value());
    QCOMPARE(*restored->altitudeMetres(), 35.5);

    QVERIFY(!GeoLocation::fromJson(QJsonObject{{QStringLiteral("latitude"), 999.0},
                                               {QStringLiteral("longitude"), 0.0}})
                     .has_value());
}

void TestCoreSerialization::editRecipeRoundTripPreservesUnknownOperations()
{
    EditRecipe recipe = sampleRecipe();
    QVERIFY(recipe.isFullyRecognized());

    QJsonObject written = recipe.toJson();
    QJsonArray operations = written.value(QStringLiteral("operations")).toArray();
    operations.append(QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("perspectiveCorrection")},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("angle"), 2.5}}}});
    written.insert(QStringLiteral("operations"), operations);
    written.insert(QStringLiteral("futureRecipeField"), QStringLiteral("keep me"));

    const EditRecipe restored = EditRecipe::fromJson(reparse(written));
    QCOMPARE(restored.revision(), 3);
    QCOMPARE(restored.operations().size(), 3);
    QVERIFY(!restored.isFullyRecognized());
    QCOMPARE(restored.operations().at(2).rawKind(), QStringLiteral("perspectiveCorrection"));

    const QJsonObject rewritten = restored.toJson();
    QCOMPARE(rewritten.value(QStringLiteral("operations")).toArray().size(), 3);
    QCOMPARE(rewritten.value(QStringLiteral("operations")).toArray().at(2).toObject(),
             operations.at(2).toObject());
    QCOMPARE(rewritten.value(QStringLiteral("futureRecipeField")).toString(),
             QStringLiteral("keep me"));
}

void TestCoreSerialization::jobRecordRoundTripPreservesUnknownFields()
{
    JobRecord record;
    record.id = JobId(QStringLiteral("job-1"));
    record.kind = JobKind::GenerateThumbnail;
    record.priority = JobPriority::Interactive;
    record.state = JobState::Failed;
    record.coalescingKey = QStringLiteral("sha256-abc/thumbnail");
    record.payload = QJsonObject{{QStringLiteral("mediaId"), QStringLiteral("m-1")}};
    record.attempts = 1;
    record.maxAttempts = 3;
    record.createdAt = QDateTime(QDate(2024, 1, 2), QTime(3, 4, 5), Qt::UTC);
    record.lastError = Error(ErrorCode::Timeout, QStringLiteral("Timed out."));

    QJsonObject written = record.toJson();
    written.insert(QStringLiteral("futureJobField"), true);

    const JobRecord restored = JobRecord::fromJson(reparse(written));
    PIMIO_COMPARE_ID(restored.id, record.id);
    PIMIO_COMPARE_ENUM(restored.kind, record.kind);
    PIMIO_COMPARE_ENUM(restored.priority, record.priority);
    PIMIO_COMPARE_ENUM(restored.state, record.state);
    QCOMPARE(restored.coalescingKey, record.coalescingKey);
    QCOMPARE(restored.payload, record.payload);
    QCOMPARE(restored.attempts, record.attempts);
    QCOMPARE(restored.createdAt, record.createdAt);
    QCOMPARE(restored.lastError, record.lastError);
    QVERIFY(restored.canRetry());
    QCOMPARE(restored.toJson().value(QStringLiteral("futureJobField")).toBool(), true);

    // An unknown job kind must survive rather than being silently rewritten
    // into a different kind of work.
    JobRecord unknownKind = restored;
    unknownKind.kind = JobKind::Unknown;
    PIMIO_COMPARE_ENUM(JobRecord::fromJson(unknownKind.toJson()).kind, JobKind::Unknown);
}

void TestCoreSerialization::jobOrderingIsDeterministic()
{
    const QDateTime base(QDate(2024, 1, 1), QTime(0, 0), Qt::UTC);

    JobRecord interactive;
    interactive.id = JobId(QStringLiteral("b"));
    interactive.priority = JobPriority::Interactive;
    interactive.createdAt = base.addSecs(60);

    JobRecord background;
    background.id = JobId(QStringLiteral("a"));
    background.priority = JobPriority::Background;
    background.createdAt = base;

    QVERIFY(interactive.runsBefore(background));
    QVERIFY(!background.runsBefore(interactive));

    JobRecord sameEverything = interactive;
    sameEverything.id = JobId(QStringLiteral("a"));
    QVERIFY(sameEverything.runsBefore(interactive));
    QVERIFY(!interactive.runsBefore(sameEverything));

    JobRecord noCreationTime = interactive;
    noCreationTime.createdAt = QDateTime();
    QVERIFY(interactive.runsBefore(noCreationTime));
    QVERIFY(!noCreationTime.runsBefore(interactive));
}

void TestCoreSerialization::errorRoundTripPreservesCodeAndContext()
{
    Error error(ErrorCode::OutOfSpace, QStringLiteral("No space left."));
    error.setContext(QJsonObject{{QStringLiteral("path"), QStringLiteral("/library/a.jpg")}});

    const Error restored = Error::fromJson(reparse(error.toJson()));
    QCOMPARE(restored, error);
    QVERIFY(restored.isError());
    QVERIFY(!restored.isRetryable());

    QVERIFY(Error(ErrorCode::Timeout, QString()).isRetryable());
    QVERIFY(Error(ErrorCode::StorageUnavailable, QString()).isRetryable());
    QVERIFY(!Error().isError());

    // An unrecognized code must not be mistaken for success.
    const Error unknown = Error::fromJson(
            QJsonObject{{QStringLiteral("code"), QStringLiteral("somethingNewer")}});
    PIMIO_COMPARE_ENUM(unknown.code(), ErrorCode::Internal);
    QVERIFY(unknown.isError());
}

void TestCoreSerialization::mediaRecordRoundTrip()
{
    MediaRecord record;
    record.id = MediaId(QStringLiteral("media-1"));
    record.fingerprint = ContentFingerprint(QStringLiteral("sha256"), QStringLiteral("abcdef"));
    record.identity.absolutePath = QStringLiteral("/library/2019/05/DSC_0001.jpg");
    record.identity.volumeId = QStringLiteral("vol-1");
    record.identity.fileId = QStringLiteral("42");
    record.identity.sizeBytes = 1234567;
    record.identity.lastModified = QDateTime(QDate(2019, 5, 4), QTime(13, 45, 12), Qt::UTC);
    record.metadata = sampleMetadata();
    record.recipe = sampleRecipe();

    const MediaRecord restored = MediaRecord::fromJson(reparse(record.toJson()));
    PIMIO_COMPARE_ID(restored.id, record.id);
    QCOMPARE(restored.fingerprint.cacheKey(), QStringLiteral("sha256-abcdef"));
    QCOMPARE(restored.identity.absolutePath, record.identity.absolutePath);
    QCOMPARE(restored.identity.sizeBytes, record.identity.sizeBytes);
    QCOMPARE(restored.identity.lastModified, record.identity.lastModified);
    QCOMPARE(restored.metadata, record.metadata);
    QCOMPARE(restored.recipe, record.recipe);
}

void TestCoreSerialization::everyRecordWritesSchemaVersion()
{
    const QList<QJsonObject> records = {
        sampleMetadata().toJson(),
        sampleRecipe().toJson(),
        JobRecord().toJson(),
        MediaRecord().toJson(),
    };

    for (const QJsonObject &record : records) {
        QVERIFY(record.contains(QStringLiteral("schemaVersion")));
        QCOMPARE(record.value(QStringLiteral("schemaVersion")).toInt(), kRecordSchemaVersion);
    }
}

QTEST_APPLESS_MAIN(TestCoreSerialization)

#include "tst_core_serialization.moc"
