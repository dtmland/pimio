#include "lore_test_support.h"

#include "pimio/lore/lore_durable_store.h"
#include "pimio/testing/qtest_printers.h"

#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::core;
using namespace pimio::lore;
using namespace pimio::testing;

/// Increment 2b: the LORE adapter behind the DurableStore boundary.
///
/// These tests exercise the adapter against a real, local, offline LORE
/// repository in a temporary directory. Nothing here touches scanning, SQLite,
/// or the UI.
class TestLoreAdapter : public QObject
{
    Q_OBJECT

private slots:
    void libraryReportsItsVersion();
    void missingLibraryDegradesToUnavailable();
    void savedRecordsSurviveCloseAndReopen();
    void stagedWorkIsNotVisibleAsCommittedState();
    void discardStagedLeavesCommittedStateIntact();
    void historyIsNewestFirstAndCarriesCommitMessages();
    void libraryIdentitySurvivesMoveAndCheckpointsCarryProvenance();
    void externalCliCommitChangesTheStateToken();
    void promotionRejectsInvalidServerUrl();
    void unicodeAndOpaqueIdentifiersRoundTrip();
    void commitCostGrowsWithBatchSize();
    void commitsDoNotCopyTheRepository();
};

void TestLoreAdapter::promotionRejectsInvalidServerUrl()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY(!store.promoteToServer(QStringLiteral("file:///not-a-server"), &error));
    PIMIO_COMPARE_ENUM(error.code(), ErrorCode::Internal);
}

void TestLoreAdapter::libraryReportsItsVersion()
{
    PIMIO_SKIP_WITHOUT_LORE();
    QCOMPARE(loadedLibraryVersion(), QStringLiteral(PIMIO_LORE_EXPECTED_VERSION));
    QVERIFY(!defaultLibraryPath().isEmpty());
}

void TestLoreAdapter::missingLibraryDegradesToUnavailable()
{
    // The adapter must never make a missing library fatal. Opening a store
    // under a path that cannot be created exercises the same visible-failure
    // path without disturbing the process-wide library handle.
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString blockedPath = temporary.filePath(QStringLiteral("file/store"));
    QFile blocker(temporary.filePath(QStringLiteral("file")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("not a directory");
    blocker.close();

    LoreDurableStore store(blockedPath);
    QVERIFY(!store.isAvailable());

    Error error;
    QVERIFY(!store.open(&error));
    QVERIFY(error.isError());

    // Every operation still answers, and none of them claims success.
    Error stageError;
    QVERIFY(!store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("x")), &stageError));
    PIMIO_COMPARE_ENUM(stageError.code(), ErrorCode::StorageUnavailable);

    Error commitError;
    QVERIFY(!store.commit(QStringLiteral("Save"), &commitError).has_value());
    PIMIO_COMPARE_ENUM(commitError.code(), ErrorCode::StorageUnavailable);

    Error loadError;
    QVERIFY(!store.load(MediaId(QStringLiteral("m-1")), &loadError).has_value());
    PIMIO_COMPARE_ENUM(loadError.code(), ErrorCode::StorageUnavailable);

    QVERIFY(store.listIds(nullptr).isEmpty());
    QVERIFY(store.history(-1, nullptr).isEmpty());
    QVERIFY(store.stateToken().isEmpty());
    QVERIFY(!store.hasStagedChanges());
}

