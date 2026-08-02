#include "pimio/scan/qt_file_system.h"

#include "pimio/core/error.h"
#include "pimio/core/types.h"

#include <memory>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::core;
using namespace pimio::scan;

/// Covers the production FileSystem implementation against a real, temporary
/// directory on disk. The abstract contract itself (permission failures,
/// disappearing files, atomic writes) is already exercised against
/// MemoryFileSystem in tst_core_contracts; this test instead proves
/// QtFileSystem satisfies that same contract when backed by a real
/// filesystem, which is what the shipped application actually uses.
class TestScanQtFileSystem : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void existsAndIsDirectoryReflectRealDisk();
    void listDirectoryReturnsEntriesWithIdentity();
    void listDirectoryOfMissingPathIsNotFound();
    void identifyReportsSizeAndModificationTime();
    void writeAtomicallyIsReadableAfterCommit();
    void removeIsIdempotentForAMissingFile();
    void makeDirectoriesCreatesNestedPath();
    void availableSpaceBytesIsPositiveForARealPath();
    void sameFileSurvivesARename();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestScanQtFileSystem::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void TestScanQtFileSystem::cleanup()
{
    m_dir.reset();
}

void TestScanQtFileSystem::existsAndIsDirectoryReflectRealDisk()
{
    QtFileSystem fs;
    const QString filePath = m_dir->filePath(QStringLiteral("plain.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("hello");
    file.close();

    QVERIFY(fs.exists(filePath));
    QVERIFY(fs.exists(m_dir->path()));
    QVERIFY(!fs.exists(m_dir->filePath(QStringLiteral("missing.txt"))));

    QVERIFY(fs.isDirectory(m_dir->path()));
    QVERIFY(!fs.isDirectory(filePath));
}

void TestScanQtFileSystem::listDirectoryReturnsEntriesWithIdentity()
{
    QtFileSystem fs;
    QFile a(m_dir->filePath(QStringLiteral("a.jpg")));
    QVERIFY(a.open(QIODevice::WriteOnly));
    a.write("aaaa");
    a.close();

    QVERIFY(QDir(m_dir->path()).mkdir(QStringLiteral("sub")));

    Error error;
    const QList<DirectoryEntry> entries = fs.listDirectory(m_dir->path(), &error);
    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QCOMPARE(entries.size(), 2);

    bool sawFile = false;
    bool sawDir = false;
    for (const DirectoryEntry &entry : entries) {
        if (entry.fileName == QStringLiteral("a.jpg")) {
            sawFile = true;
            QVERIFY(!entry.isDirectory);
            QVERIFY(entry.identity.isValid());
            QCOMPARE(entry.identity.sizeBytes, qint64(4));
        } else if (entry.fileName == QStringLiteral("sub")) {
            sawDir = true;
            QVERIFY(entry.isDirectory);
        }
    }
    QVERIFY(sawFile);
    QVERIFY(sawDir);
}

void TestScanQtFileSystem::listDirectoryOfMissingPathIsNotFound()
{
    QtFileSystem fs;
    Error error;
    const QList<DirectoryEntry> entries =
            fs.listDirectory(m_dir->filePath(QStringLiteral("does-not-exist")), &error);
    QVERIFY(entries.isEmpty());
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(ErrorCode::NotFound));
}

void TestScanQtFileSystem::identifyReportsSizeAndModificationTime()
{
    QtFileSystem fs;
    const QString path = m_dir->filePath(QStringLiteral("identify.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(10, 'x'));
    file.close();

    Error error;
    const FileIdentity identity = fs.identify(path, &error);
    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QVERIFY(identity.isValid());
    QCOMPARE(identity.sizeBytes, qint64(10));
    QVERIFY(identity.lastModified.isValid());
}

void TestScanQtFileSystem::writeAtomicallyIsReadableAfterCommit()
{
    QtFileSystem fs;
    const QString path = m_dir->filePath(QStringLiteral("atomic.txt"));

    Error error;
    QVERIFY(fs.writeAtomically(path, QByteArrayLiteral("payload"), &error));
    QVERIFY2(!error.isError(), qPrintable(error.message()));

    QByteArray readBack = fs.readAll(path, &error);
    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QCOMPARE(readBack, QByteArrayLiteral("payload"));

    // No stray temporary file should be left behind in the directory.
    const QStringList afterFiles = QDir(m_dir->path()).entryList(QDir::Files);
    QCOMPARE(afterFiles, QStringList{QStringLiteral("atomic.txt")});
}

void TestScanQtFileSystem::removeIsIdempotentForAMissingFile()
{
    QtFileSystem fs;
    Error error;
    QVERIFY(fs.remove(m_dir->filePath(QStringLiteral("never-existed.txt")), &error));
    QVERIFY(!error.isError());

    const QString path = m_dir->filePath(QStringLiteral("to-remove.txt"));
    QVERIFY(fs.writeAtomically(path, "x", &error));
    QVERIFY(fs.remove(path, &error));
    QVERIFY(!fs.exists(path));
}

void TestScanQtFileSystem::makeDirectoriesCreatesNestedPath()
{
    QtFileSystem fs;
    const QString nested = m_dir->filePath(QStringLiteral("one/two/three"));
    Error error;
    QVERIFY(fs.makeDirectories(nested, &error));
    QVERIFY(fs.isDirectory(nested));
}

void TestScanQtFileSystem::availableSpaceBytesIsPositiveForARealPath()
{
    QtFileSystem fs;
    QVERIFY(fs.availableSpaceBytes(m_dir->path()) > 0);
    QCOMPARE(fs.availableSpaceBytes(QStringLiteral("/definitely/not/a/real/path")), qint64(-1));
}

void TestScanQtFileSystem::sameFileSurvivesARename()
{
    QtFileSystem fs;
    const QString original = m_dir->filePath(QStringLiteral("before.jpg"));
    const QString renamed = m_dir->filePath(QStringLiteral("after.jpg"));

    Error error;
    QVERIFY(fs.writeAtomically(original, "content", &error));
    const FileIdentity before = fs.identify(original, &error);
    QVERIFY(before.isValid());

    QVERIFY(QFile::rename(original, renamed));
    const FileIdentity after = fs.identify(renamed, &error);
    QVERIFY(after.isValid());

    QVERIFY2(before.sameFileAs(after),
             "QtFileSystem should report the same volume/file id across a rename");
}

QTEST_GUILESS_MAIN(TestScanQtFileSystem)

#include "tst_scan_qt_file_system.moc"
