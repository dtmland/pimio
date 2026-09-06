#pragma once

#include <QObject>
#include <QTest>

class TestScanIncremental : public QObject
{
    Q_OBJECT

private slots:
    void hasherProducesConsistentFingerprint();
    void hasherDifferentDataProducesDifferentFingerprint();
    void hasherReadFromFileSystem();
    void scanAddsNewFiles();
    void failedScanCommitRetainsManagedImportForRetry();
    void batchedScanCommitsBeforeItFinishes();
    void anUnbatchedScanCommitsOnceAtTheEnd();
    void aCancelledBatchedScanKeepsWhatItAlreadyCommitted();
    void scanSkipsNonMediaFilesLikeDsStore();
    void scanRemovesAPreviouslyIndexedFileThatIsNoLongerMedia();
    void repeatedUnchangedScanMakesNoUpdates();
    void scanRemovesHistoricalSamePathDuplicates();
    void scanUpdatesChangedFile();
    void scanReadFailureKeepsManagedOriginal();
    void scanRemovesDeletedFile();
    void scanMigratesAnAvailableReferencedRecord();
    void scanRetainsAMissingReferencedRecordForMigration();
    void scanDetectsRenameInSameDirectory();
    void scanDetectsMoveToNewDirectory();
    void scanRecordsDuplicatesWithSeparateIds();
    void scanSkipsSymlinksByDefault();
    void scanFollowsSymlinksWhenEnabled();
    void scanRecordsPermissionFailureAsWarning();
    void scanHandlesDisappearingFileBetweenListAndRead();
    void scanIsIdempotentAfterRestart();
    void scanReturnsErrorForMissingRoot();
    void scanCancellationDiscardsChanges();
    void scanUsesMetadataReaderWhenAvailable();
    void largeTreeBenchmark();
};
