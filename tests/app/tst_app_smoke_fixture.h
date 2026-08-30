#pragma once

#include <QObject>
#include <QTest>

class TestAppSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mainQmlLoadsRootWindow();
    void gridTracksVisibleRangeAndOpensDetail();
    void readyThumbnailUsesImageProvider();
    void detailLoadsModernImage_data();
    void detailLoadsModernImage();
    void arrowKeysMoveTheSelectionByRowsAndColumns();
    void gridFocusFollowsTheBrowsingContext();
    void holdingANavigationKeyAcceleratesUnlessDisabled();
    void wheelScrollingFollowsTheConfiguredSpeed();
    void scrollControllerJumpsAndUsesHandleDisplacement();
    void tileSizeSettingResizesTheGridCells();
    void gridScrollBoundsFollowLayoutOriginChanges();
    void settingsDialogExposesStoredAndSessionSettings();
    void previewArrowKeysFollowTheGridOrder();
    void aThumbnailTheProviderCannotServeIsAskedForAgain();
    void aScanInProgressShowsActivity();
    void preparedLibraryShowsStartupFeedbackBeforeStorageOpens();
};

