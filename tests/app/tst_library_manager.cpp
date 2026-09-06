#include "pimio/app/library_manager.h"

#include "pimio/core/durable_store.h"
#include "pimio/lore/lore_durable_store.h"
#include "pimio/projection/projection_database.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio;

namespace {

core::MediaRecord managedRecord()
{
    core::MediaRecord record;
    record.id = core::MediaId(QStringLiteral("managed-photo"));
    record.fingerprint =
            core::ContentFingerprint(QStringLiteral("sha256"),
                                     QStringLiteral("41a5cec5e4cb716aacbd67940ca14ec7"
                                                    "bddf4f8e3f2822e7d19e86daaa903477"));
    record.identity.absolutePath = QStringLiteral("/import/photo.jpg");
    record.identity.sizeBytes = 16;
    record.originalStorage = core::MediaRecord::OriginalStorage::Managed;
    record.managedOriginalPath = QStringLiteral("originals/managed-photo.jpg");
    record.metadata.kind = core::MediaKind::Image;
    record.metadata.caption = QStringLiteral("Preserved caption");
    return record;
}

} // namespace

class TestLibraryManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void createOpenSwitchRenameAndMove();
    void missingAndLockedLibrariesFail();
    void backupRestorePreservesCanonicalStateAndRebuildsProjection();
};

void TestLibraryManager::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestLibraryManager::createOpenSwitchRenameAndMove()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    app::LibraryManager manager(temporary.filePath(QStringLiteral("registry.json")));
    core::Error error;

    qInfo("library-manager: creating first library");
    const auto first = manager.create(QStringLiteral("First"),
                                      temporary.filePath(QStringLiteral("first")), &error);
    QVERIFY2(first.has_value(), qPrintable(error.message()));
    QVERIFY(!first->id.isEmpty());
    QCOMPARE(manager.currentLibraryId(), first->id);

    qInfo("library-manager: creating second library");
    const auto second = manager.create(QStringLiteral("Second"),
                                       temporary.filePath(QStringLiteral("second")), &error);
    QVERIFY2(second.has_value(), qPrintable(error.message()));
    QVERIFY(second->id != first->id);
    QCOMPARE(manager.libraries().size(), 2);

    qInfo("library-manager: reopening first library");
    const auto reopened = manager.open(first->location, &error);
    QVERIFY2(reopened.has_value(), qPrintable(error.message()));
    QCOMPARE(reopened->id, first->id);
    QCOMPARE(manager.currentLibraryId(), first->id);

    qInfo("library-manager: renaming first library");
    QVERIFY2(manager.rename(first->id, QStringLiteral("Family"), &error),
             qPrintable(error.message()));
    qInfo("library-manager: rejecting nested move");
    QVERIFY(!manager.move(first->id,
                          QDir(first->location).filePath(QStringLiteral("nested/library")),
                          &error));
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(core::ErrorCode::Conflict));
    const QString movedPath = temporary.filePath(QStringLiteral("moved/family"));
    qInfo("library-manager: moving first library");
    QVERIFY2(manager.move(first->id, movedPath, &error), qPrintable(error.message()));
    qInfo("library-manager: opening moved library");
    const auto moved = manager.open(movedPath, &error);
    QVERIFY2(moved.has_value(), qPrintable(error.message()));
    QCOMPARE(moved->id, first->id);
    QCOMPARE(moved->name, QStringLiteral("Family"));
    QVERIFY(!QFileInfo::exists(first->location));
}

void TestLibraryManager::missingAndLockedLibrariesFail()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    app::LibraryManager manager(temporary.filePath(QStringLiteral("registry.json")));
    core::Error error;

    QVERIFY(!manager.open(temporary.filePath(QStringLiteral("missing")), &error));
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(core::ErrorCode::NotFound));

    const auto library = manager.create(QStringLiteral("Locked"),
                                        temporary.filePath(QStringLiteral("locked")), &error);
    QVERIFY2(library.has_value(), qPrintable(error.message()));
    lore::LoreDurableStore holder(QDir(library->location).filePath(QStringLiteral("store")));
    QVERIFY2(holder.open(&error), qPrintable(error.message()));
    QVERIFY(!manager.open(library->location, &error));
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(core::ErrorCode::Conflict));
    holder.close();
}

void TestLibraryManager::backupRestorePreservesCanonicalStateAndRebuildsProjection()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    app::LibraryManager manager(temporary.filePath(QStringLiteral("registry.json")));
    core::Error error;
    const auto library = manager.create(QStringLiteral("Portable"),
                                        temporary.filePath(QStringLiteral("source-library")),
                                        &error);
    QVERIFY2(library.has_value(), qPrintable(error.message()));

    const QString sourceFile = temporary.filePath(QStringLiteral("photo.jpg"));
    QFile source(sourceFile);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray originalBytes("managed-original");
    QCOMPARE(source.write(originalBytes), originalBytes.size());
    source.close();

    lore::LoreDurableStore store(QDir(library->location).filePath(QStringLiteral("store")));
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    const core::MediaRecord record = managedRecord();
    QVERIFY2(store.stageOriginal(record, sourceFile, &error), qPrintable(error.message()));
    QVERIFY2(store.commit(QStringLiteral("Import managed original"), &error).has_value(),
             qPrintable(error.message()));
    const auto originalDescriptor = store.libraryDescriptor(&error);
    const QList<core::Checkpoint> originalHistory = store.history(-1, &error);
    store.close();
    QVERIFY(originalDescriptor.has_value());

    const QString archive = temporary.filePath(QStringLiteral("portable.pimio-backup"));
    QVERIFY2(manager.backup(library->id, archive, &error), qPrintable(error.message()));
    QVERIFY(QFileInfo(archive).isFile());
    QVERIFY(QDir(library->location).removeRecursively());

    const QString restoredLocation = temporary.filePath(QStringLiteral("different/path"));
    const auto restored = manager.restore(archive, restoredLocation, &error);
    QVERIFY2(restored.has_value(), qPrintable(error.message()));
    QCOMPARE(restored->id, originalDescriptor->id);
    QCOMPARE(restored->location, QFileInfo(restoredLocation).absoluteFilePath());

    lore::LoreDurableStore restoredStore(
            QDir(restored->location).filePath(QStringLiteral("store")));
    QVERIFY2(restoredStore.open(&error), qPrintable(error.message()));
    QCOMPARE(restoredStore.libraryDescriptor(&error)->id, originalDescriptor->id);
    const auto restoredRecord = restoredStore.load(record.id, &error);
    QVERIFY2(restoredRecord.has_value(), qPrintable(error.message()));
    QCOMPARE(*restoredRecord, record);
    QCOMPARE(restoredStore.history(-1, &error), originalHistory);
    QFile restoredOriginal(restoredStore.originalPath(record, &error));
    QVERIFY(restoredOriginal.open(QIODevice::ReadOnly));
    QCOMPARE(restoredOriginal.readAll(), originalBytes);

    const QString projectionPath =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/library-indexes/") + restored->id
            + QStringLiteral("/projection.sqlite3");
    projection::ProjectionDatabase projection;
    QVERIFY2(projection.open(projectionPath, &error), qPrintable(error.message()));
    QCOMPARE(projection.recordCount(&error), 1);
    QVERIFY(!projection.isStale(restoredStore, &error));
}

QTEST_GUILESS_MAIN(TestLibraryManager)

#include "tst_library_manager.moc"
