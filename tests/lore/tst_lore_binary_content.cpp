#include "lore_test_support.h"

#include "pimio/lore/lore_durable_store.h"
#include "pimio/scan/qt_file_system.h"
#include "pimio/scan/scanner.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace pimio::core;
using namespace pimio::lore;
using namespace pimio::testing;

namespace {

constexpr qint64 kDefaultSizeMiB = 8;
constexpr qint64 kMiB = 1024 * 1024;

qint64 spikeSize()
{
    bool ok = false;
    const int configured = qEnvironmentVariableIntValue("PIMIO_LORE_BINARY_SPIKE_MIB", &ok);
    return (ok && configured > 0 ? configured : kDefaultSizeMiB) * kMiB;
}

bool writeBinaryFile(const QString &path, qint64 size)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QByteArray block(1024 * 1024, Qt::Uninitialized);
    quint32 state = 0x9e3779b9U;
    qint64 remaining = size;
    while (remaining > 0) {
        const qint64 count = std::min(remaining, static_cast<qint64>(block.size()));
        for (qint64 index = 0; index < count; ++index) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            block[index] = static_cast<char>(state);
        }
        if (file.write(block.constData(), count) != count) {
            return false;
        }
        remaining -= count;
    }
    file.close();
    return true;
}

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result();
}

qint64 directorySize(const QString &path)
{
    qint64 total = 0;
    QDirIterator entries(path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        total += QFileInfo(entries.next()).size();
    }
    return total;
}

