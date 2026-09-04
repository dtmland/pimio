#pragma once

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

namespace {

/// Total bytes below \a path, used to report the on-disk cost of a repository.
qint64 directorySizeBytes(const QString &path)
{
    qint64 total = 0;
    QDirIterator iterator(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        total += iterator.fileInfo().size();
    }
    return total;
}

int stagedFileCount(const QString &storePath)
{
    int count = 0;
    QDirIterator iterator(storePath + QStringLiteral("/staging"),
                          QStringList{QStringLiteral("*.json")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        ++count;
    }
    return count;
}

bool makeReadOnly(const QString &path)
{
    return QFile::setPermissions(path,
                                 QFileDevice::ReadOwner | QFileDevice::ExeOwner
                                     | QFileDevice::ReadGroup | QFileDevice::ExeGroup);
}

void makeWritableAgain(const QString &path)
{
    QFile::setPermissions(path,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                              | QFileDevice::ReadGroup | QFileDevice::ExeGroup);
}

} // namespace

/// Increment 2c: the fault, concurrency and recovery gate.
///
/// These tests decide the increment. Each one asserts an invariant that must
/// hold for LORE to be an acceptable ground truth for v1:
///
///   * an uncommitted change is never reported as committed;
///   * a failed commit leaves staged work recoverable;
///   * derived state can be deleted without losing committed state; and
///   * a change made by another process is detectable.
///
/// See docs/decisions/0001-lore-durable-store.md for the recorded decision.
class TestLoreFaults : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void killedProcessBeforeCommitLeavesNoUncommittedStateVisible();
    void lore090InterruptedCommitOpenFailuresAreObservational();
    void killedProcessDuringCommitLeavesAConsistentRepository();
    void killedProcessAfterCommitKeepsTheRevisionItReported();
    void unwritableCheckoutFailsVisiblyAndKeepsStagedWork();
    void deletingTheCheckoutLosesNoCommittedState();
    void corruptCheckoutFileIsRecoverable();
    void interruptedWriteMarkerIsRepairedOnOpen();
    void concurrentWriterNeverProducesAPartialResult();
    void largeRecordSetReportsSizeAndLatency();

private:
    QString m_helperPath;
};
