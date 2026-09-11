#include "pimio/editing/metadata_edit_service.h"

#include "pimio/testing/fake_clock.h"
#include "pimio/testing/memory_durable_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio;

namespace {

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

class FakeWriter final : public core::MetadataWriter
{
public:
    bool supportsEmbeddedWrite(const QString &) const override { return supported; }

    bool write(const QString &path, const core::MediaMetadata &metadata,
               const core::ContentFingerprint &expected, core::Error *error) override
    {
        ++calls;
        if (fail) {
            if (error) {
                *error = core::Error(core::ErrorCode::OutOfSpace,
                                     QStringLiteral("Injected write failure."));
            }
            return false;
        }
        if (fingerprint(path) != expected) {
            if (error) {
                *error = core::Error(core::ErrorCode::Conflict,
                                     QStringLiteral("Unexpected working copy."));
            }
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::Append)) {
            return false;
        }
        file.write(QByteArrayLiteral("\nmetadata=") + QByteArray::number(metadata.rating));
        return true;
    }

    bool supported = true;
    bool fail = false;
    int calls = 0;
};

core::MediaRecord managedRecord(const QString &path)
{
    core::MediaRecord record;
    record.id = core::MediaId(QStringLiteral("media-1"));
    record.originalStorage = core::MediaRecord::OriginalStorage::Managed;
    record.managedOriginalPath = QStringLiteral("originals/media-1.jpg");
    record.fingerprint = fingerprint(path);
    record.metadata.kind = core::MediaKind::Image;
    record.recipe.setRevision(2);
    return record;
}

} // namespace

class TestMetadataEditService : public QObject
{
    Q_OBJECT

private slots:
    void stageCancelAndSave();
    void conflictsPreserveOriginal();
    void failedWriteAndCommitRemainRecoverable();
};

void TestMetadataEditService::stageCancelAndSave()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("original.jpg"));
    QFile source(QDir(QStringLiteral(PIMIO_FIXTURES_DIR))
                         .filePath(QStringLiteral("images/jpeg-no-exif.jpg")));
    QVERIFY(source.copy(path));
    const QByteArray before = contents(path);

    testing::FakeClock clock(QDateTime::fromString(QStringLiteral("2026-09-11T00:00:00Z"),
                                                   Qt::ISODate));
    testing::MemoryDurableStore store(clock);
    core::Error error;
    QVERIFY(store.stage(managedRecord(path), &error));
    QVERIFY(store.commit(QStringLiteral("Import"), &error).has_value());
    store.setOriginalPath(core::MediaId(QStringLiteral("media-1")), path);
    FakeWriter writer;
    editing::MetadataEditService edits(store, writer);

    core::MediaRecord original = *store.load(core::MediaId(QStringLiteral("media-1")), &error);
    core::MediaMetadata metadata = original.metadata;
    metadata.rating = 4;
    core::EditRecipe recipe = original.recipe;
    recipe.append(core::EditOperation(core::EditOperationKind::Rotate, {{"degrees", 90}}));
    QVERIFY(edits.stage(original.id, metadata, recipe, &error));
    QCOMPARE(edits.staged(original.id)->recipe.revision(), 3);
    QCOMPARE(contents(path), before);
    QVERIFY(edits.cancel(&error));
    QCOMPARE(contents(path), before);

    QVERIFY(edits.stage(original.id, metadata, recipe, &error));
    const auto checkpoint = edits.save(QStringLiteral("Rate and rotate"), &error);
    QVERIFY2(checkpoint.has_value(), qPrintable(error.message()));
    QCOMPARE(checkpoint->message, QStringLiteral("Rate and rotate"));
    QVERIFY(!checkpoint->authorId.isEmpty());
    QVERIFY(!checkpoint->applicationVersion.isEmpty());
    QVERIFY(!checkpoint->parentId.isEmpty());
    const auto saved = store.load(original.id, &error);
    QCOMPARE(saved->metadata.rating, 4);
    QCOMPARE(saved->recipe.revision(), 3);
    QVERIFY(saved->fingerprint != original.fingerprint);
    QVERIFY(contents(path) != before);
}

void TestMetadataEditService::conflictsPreserveOriginal()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("original.jpg"));
    QFile source(QDir(QStringLiteral(PIMIO_FIXTURES_DIR))
                         .filePath(QStringLiteral("images/jpeg-no-exif.jpg")));
    QVERIFY(source.copy(path));

    testing::FakeClock clock(QDateTime::fromString(QStringLiteral("2026-09-11T00:00:00Z"),
                                                   Qt::ISODate));
    testing::MemoryDurableStore store(clock);
    core::Error error;
    const core::MediaRecord record = managedRecord(path);
    QVERIFY(store.stage(record, &error));
    QVERIFY(store.commit(QStringLiteral("Import"), &error).has_value());
    store.setOriginalPath(record.id, path);
    FakeWriter writer;
    editing::MetadataEditService edits(store, writer);
    core::MediaMetadata metadata = record.metadata;
    metadata.caption = QStringLiteral("edited");
    QVERIFY(edits.stage(record.id, metadata, record.recipe, &error));

    QFile external(path);
    QVERIFY(external.open(QIODevice::Append));
    external.write("external");
    external.close();
    const QByteArray changed = contents(path);
    QVERIFY(!edits.save(QStringLiteral("Save"), &error));
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(core::ErrorCode::Conflict));
    QCOMPARE(contents(path), changed);
    QCOMPARE(writer.calls, 0);
}

void TestMetadataEditService::failedWriteAndCommitRemainRecoverable()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("original.jpg"));
    QFile source(QDir(QStringLiteral(PIMIO_FIXTURES_DIR))
                         .filePath(QStringLiteral("images/jpeg-no-exif.jpg")));
    QVERIFY(source.copy(path));
    const QByteArray before = contents(path);

    testing::FakeClock clock(QDateTime::fromString(QStringLiteral("2026-09-11T00:00:00Z"),
                                                   Qt::ISODate));
    testing::MemoryDurableStore store(clock);
    core::Error error;
    const core::MediaRecord record = managedRecord(path);
    QVERIFY(store.stage(record, &error));
    QVERIFY(store.commit(QStringLiteral("Import"), &error).has_value());
    store.setOriginalPath(record.id, path);
    FakeWriter writer;
    editing::MetadataEditService edits(store, writer);
    core::MediaMetadata metadata = record.metadata;
    metadata.rating = 2;
    QVERIFY(edits.stage(record.id, metadata, record.recipe, &error));

    writer.fail = true;
    QVERIFY(!edits.save(QStringLiteral("Save"), &error));
    QCOMPARE(contents(path), before);
    QVERIFY(edits.hasStagedEdits());
    QVERIFY(!store.hasStagedChanges());

    writer.fail = false;
    store.failNextCommit(core::ErrorCode::OutOfSpace);
    QVERIFY(!edits.save(QStringLiteral("Save"), &error));
    QVERIFY(store.hasStagedChanges());
    QCOMPARE(contents(path), before);
    QVERIFY(edits.save(QStringLiteral("Save"), &error).has_value());
    QVERIFY(!edits.hasStagedEdits());
    QVERIFY(contents(path) != before);
}

QTEST_APPLESS_MAIN(TestMetadataEditService)

#include "tst_metadata_edit_service.moc"