bool copyDirectory(const QString &sourcePath, const QString &destinationPath)
{
    const QDir source(sourcePath);
    if (!QDir().mkpath(destinationPath)) {
        return false;
    }

    QDirIterator entries(sourcePath,
                         QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        const QString sourceEntry = entries.next();
        const QString relative = source.relativeFilePath(sourceEntry);
        const QString destinationEntry = destinationPath + QLatin1Char('/') + relative;
        const QFileInfo info(sourceEntry);
        if (info.isDir()) {
            if (!QDir().mkpath(destinationEntry)) {
                return false;
            }
        } else {
            if (!QDir().mkpath(QFileInfo(destinationEntry).absolutePath())
                || !QFile::copy(sourceEntry, destinationEntry)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

class TestLoreBinaryContent : public QObject
{
    Q_OBJECT

private slots:
    void commitRestartReloadAndDeduplicate();
    void scannerIngestsManagedOriginal();
    void failedManagedCommitRetainsRecordAndBytesForRetry();
};

void TestLoreBinaryContent::commitRestartReloadAndDeduplicate()
{
    PIMIO_SKIP_WITHOUT_LORE();
    PIMIO_SKIP_WITHOUT_LORE_CLI();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY2(store.createLibrary(QStringLiteral("Binary storage spike"), &error),
             qPrintable(error.message()));
    const auto descriptor = store.libraryDescriptor(&error);
    QVERIFY2(descriptor.has_value(), qPrintable(error.message()));
    const QString libraryId = descriptor->id;
    store.close();

    const QString storePath = store.storePath();
    const QString repository = store.repositoryPath();
    const QString originals = repository + QStringLiteral("/originals");
    QVERIFY(QDir().mkpath(originals));
    const QString original = originals + QStringLiteral("/representative-video.bin");
    const QString duplicate = originals + QStringLiteral("/duplicate-video.bin");
    const qint64 contentSize = spikeSize();
    QVERIFY(writeBinaryFile(original, contentSize));
    const QByteArray expectedHash = fileHash(original);
    QVERIFY(!expectedHash.isEmpty());

    QString output;
    QElapsedTimer timer;
    timer.start();
    QVERIFY2(runLoreCli(repository,
                        {QStringLiteral("file"), QStringLiteral("stage"),
                         QDir::toNativeSeparators(original)},
                        &output),
             qPrintable(output));
    const qint64 stageMilliseconds = timer.elapsed();

    timer.restart();
    QVERIFY2(runLoreCli(repository,
                        {QStringLiteral("revision"), QStringLiteral("commit"),
                         QStringLiteral("Managed-original feasibility spike")},
                        &output),
             qPrintable(output));
    const qint64 commitMilliseconds = timer.elapsed();
    const qint64 loreSizeAfterOriginal = directorySize(repository + QStringLiteral("/.lore"));
    const qint64 repositorySizeAfterOriginal = directorySize(repository);
    const qint64 checkoutSizeAfterOriginal =
            repositorySizeAfterOriginal - loreSizeAfterOriginal;

    QVERIFY(QFile::remove(original));
    timer.restart();
    QVERIFY2(runLoreCli(repository,
                        {QStringLiteral("file"), QStringLiteral("reset"),
                         QDir::toNativeSeparators(original)},
                        &output),
             qPrintable(output));
    const qint64 reloadMilliseconds = timer.elapsed();
    QCOMPARE(fileHash(original), expectedHash);

    QVERIFY(QFile::copy(original, duplicate));
    QVERIFY2(runLoreCli(repository,
                        {QStringLiteral("file"), QStringLiteral("stage"),
                         QDir::toNativeSeparators(duplicate)},
                        &output),
             qPrintable(output));
    timer.restart();
    QVERIFY2(runLoreCli(repository,
                        {QStringLiteral("revision"), QStringLiteral("commit"),
                         QStringLiteral("Duplicate-content feasibility spike")},
                        &output),
             qPrintable(output));
    const qint64 duplicateCommitMilliseconds = timer.elapsed();
    const qint64 loreSizeAfterDuplicate = directorySize(repository + QStringLiteral("/.lore"));
    const qint64 duplicateStoreGrowth = loreSizeAfterDuplicate - loreSizeAfterOriginal;

    QVERIFY2(
        duplicateStoreGrowth < contentSize / 4,
        qPrintable(QStringLiteral("Identical content grew .lore by %1 bytes for a %2-byte file.")
                       .arg(duplicateStoreGrowth)
                       .arg(contentSize)));
    QCOMPARE(fileHash(duplicate), expectedHash);

    const qint64 repositorySizeBeforeMetadata = directorySize(repository);
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("large-corpus-record"),
                                       QStringLiteral("metadata-only edit")),
                        &error));
    timer.restart();
    QVERIFY2(store.commit(QStringLiteral("Small metadata edit"), &error).has_value(),
             qPrintable(error.message()));
    const qint64 metadataCommitMilliseconds = timer.elapsed();
    store.close();
    const qint64 metadataCommitGrowth = directorySize(repository) - repositorySizeBeforeMetadata;
    QVERIFY(!QFileInfo::exists(storePath + QStringLiteral("/.pimio-lore-backup")));

    const QString backupPath = temporary.filePath(QStringLiteral("backup"));
    timer.restart();
    QVERIFY(copyDirectory(storePath, backupPath));
    const qint64 backupMilliseconds = timer.elapsed();
    const qint64 backupSize = directorySize(backupPath);
    QCOMPARE(backupSize, directorySize(storePath));

    QVERIFY(QDir(storePath).removeRecursively());
    const QString restoredPath = temporary.filePath(QStringLiteral("restored"));
    timer.restart();
    QVERIFY(copyDirectory(backupPath, restoredPath));
    const qint64 restoreMilliseconds = timer.elapsed();

    LoreDurableStore restored(restoredPath);
    QVERIFY2(restored.open(&error), qPrintable(error.message()));
    const auto restoredDescriptor = restored.libraryDescriptor(&error);
    QVERIFY2(restoredDescriptor.has_value(), qPrintable(error.message()));
    QCOMPARE(restoredDescriptor->id, libraryId);
    const auto restoredRecord =
            restored.load(MediaId(QStringLiteral("large-corpus-record")), &error);
    QVERIFY2(restoredRecord.has_value(), qPrintable(error.message()));
    QCOMPARE(restoredRecord->metadata.caption, QStringLiteral("metadata-only edit"));
    restored.close();

    const QString restoredOriginal =
            restored.repositoryPath() + QStringLiteral("/originals/representative-video.bin");
    QVERIFY(QFile::remove(restoredOriginal));
    QVERIFY2(runLoreCli(restored.repositoryPath(),
                        {QStringLiteral("file"), QStringLiteral("reset"),
                         QDir::toNativeSeparators(restoredOriginal)},
                        &output),
             qPrintable(output));
    QCOMPARE(fileHash(restoredOriginal), expectedHash);

    qInfo().noquote() << QStringLiteral("LORE binary spike: bytes=%1 stage_ms=%2 commit_ms=%3 "
                                        "reload_ms=%4 "
                                        "repository_bytes=%5 checkout_bytes=%6 lore_bytes=%7 "
                                        "duplicate_commit_ms=%8 duplicate_lore_growth_bytes=%9 "
                                        "metadata_commit_ms=%10 metadata_commit_growth_bytes=%11 "
                                        "backup_bytes=%12 backup_ms=%13 restore_ms=%14")
                             .arg(contentSize)
                             .arg(stageMilliseconds)
                             .arg(commitMilliseconds)
                             .arg(reloadMilliseconds)
                             .arg(repositorySizeAfterOriginal)
                             .arg(checkoutSizeAfterOriginal)
                             .arg(loreSizeAfterOriginal)
                             .arg(duplicateCommitMilliseconds)
                             .arg(duplicateStoreGrowth)
                             .arg(metadataCommitMilliseconds)
                             .arg(metadataCommitGrowth)
                             .arg(backupSize)
                             .arg(backupMilliseconds)
                             .arg(restoreMilliseconds);
}

void TestLoreBinaryContent::scannerIngestsManagedOriginal()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourceRoot = temporary.filePath(QStringLiteral("import"));
    QVERIFY(QDir().mkpath(sourceRoot));
    const QString sourcePath = sourceRoot + QStringLiteral("/photo.jpg");
    QVERIFY(writeBinaryFile(sourcePath, 2 * kMiB));
    QByteArray expectedHash = fileHash(sourcePath);

    const QString storePath = temporary.filePath(QStringLiteral("store"));
    {
        LoreDurableStore store(storePath);
        Error error;
        QVERIFY2(store.open(&error), qPrintable(error.message()));
        QVERIFY(store.createLibrary(QStringLiteral("Managed"), &error));
        pimio::scan::QtFileSystem fileSystem;
        pimio::scan::Scanner scanner(&fileSystem, nullptr, &store);
        std::atomic<bool> cancelled{false};
        pimio::scan::Scanner::Result result;
        QVERIFY2(!scanner.scan({sourceRoot}, cancelled, &result).isError(),
                 qPrintable(error.message()));
        QCOMPARE(result.added, 1);

        const MediaId id = store.listIds(&error).constFirst();
        const auto record = store.load(id, &error);
        QVERIFY(record.has_value());
        QCOMPARE(record->originalStorage, MediaRecord::OriginalStorage::Managed);
        QVERIFY(!record->managedOriginalPath.isEmpty());
        const QString resolvedPath = store.originalPath(*record, &error);
        QVERIFY2(!resolvedPath.isEmpty(), qPrintable(error.message()));
        QVERIFY2(QFileInfo::exists(resolvedPath), qPrintable(resolvedPath));
        QCOMPARE(fileHash(resolvedPath), expectedHash);

        QFile changedSource(sourcePath);
        QVERIFY(changedSource.open(QIODevice::Append));
        QCOMPARE(changedSource.write("changed"), qint64(7));
        changedSource.close();
        expectedHash = fileHash(sourcePath);
        result = {};
        QVERIFY(!scanner.scan({sourceRoot}, cancelled, &result).isError());
        QCOMPARE(result.updated, 1);
        const auto updatedRecord = store.load(id, &error);
        QVERIFY(updatedRecord.has_value());
        const QString updatedPath = store.originalPath(*updatedRecord, &error);
        QVERIFY(updatedPath != resolvedPath);
        QCOMPARE(fileHash(updatedPath), expectedHash);
        QVERIFY(!QFileInfo::exists(resolvedPath));

        QVERIFY(QFile::remove(sourcePath));
        result = {};
        QVERIFY(!scanner.scan({sourceRoot}, cancelled, &result).isError());
        QCOMPARE(result.removed, 0);
        QVERIFY(store.load(id, &error).has_value());
    }

    LoreDurableStore reopened(storePath);
    Error error;
    QVERIFY2(reopened.open(&error), qPrintable(error.message()));
    const MediaId id = reopened.listIds(&error).constFirst();
    const auto record = reopened.load(id, &error);
    QVERIFY(record.has_value());
    const QString managedPath = reopened.originalPath(*record, &error);
    QCOMPARE(fileHash(managedPath), expectedHash);

    QVERIFY(QFile::remove(managedPath));
    QVERIFY2(reopened.restoreFromDurableState(&error), qPrintable(error.message()));
    QCOMPARE(fileHash(managedPath), expectedHash);

    QVERIFY(reopened.remove(id, &error));
    QVERIFY(reopened.commit(QStringLiteral("Remove managed original"), &error).has_value());
    QVERIFY(!QFileInfo::exists(managedPath));
}