void TestLoreAdapter::savedRecordsSurviveCloseAndReopen()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));

    QString tokenAfterCommit;
    {
        LoreDurableStore store(storePath);
        Error error;
        QVERIFY2(store.open(&error), qPrintable(error.message()));
        QVERIFY(store.isAvailable());

        QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("first")),
                            &error));
        QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-2"), QStringLiteral("second")),
                            &error));
        const auto checkpoint = store.commit(QStringLiteral("Save captions"), &error);
        QVERIFY2(checkpoint.has_value(), qPrintable(error.message()));
        QCOMPARE(checkpoint->message, QStringLiteral("Save captions"));
        QVERIFY(!checkpoint->id.isEmpty());
        QVERIFY(checkpoint->createdAtUtc.isValid());
        QCOMPARE(checkpoint->createdAtUtc.timeSpec(), Qt::UTC);

        tokenAfterCommit = store.stateToken();
        QVERIFY(!tokenAfterCommit.isEmpty());
        store.close();
    }

    // A second store object over the same path is the closest in-process
    // equivalent of restarting the application.
    LoreDurableStore reopened(storePath);
    Error error;
    QVERIFY2(reopened.open(&error), qPrintable(error.message()));

    QCOMPARE(reopened.stateToken(), tokenAfterCommit);
    QCOMPARE(reopened.listIds(nullptr).size(), 2);

    const auto loaded = reopened.load(MediaId(QStringLiteral("m-1")), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error.message()));
    QCOMPARE(loaded->metadata.caption, QStringLiteral("first"));
    PIMIO_COMPARE_ID(loaded->id, MediaId(QStringLiteral("m-1")));
    QCOMPARE(loaded->identity.sizeBytes, 4096);
    PIMIO_COMPARE_ENUM(loaded->metadata.kind, MediaKind::Image);

    Error missingError;
    QVERIFY(!reopened.load(MediaId(QStringLiteral("absent")), &missingError).has_value());
    PIMIO_COMPARE_ENUM(missingError.code(), ErrorCode::NotFound);
}

void TestLoreAdapter::stagedWorkIsNotVisibleAsCommittedState()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    const QString tokenBefore = store.stateToken();

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("draft")), &error));
    QVERIFY(store.hasStagedChanges());

    // The central invariant of the gate: staged work is never readable as
    // committed state, and it never moves the state token.
    Error loadError;
    QVERIFY(!store.load(MediaId(QStringLiteral("m-1")), &loadError).has_value());
    PIMIO_COMPARE_ENUM(loadError.code(), ErrorCode::NotFound);
    QVERIFY(store.listIds(nullptr).isEmpty());
    QCOMPARE(store.stateToken(), tokenBefore);
    QVERIFY(store.history(-1, nullptr).isEmpty());

    QVERIFY(store.commit(QStringLiteral("Save"), &error).has_value());
    QVERIFY(!store.hasStagedChanges());
    QVERIFY(store.load(MediaId(QStringLiteral("m-1")), nullptr).has_value());
    QVERIFY(store.stateToken() != tokenBefore);
}

void TestLoreAdapter::discardStagedLeavesCommittedStateIntact()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("kept")), &error));
    QVERIFY(store.commit(QStringLiteral("Keep"), &error).has_value());
    const QString tokenAfterCommit = store.stateToken();

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-2"), QStringLiteral("thrown away")),
                        &error));
    // Overwriting an already committed record must also be discardable.
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("edited")), &error));
    QVERIFY(store.hasStagedChanges());

    QVERIFY(store.discardStaged(&error));
    QVERIFY(!store.hasStagedChanges());
    QCOMPARE(store.stateToken(), tokenAfterCommit);
    QCOMPARE(store.listIds(nullptr).size(), 1);
    QVERIFY(!store.load(MediaId(QStringLiteral("m-2")), nullptr).has_value());

    const auto kept = store.load(MediaId(QStringLiteral("m-1")), nullptr);
    QVERIFY(kept.has_value());
    QCOMPARE(kept->metadata.caption, QStringLiteral("kept"));

    // A discard with nothing staged is not an error, and committing nothing is
    // a visible conflict rather than an empty checkpoint.
    QVERIFY(store.discardStaged(&error));
    Error emptyCommitError;
    QVERIFY(!store.commit(QStringLiteral("Nothing"), &emptyCommitError).has_value());
    PIMIO_COMPARE_ENUM(emptyCommitError.code(), ErrorCode::Conflict);
}

void TestLoreAdapter::historyIsNewestFirstAndCarriesCommitMessages()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("one")), &error));
    const auto first = store.commit(QStringLiteral("First save"), &error);
    QVERIFY2(first.has_value(), qPrintable(error.message()));

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-2"), QStringLiteral("two")), &error));
    const auto second = store.commit(QStringLiteral("Second save"), &error);
    QVERIFY2(second.has_value(), qPrintable(error.message()));

    const QList<Checkpoint> all = store.history(-1, &error);
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.at(0).id, second->id);
    QCOMPARE(all.at(0).message, QStringLiteral("Second save"));
    QCOMPARE(all.at(1).id, first->id);
    QCOMPARE(all.at(1).message, QStringLiteral("First save"));
    QVERIFY(all.at(0).createdAtUtc.isValid());
    QVERIFY(all.at(0).createdAtUtc >= all.at(1).createdAtUtc);

    const QList<Checkpoint> limited = store.history(1, &error);
    QCOMPARE(limited.size(), 1);
    QCOMPARE(limited.constFirst().id, second->id);
}

