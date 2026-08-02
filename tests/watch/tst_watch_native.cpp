#include "pimio/watch/qt_directory_watch_adapter.h"

#include "pimio/core/error.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::watch;
using namespace pimio::core;

/// Native integration test for the shipped WatchAdapter.
///
/// Unlike tst_watch_contract (which drives EventCoalescer with synthetic
/// events and never touches a disk), this test performs real file operations
/// under a real, temporary directory and waits for QtDirectoryWatchAdapter to
/// report them through actual OS-level filesystem notifications
/// (inotify on Linux, FSEvents/kqueue on macOS, ReadDirectoryChangesW on
/// Windows, all reached through QFileSystemWatcher). It is intentionally the
/// same test on every platform: nothing here is Linux-specific, which is
/// what "a portable behavioral contract" means in practice. Timeouts are
/// generous because real OS notification latency is not deterministic.
class TestWatchNative : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startFailsForAMissingDirectory();
    void creatingAFileIsObserved();
    void removingAFileIsObserved();
    void renamingAFileIsObserved();
    void aNewSubdirectoryIsWatchedAutomatically();
    void stopSuppressesFurtherEvents();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestWatchNative::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void TestWatchNative::cleanup()
{
    m_dir.reset();
}

void TestWatchNative::startFailsForAMissingDirectory()
{
    QtDirectoryWatchAdapter adapter;
    Error error;
    QVERIFY(!adapter.start(m_dir->filePath(QStringLiteral("does-not-exist")), &error));
    QVERIFY(error.isError());
    QVERIFY(!adapter.isWatching());
}

void TestWatchNative::creatingAFileIsObserved()
{
    QtDirectoryWatchAdapter adapter;
    QSignalSpy spy(&adapter, &WatchAdapter::eventOccurred);

    Error error;
    QVERIFY(adapter.start(m_dir->path(), &error));
    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QVERIFY(adapter.isWatching());

    QFile file(m_dir->filePath(QStringLiteral("new-file.jpg")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    QVERIFY2(spy.wait(15000), "No watch event observed for a created file within the timeout");

    bool sawCreate = false;
    for (const QList<QVariant> &call : spy) {
        const WatchEvent event = call.at(0).value<WatchEvent>();
        if (event.kind == WatchEventKind::Created && event.path.endsWith("new-file.jpg")) {
            sawCreate = true;
        }
    }
    QVERIFY2(sawCreate, "Expected a Created event for the new file");
}

void TestWatchNative::removingAFileIsObserved()
{
    const QString filePath = m_dir->filePath(QStringLiteral("to-remove.jpg"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    QtDirectoryWatchAdapter adapter;
    QSignalSpy spy(&adapter, &WatchAdapter::eventOccurred);

    Error error;
    QVERIFY(adapter.start(m_dir->path(), &error));

    QVERIFY(QFile::remove(filePath));

    QVERIFY2(spy.wait(15000), "No watch event observed for a removed file within the timeout");

    bool sawRemove = false;
    for (const QList<QVariant> &call : spy) {
        const WatchEvent event = call.at(0).value<WatchEvent>();
        if (event.kind == WatchEventKind::Removed && event.path.endsWith("to-remove.jpg")) {
            sawRemove = true;
        }
    }
    QVERIFY2(sawRemove, "Expected a Removed event for the deleted file");
}

void TestWatchNative::renamingAFileIsObserved()
{
    const QString before = m_dir->filePath(QStringLiteral("before.jpg"));
    const QString after = m_dir->filePath(QStringLiteral("after.jpg"));
    QFile file(before);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    QtDirectoryWatchAdapter adapter;
    QSignalSpy spy(&adapter, &WatchAdapter::eventOccurred);

    Error error;
    QVERIFY(adapter.start(m_dir->path(), &error));

    QVERIFY(QFile::rename(before, after));

    // QFileSystemWatcher reports a rename as the directory changing (this
    // adapter does not depend on a native rename-cookie signal); the
    // observable outcome is a Removed for the old name and a Created for
    // the new one, exactly the raw events EventCoalescer already knows how
    // to fold into a single reconcile.
    QVERIFY2(spy.wait(15000), "No watch event observed for a rename within the timeout");
    QTest::qWait(500); // let the second half of the pair arrive too

    bool sawOldGone = false;
    bool sawNewPresent = false;
    for (const QList<QVariant> &call : spy) {
        const WatchEvent event = call.at(0).value<WatchEvent>();
        if (event.path.endsWith("before.jpg") && event.kind == WatchEventKind::Removed) {
            sawOldGone = true;
        }
        if (event.path.endsWith("after.jpg") && event.kind == WatchEventKind::Created) {
            sawNewPresent = true;
        }
    }
    QVERIFY2(sawOldGone, "Expected the old name to be reported as removed");
    QVERIFY2(sawNewPresent, "Expected the new name to be reported as created");
}

void TestWatchNative::aNewSubdirectoryIsWatchedAutomatically()
{
    QtDirectoryWatchAdapter adapter;
    QSignalSpy spy(&adapter, &WatchAdapter::eventOccurred);

    Error error;
    QVERIFY(adapter.start(m_dir->path(), &error));

    QVERIFY(QDir(m_dir->path()).mkdir(QStringLiteral("album")));
    QVERIFY2(spy.wait(15000), "No watch event observed for the new subdirectory");
    spy.clear();

    // A file created inside the newly discovered subdirectory must also be
    // observed, proving the adapter started watching it automatically
    // rather than only ever watching the original root.
    QFile file(m_dir->filePath(QStringLiteral("album/inside.jpg")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    QVERIFY2(spy.wait(15000), "No watch event observed inside the new subdirectory");
    bool sawInside = false;
    for (const QList<QVariant> &call : spy) {
        const WatchEvent event = call.at(0).value<WatchEvent>();
        if (event.path.endsWith("album/inside.jpg")) {
            sawInside = true;
        }
    }
    QVERIFY2(sawInside, "Expected the file inside the new subdirectory to be observed");
}

void TestWatchNative::stopSuppressesFurtherEvents()
{
    QtDirectoryWatchAdapter adapter;
    QSignalSpy spy(&adapter, &WatchAdapter::eventOccurred);

    Error error;
    QVERIFY(adapter.start(m_dir->path(), &error));
    adapter.stop();
    QVERIFY(!adapter.isWatching());

    QFile file(m_dir->filePath(QStringLiteral("after-stop.jpg")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    // Give the OS a moment to have delivered a notification if it were
    // going to; none should arrive because the adapter stopped watching.
    QTest::qWait(500);
    QCOMPARE(spy.count(), 0);
}

QTEST_GUILESS_MAIN(TestWatchNative)

#include "tst_watch_native.moc"