void TestLoreBinaryContent::failedManagedCommitRetainsRecordAndBytesForRetry()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source.jpg"));
    QVERIFY(writeBinaryFile(source, kMiB));

    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY(store.open(&error));
    QVERIFY(store.createLibrary(QStringLiteral("Retry"), &error));

    MediaRecord record;
    record.id = MediaId(QStringLiteral("retry-original"));
    record.fingerprint = ContentFingerprint(
            QStringLiteral("sha256"), QString::fromLatin1(fileHash(source).toHex()));
    record.identity.absolutePath = source;
    record.identity.sizeBytes = QFileInfo(source).size();
    record.originalStorage = MediaRecord::OriginalStorage::Managed;
    record.managedOriginalPath = QStringLiteral("originals/re/retry-original.jpg");
    QVERIFY(store.stageOriginal(record, source, &error));

    const QString destination = store.originalPath(record, &error);
    QVERIFY(QDir().mkpath(destination));
    const auto failed = store.commit(QStringLiteral("Blocked"), &error);
    QVERIFY(!failed.has_value());
    QVERIFY(store.hasStagedChanges());
    QVERIFY(!store.load(record.id, nullptr).has_value());

    QDir(destination).removeRecursively();
    QVERIFY2(store.commit(QStringLiteral("Retry"), &error).has_value(),
             qPrintable(error.message()));
    const auto loaded = store.load(record.id, &error);
    QVERIFY(loaded.has_value());
    QCOMPARE(fileHash(store.originalPath(*loaded, &error)), fileHash(source));
}

QTEST_GUILESS_MAIN(TestLoreBinaryContent)

#include "tst_lore_binary_content.moc"