void TestLoreAdapter::libraryIdentitySurvivesMoveAndCheckpointsCarryProvenance()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString originalPath = temporary.filePath(QStringLiteral("original"));
    QString libraryId;
    QString authorId;
    QString creationCheckpointId;
    {
        LoreDurableStore store(originalPath);
        Error error;
        QVERIFY2(store.open(&error), qPrintable(error.message()));
        QVERIFY2(store.createLibrary(QStringLiteral("Family archive"), &error),
                 qPrintable(error.message()));
        const auto descriptor = store.libraryDescriptor(&error);
        QVERIFY2(descriptor.has_value(), qPrintable(error.message()));
        libraryId = descriptor->id;
        authorId = descriptor->localUser.id;

        const QList<Checkpoint> createdHistory = store.history(-1, &error);
        QCOMPARE(createdHistory.size(), 1);
        creationCheckpointId = createdHistory.constFirst().id;
        QCOMPARE(createdHistory.constFirst().authorId, authorId);
        QVERIFY(!createdHistory.constFirst().applicationVersion.isEmpty());
        QVERIFY(createdHistory.constFirst().parentId.isEmpty());

        QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("one")), &error));
        const auto imported = store.commit(QStringLiteral("Import"), &error);
        QVERIFY2(imported.has_value(), qPrintable(error.message()));
        QCOMPARE(imported->authorId, authorId);
        QCOMPARE(imported->parentId, creationCheckpointId);
        QCOMPARE(store.listIds(&error), QList<MediaId>{MediaId(QStringLiteral("m-1"))});
    }

    const QString movedPath = temporary.filePath(QStringLiteral("moved"));
    QVERIFY(QDir().rename(originalPath, movedPath));
    LoreDurableStore moved(movedPath);
    Error error;
    QVERIFY2(moved.open(&error), qPrintable(error.message()));
    const auto movedDescriptor = moved.libraryDescriptor(&error);
    QVERIFY2(movedDescriptor.has_value(), qPrintable(error.message()));
    QCOMPARE(movedDescriptor->id, libraryId);

    LoreDurableStore independent(temporary.filePath(QStringLiteral("independent")));
    QVERIFY2(independent.open(&error), qPrintable(error.message()));
    QVERIFY2(independent.createLibrary(QStringLiteral("Family archive"), &error),
             qPrintable(error.message()));
    const auto independentDescriptor = independent.libraryDescriptor(&error);
    QVERIFY(independentDescriptor.has_value());
    QVERIFY(independentDescriptor->id != libraryId);

    const QList<Checkpoint> movedHistory = moved.history(-1, &error);
    QCOMPARE(movedHistory.size(), 2);
    QCOMPARE(movedHistory.constFirst().message, QStringLiteral("Import"));
    QCOMPARE(movedHistory.constFirst().authorId, authorId);
    QCOMPARE(movedHistory.constFirst().parentId, creationCheckpointId);
}

void TestLoreAdapter::externalCliCommitChangesTheStateToken()
{
    PIMIO_SKIP_WITHOUT_LORE();
    PIMIO_SKIP_WITHOUT_LORE_CLI();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("mine")), &error));
    QVERIFY2(store.commit(QStringLiteral("Mine"), &error).has_value(), qPrintable(error.message()));
    const QString tokenAfterCommit = store.stateToken();
    QVERIFY(!tokenAfterCommit.isEmpty());

    // Release LORE's handles so the CLI is a genuinely separate writer.
    store.close();

    const QString repository = store.repositoryPath();
    const QString external = repository + QStringLiteral("/records/no/note.txt");
    QVERIFY(QDir().mkpath(QFileInfo(external).absolutePath()));
    QFile file(external);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("written by another tool");
    file.close();

    QString output;
    QVERIFY2(runLoreCli(repository,
                        {QStringLiteral("file"), QStringLiteral("stage"),
                         QDir::toNativeSeparators(external)},
                        &output),
             qPrintable(output));
    QVERIFY2(runLoreCli(repository,
                        {QStringLiteral("revision"), QStringLiteral("commit"),
                         QStringLiteral("External change")},
                        &output),
             qPrintable(output));

    QVERIFY2(store.open(&error), qPrintable(error.message()));
    const QString tokenAfterExternalChange = store.stateToken();
    QVERIFY(!tokenAfterExternalChange.isEmpty());
    QVERIFY(tokenAfterExternalChange != tokenAfterCommit);

    const QList<Checkpoint> history = store.history(-1, &error);
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.constFirst().message, QStringLiteral("External change"));

    // pimio's own committed records are untouched by the external writer.
    QVERIFY(store.load(MediaId(QStringLiteral("m-1")), nullptr).has_value());
}

