#pragma once

#include "pimio/projection/projection_database.h"
#include "pimio/testing/fake_clock.h"

#include <QDateTime>
#include <QObject>
#include <QTest>

#include <memory>

class TestBrowserModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rowCountMatchesProjectionSize();
    void emptyProjectionYieldsZeroRows();
    void mediaIdRoleReturnsStableId();
    void absolutePathRoleReturnsPath();
    void managedOriginalPathIsResolvedByStore();
    void captureTimeStringRoleReturnsIsoString();
    void mediaKindRoleReturnsImageForImages();
    void thumbnailStatusStartsAsPending();
    void itemAtReturnsDetailRoles();
    void setVisibleRangeRequestsThumbnailsForWindow();
    void visibleRangeIsInvokableFromQml();
    void setVisibleRangeWithPrefetchExpandsWindow();
    void setVisibleRangeChangesCancelsPreviousRequests();
    void thumbnailResultTransitionsStatusToReady();
    void invalidThumbnailResultTransitionsStatusToError();
    void duplicateContentRequestsCompleteIndependently();
    void thumbnailErrorTransitionsStatusToError();
    void thumbnailResultIsPushedToTheImageProvider();
    void cancelledThumbnailResetsStatusToPending();
    void reloadClearsExistingItems();
    void setSortingReordersRows();
    void unknownSortKeyKeepsTheCurrentOrder();
    void tilePixelSizeSelectsAThumbnailTier();
    void changingTheThumbnailSizeReRequestsTheVisibleWindow();
    void modelPassesGenericModelTest();
    void thumbnailsBeyondTheRetentionBoundAreDroppedAndRequestedAgain();
    void refreshThumbnailReRequestsARowTheProviderCannotServe();
    void appendingReloadKeepsLoadedThumbnailsAndInsertsRows();
    void insertingReloadKeepsLoadedThumbnailsAndInsertsRows();

private:
    // Builds an in-memory projection with \a count items and leaves it in m_db.
    void populate(int count);

    pimio::testing::FakeClock m_clock{QDateTime::fromMSecsSinceEpoch(0, Qt::UTC)};
    std::unique_ptr<pimio::projection::ProjectionDatabase> m_db;
};
