#include "lore_test_support.h"

#include "pimio/lore/lore_durable_store.h"

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
                                        "repository_bytes=%5 lore_bytes=%6 duplicate_commit_ms=%7 "
                                        "duplicate_lore_growth_bytes=%8 metadata_commit_ms=%9 "
                                        "metadata_commit_growth_bytes=%10 backup_bytes=%11 "
                                        "backup_ms=%12 restore_ms=%13")
                             .arg(contentSize)
                             .arg(stageMilliseconds)
                             .arg(commitMilliseconds)
                             .arg(reloadMilliseconds)
                             .arg(repositorySizeAfterOriginal)
                             .arg(loreSizeAfterOriginal)
                             .arg(duplicateCommitMilliseconds)
                             .arg(duplicateStoreGrowth)
                             .arg(metadataCommitMilliseconds)
                             .arg(metadataCommitGrowth)
                             .arg(backupSize)
                             .arg(backupMilliseconds)
                             .arg(restoreMilliseconds);
}

QTEST_GUILESS_MAIN(TestLoreBinaryContent)

#include "tst_lore_binary_content.moc"