void TestLoreAdapter::unicodeAndOpaqueIdentifiersRoundTrip()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    // Media ids are opaque. The adapter must not assume they are filesystem
    // safe, and it must not collide on case-insensitive filesystems.
    const QStringList ids{
        MediaId::generate().value(),
        QStringLiteral("ünïcode/../id"),
        QStringLiteral("with space"),
        QStringLiteral("MixedCase"),
        QStringLiteral("mixedcase"),
    };
    for (const QString &id : ids) {
        QVERIFY2(store.stage(makeLoreRecord(id, QStringLiteral("caption for %1").arg(id)), &error),
                 qPrintable(error.message()));
    }
    QVERIFY2(store.commit(QStringLiteral("Opaque identifiers"), &error).has_value(),
             qPrintable(error.message()));

    QCOMPARE(store.listIds(nullptr).size(), ids.size());
    for (const QString &id : ids) {
        const auto record = store.load(MediaId(id), &error);
        QVERIFY2(record.has_value(), qPrintable(id));
        QCOMPARE(record->metadata.caption, QStringLiteral("caption for %1").arg(id));
    }
}

void TestLoreAdapter::commitCostGrowsWithBatchSize()
{
    PIMIO_SKIP_WITHOUT_LORE();

    // Commit granularity is the main performance unknown for library-scale
    // ingest, so it is measured here rather than discovered during scanning.
    // The assertion is deliberately weak: the point is to publish a number,
    // not to enforce a threshold before a stable baseline exists.
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    constexpr int kBatchSize = 200;
    QElapsedTimer timer;

    timer.start();
    for (int index = 0; index < kBatchSize; ++index) {
        QVERIFY(store.stage(makeLoreRecord(QStringLiteral("batch-%1").arg(index, 5, 10,
                                                                         QLatin1Char('0')),
                                           QStringLiteral("caption %1").arg(index)),
                            &error));
    }
    const qint64 stageMs = timer.elapsed();

    timer.restart();
    QVERIFY2(store.commit(QStringLiteral("Batch of %1").arg(kBatchSize), &error).has_value(),
             qPrintable(error.message()));
    const qint64 batchCommitMs = timer.elapsed();

    timer.restart();
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("single"), QStringLiteral("one")), &error));
    QVERIFY2(store.commit(QStringLiteral("Single record"), &error).has_value(),
             qPrintable(error.message()));
    const qint64 singleCommitMs = timer.elapsed();

    qInfo("staged %d records in %lld ms; batched commit %lld ms; single-record commit %lld ms",
          kBatchSize, static_cast<long long>(stageMs), static_cast<long long>(batchCommitMs),
          static_cast<long long>(singleCommitMs));

    QCOMPARE(store.listIds(nullptr).size(), kBatchSize + 1);
    QCOMPARE(store.history(-1, nullptr).size(), 2);
}

void TestLoreAdapter::commitsDoNotCopyTheRepository()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));
    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("saved")), &error));
    QVERIFY2(store.commit(QStringLiteral("Save"), &error).has_value(), qPrintable(error.message()));

    QVERIFY(!QFileInfo::exists(storePath + QStringLiteral("/.pimio-lore-backup")));
    QVERIFY(!QFileInfo::exists(
        storePath + QStringLiteral("/.pimio-lore-commit-in-progress")));
}

QTEST_GUILESS_MAIN(TestLoreAdapter)

#include "tst_lore_adapter.moc"
