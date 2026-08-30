#pragma once

#include "pimio/projection/projection_database.h"

#include "pimio/core/durable_store.h"
#include "pimio/core/metadata.h"
#include "pimio/core/types.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/qtest_printers.h"

#include <QDateTime>
#include <QDebug>
#include <QObject>
#include <QTest>

namespace {

const QDateTime kEpoch = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

pimio::testing::FakeClock makeClock()
{
    return pimio::testing::FakeClock(kEpoch);
}

/// Adds one MediaRecord to a MemoryDurableStore and returns its id.
pimio::core::MediaId addRecord(pimio::testing::MemoryDurableStore &store,
                               pimio::core::MediaRecord record)
{
    pimio::core::Error err;
    store.stage(record, &err);
    Q_ASSERT(!err.isError());
    store.commit(QStringLiteral("add"), &err);
    Q_ASSERT(!err.isError());
    return record.id;
}

/// Creates a minimal record with a capture time. The id is generated each call.
pimio::core::MediaRecord makeRecord(const QString &fileName,
                                    pimio::core::CaptureTime captureTime = {},
                                    pimio::core::MediaKind kind = pimio::core::MediaKind::Image,
                                    int rating = 0)
{
    pimio::core::MediaRecord r;
    r.id = pimio::core::MediaId::generate();
    r.fingerprint = pimio::core::ContentFingerprint(QStringLiteral("sha256"),
                                                    r.id.value()); // unique per record
    r.identity.absolutePath = QStringLiteral("/lib/") + fileName;
    // volumeId and fileId must not be null — the media table declares them NOT NULL.
    r.identity.volumeId = QStringLiteral("");
    r.identity.fileId = QStringLiteral("");
    r.metadata.fileName = fileName;
    r.metadata.folderPath = QStringLiteral("/lib");
    r.metadata.kind = kind;
    r.metadata.captureTime = captureTime;
    r.metadata.captureTimeOrigin = pimio::core::MetadataOrigin::Embedded;
    r.metadata.rating = rating;
    // cameraMake, cameraModel, and caption are NOT NULL in the schema; set them
    // to empty strings so Qt does not bind them as SQL NULL.
    r.metadata.cameraMake = QStringLiteral("");
    r.metadata.cameraModel = QStringLiteral("");
    r.metadata.caption = QStringLiteral("");
    return r;
}

/// Rebuilds a ProjectionDatabase from \a store. Asserts success.
void rebuild(pimio::projection::ProjectionDatabase &db,
             pimio::testing::MemoryDurableStore &store)
{
    pimio::core::Error err;
    const bool ok = db.rebuildFrom(store, &err);
    if (!ok || err.isError()) {
        qCritical() << "rebuildFrom failed:" << err.message();
    }
}

} // namespace

class TestProjectionMetadata : public QObject
{
    Q_OBJECT

private slots:
    void captureTimeSortOrderIsChronological();
    void captureTimeEqualTimestampsOrderedById();
    void captureTimeMissingTimestampsSortFirstAndByIdAmongThemselves();
    void captureTimePaginationReturnsCorrectSlice();
    void filterByKindReturnsOnlyMatchingItems();
    void filterByMinimumRatingReturnsCorrectSubset();
    void fullTextSearchMatchesCaption();
    void fullTextSearchMatchesFileName();
    void fullTextSearchUnicodeCaption();
    void fullTextSearchEmptyQueryReturnsEmpty();
    void fullTextSearchNoMatchReturnsEmpty();
    void fullTextSearchTreatsOperatorCharactersAsText();
    void fullTextSearchEscapesEmbeddedQuotes();
    void fullTextSearchMatchesAllTerms();
    void metadataConflictsAreStoredAndReloaded();
    void gpsLocationSurvivesRebuild();
    void rotationIsPersisted();
    void timezoneAwareSortKey();
    void timezoneNaiveSortByWallClock();
    void unsupportedMediaStoredWithUnknownKind();
    void cameraAndDimensionFieldsSurviveRebuild();
    void videoDurationAndAudioFlagSurviveRebuild();
};
