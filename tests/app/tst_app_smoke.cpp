#include "tst_app_smoke_fixture.h"

#include "app_test_support.h"

#include "pimio/app/application.h"
#include "pimio/app/library_activity.h"
#include "pimio/app/library_session.h"
#include "pimio/browser/thumbnail_image_provider.h"
#include "pimio/settings/settings.h"

#include <QDir>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#ifndef PIMIO_FIXTURES_DIR
#error "PIMIO_FIXTURES_DIR must be defined by the build system"
#endif

using pimio::tests::app_support::findVisualItem;
using pimio::tests::app_support::SyntheticMediaModel;

void TestAppSmoke::initTestCase()
{
    // Qt Quick Controls picks a native style on macOS and Windows. The macOS
    // one draws ComboBox and Slider through AppKit (QtQuick.NativeStyle),
    // which needs a real window from the Cocoa platform plugin; under the
    // offscreen plugin this test runs on there is none, and the first frame
    // that paints one of those controls crashes the process. The style a
    // headless test draws with is not what it is testing, so it asks for a
    // Qt-drawn one, which also makes the three platforms behave alike.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // Keeps the settings this test writes out of the developer's real
    // configuration directory.
    QStandardPaths::setTestModeEnabled(true);
    pimio::app::configureApplicationMetadata();
    QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("pimio"));
}

void TestAppSmoke::mainQmlLoadsRootWindow()
{
    QQmlApplicationEngine engine;
    QVERIFY(pimio::app::loadMainQml(engine));
    QCOMPARE(engine.rootObjects().size(), 1);

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    QCOMPARE(window->objectName(), QStringLiteral("pimioMainWindow"));
}

void TestAppSmoke::gridTracksVisibleRangeAndOpensDetail()
{
    SyntheticMediaModel model(100);
    QSignalSpy rangeSpy(&model, &SyntheticMediaModel::visibleRangeChanged);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    QVERIFY(grid != nullptr);

    QTRY_VERIFY(grid->width() > 0 && grid->height() > 0);
    QVERIFY(QMetaObject::invokeMethod(grid, "updateVisibleRange"));
    QTRY_VERIFY(!rangeSpy.isEmpty() && rangeSpy.constLast().at(1).toInt() > 0);
    const QList<QVariant> initialRange = rangeSpy.constLast();
    QCOMPARE(initialRange.at(0).toInt(), 0);
    QVERIFY(initialRange.at(1).toInt() > 0);
    QVERIFY(initialRange.at(1).toInt() < model.rowCount() - 1);

    const int callsBeforeScroll = rangeSpy.size();
    grid->setProperty("contentY", 1800);
    QTRY_VERIFY(rangeSpy.size() > callsBeforeScroll);
    QVERIFY(rangeSpy.constLast().at(0).toInt() > 0);

    grid->setProperty("contentY", 0);
    QTRY_COMPARE(grid->property("contentY").toInt(), 0);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(88, 136));
    auto *detail = window->findChild<QQuickItem *>(QStringLiteral("detailView"));
    QVERIFY(detail != nullptr);
    QTRY_VERIFY(detail->property("visible").toBool());
    QCOMPARE(detail->property("mediaId").toString(), QStringLiteral("item-0"));
    QCOMPARE(detail->property("absolutePath").toString(),
             QStringLiteral("/library/item-0.jpg"));
}

void TestAppSmoke::readyThumbnailUsesImageProvider()
{
    SyntheticMediaModel model(1, 2);
    QQmlApplicationEngine engine;
    auto *provider = new pimio::browser::ThumbnailImageProvider;
    QImage thumbnail(16, 16, QImage::Format_RGB32);
    thumbnail.fill(Qt::green);
    provider->setImage(QStringLiteral("item-0"), thumbnail);
    engine.addImageProvider(QStringLiteral("thumbnail"), provider);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    QTRY_VERIFY(findVisualItem(window->contentItem(), QStringLiteral("gridThumbnail")) != nullptr);
    auto *image = findVisualItem(window->contentItem(), QStringLiteral("gridThumbnail"));
    QVERIFY(image != nullptr);
    QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
    QCOMPARE(image->property("source").toUrl(),
             QUrl(QStringLiteral("image://thumbnail/item-0")));
}

void TestAppSmoke::detailLoadsModernImage_data()
{
    QTest::addColumn<QString>("relativePath");

    QTest::newRow("WebP") << QStringLiteral("images/webp-solid.webp");
    QTest::newRow("AVIF") << QStringLiteral("images/avif-solid.avif");
    QTest::newRow("HEIC grid") << QStringLiteral("images/heic-grid.heic");
}

void TestAppSmoke::detailLoadsModernImage()
{
    QFETCH(QString, relativePath);

    const QString absolutePath =
            QDir(QStringLiteral(PIMIO_FIXTURES_DIR)).absoluteFilePath(relativePath);
    SyntheticMediaModel model(1, 0, absolutePath);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    QVERIFY(QMetaObject::invokeMethod(window, "showDetail", Q_ARG(QVariant, 0)));

    auto *preview = window->findChild<QQuickItem *>(QStringLiteral("detailPreview"));
    QVERIFY(preview != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(preview->property("status").toInt(), 1, 10000); // Image.Ready
    QCOMPARE(preview->property("source").toUrl(), QUrl::fromLocalFile(absolutePath));
}

void TestAppSmoke::wheelScrollingFollowsTheConfiguredSpeed()
{
    pimio::settings::Settings settings;
    settings.resetToDefaults();
    settings.setScrollAcceleration(false);
    SyntheticMediaModel model(400);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    QVERIFY(grid != nullptr);
    QTRY_VERIFY(grid->width() > 0 && grid->height() > 0);

    // One notch of a wheel, from rest, with a timestamp far enough apart that
    // no gesture is in progress.
    const auto oneNotch = [grid](qreal atMs) {
        grid->setProperty("contentY", 0);
        QMetaObject::invokeMethod(grid, "scrollByWheel", Q_ARG(QVariant, -120),
                                  Q_ARG(QVariant, 0), Q_ARG(QVariant, atMs));
        return grid->property("contentY").toReal();
    };

    const qreal slow = oneNotch(0);
    QVERIFY(slow > 0);

    settings.setScrollSpeed(settings.scrollSpeed() * 2.0);
    const qreal fast = oneNotch(10000);
    QCOMPARE(fast, slow * 2.0);

    // With acceleration on, a continued gesture covers more ground per notch
    // than the first notch did.
    settings.setScrollAcceleration(true);
    grid->setProperty("contentY", 0);
    qreal previous = 0;
    for (int notch = 0; notch < 6; ++notch) {
        QMetaObject::invokeMethod(grid, "scrollByWheel", Q_ARG(QVariant, -120),
                                  Q_ARG(QVariant, 0), Q_ARG(QVariant, 20000 + notch * 50));
        previous = grid->property("contentY").toReal();
    }
    QVERIFY2(previous > fast * 6, "a continued scroll gesture must speed up");

    // Scrolling never runs past the end of the content.
    QMetaObject::invokeMethod(grid, "scrollByWheel", Q_ARG(QVariant, -120000),
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, 40000));
    const qreal origin = grid->property("originY").toReal();
    const qreal maximum = qMax(origin, origin + grid->property("contentHeight").toReal()
                                      - grid->property("height").toReal());
    QCOMPARE(grid->property("contentY").toReal(), maximum);
    QMetaObject::invokeMethod(grid, "scrollByWheel", Q_ARG(QVariant, 120000),
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, 60000));
    QCOMPARE(grid->property("contentY").toReal(), origin);
}

void TestAppSmoke::scrollControllerJumpsAndUsesHandleDisplacement()
{
    pimio::settings::Settings settings;
    settings.resetToDefaults();
    SyntheticMediaModel model(400);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    auto *controller = window->findChild<QQuickItem *>(QStringLiteral("scrollController"));
    auto *handle = window->findChild<QQuickItem *>(QStringLiteral("scrollControllerHandle"));
    QVERIFY(grid != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(handle != nullptr);
    QTRY_VERIFY(grid->property("contentHeight").toReal() > grid->height());

    QVERIFY(QMetaObject::invokeMethod(controller, "jumpToEnd"));
    const qreal origin = grid->property("originY").toReal();
    const qreal maximum = qMax(origin, origin + grid->property("contentHeight").toReal()
                                      - grid->property("height").toReal());
    QCOMPARE(grid->property("contentY").toReal(), maximum);
    QVERIFY(QMetaObject::invokeMethod(controller, "jumpToStart"));
    QCOMPARE(grid->property("contentY").toReal(), origin);

    QVERIFY(QMetaObject::invokeMethod(controller, "scrollFromDisplacement",
                                      Q_ARG(QVariant, 0.25)));
    const qreal smallDisplacementDistance = grid->property("contentY").toReal() - origin;
    QVERIFY(smallDisplacementDistance > 0);
    grid->setProperty("contentY", origin);
    QVERIFY(QMetaObject::invokeMethod(controller, "scrollFromDisplacement",
                                      Q_ARG(QVariant, 0.75)));
    const qreal largeDisplacementDistance = grid->property("contentY").toReal() - origin;
    QVERIFY(largeDisplacementDistance > smallDisplacementDistance);

    controller->setProperty("handleOffset", -100);
    QVERIFY(QMetaObject::invokeMethod(controller, "returnHandleToCenter"));
    QTRY_COMPARE(handle->y(), handle->property("restingY").toReal());
}

void TestAppSmoke::tileSizeSettingResizesTheGridCells()
{
    pimio::settings::Settings settings;
    settings.resetToDefaults();
    SyntheticMediaModel model(50);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->property("cellWidth").toInt(), settings.tileSize());

    // The slider writes the setting; the grid follows it live.
    settings.setTileSize(settings.maximumTileSize());
    QCOMPARE(grid->property("cellWidth").toInt(), settings.maximumTileSize());
    QCOMPARE(grid->property("cellHeight").toInt(), settings.maximumTileSize());

    auto *slider = window->findChild<QQuickItem *>(QStringLiteral("tileSizeSlider"));
    QVERIFY(slider != nullptr);
    QCOMPARE(slider->property("from").toInt(), settings.minimumTileSize());
    QCOMPARE(slider->property("to").toInt(), settings.maximumTileSize());
}

void TestAppSmoke::gridScrollBoundsFollowLayoutOriginChanges()
{
    pimio::settings::Settings settings;
    settings.resetToDefaults();
    SyntheticMediaModel model(400);
    QSignalSpy rangeSpy(&model, &SyntheticMediaModel::visibleRangeChanged);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    QVERIFY(grid != nullptr);
    QTRY_VERIFY(grid->property("contentHeight").toReal() > grid->height());

    const int columns = grid->property("columns").toInt();
    const qreal cellHeight = grid->property("cellHeight").toReal();
    grid->setProperty("contentY", cellHeight * 12);
    QVERIFY(QMetaObject::invokeMethod(grid, "moveSelection",
                                      Q_ARG(QVariant, columns * 12)));
    model.prependRows(columns * 2);
    model.removeLeadingRows(columns);
    settings.setTileSize(settings.maximumTileSize());
    QTRY_COMPARE(grid->property("cellHeight").toInt(), settings.maximumTileSize());
    QVERIFY(QMetaObject::invokeMethod(grid, "refreshGeometry"));
    QTRY_VERIFY(!rangeSpy.isEmpty());
    const int lastBeforeWidthChange = rangeSpy.constLast().at(1).toInt();
    rangeSpy.clear();
    window->setWidth(window->width() + settings.maximumTileSize());
    QTRY_VERIFY(!rangeSpy.isEmpty()
                && rangeSpy.constLast().at(1).toInt() > lastBeforeWidthChange);

    QMetaObject::invokeMethod(grid, "scrollByWheel", Q_ARG(QVariant, 120000),
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, 10000));
    const qreal origin = grid->property("originY").toReal();
    QVERIFY2(origin != 0.0, "the regression setup must exercise a shifted GridView origin");
    QCOMPARE(grid->property("contentY").toReal(), origin);
    QTRY_VERIFY(!rangeSpy.isEmpty());
    QCOMPARE(rangeSpy.constLast().at(0).toInt(), 0);

    QMetaObject::invokeMethod(grid, "scrollByWheel", Q_ARG(QVariant, -120000),
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, 20000));
    const qreal maximum = qMax(origin, origin + grid->property("contentHeight").toReal()
                                      - grid->property("height").toReal());
    QCOMPARE(grid->property("contentY").toReal(), maximum);
    QTRY_COMPARE(rangeSpy.constLast().at(1).toInt(), model.rowCount() - 1);
}

void TestAppSmoke::settingsDialogExposesStoredAndSessionSettings()
{
    pimio::settings::Settings settings;
    settings.resetToDefaults();
    SyntheticMediaModel model(10);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *dialog = window->findChild<QObject *>(QStringLiteral("settingsDialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(!dialog->property("visible").toBool());

    auto *button = window->findChild<QQuickItem *>(QStringLiteral("settingsButton"));
    QVERIFY(button != nullptr);
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QTRY_VERIFY(dialog->property("visible").toBool());

    // A stored setting and the session-only one are both reachable from the
    // dialog.
    auto *scrollSlider = window->findChild<QQuickItem *>(QStringLiteral("settingsScrollSpeedSlider"));
    QVERIFY(scrollSlider != nullptr);
    QCOMPARE(scrollSlider->property("value").toReal(), settings.scrollSpeed());

    auto *diagnostics = window->findChild<QQuickItem *>(QStringLiteral("settingsTileDiagnosticsCheck"));
    QVERIFY(diagnostics != nullptr);
    QVERIFY(!settings.showTileDiagnostics());
    settings.setShowTileDiagnostics(true);
    QTRY_VERIFY(diagnostics->property("checked").toBool());
}

void TestAppSmoke::aThumbnailTheProviderCannotServeIsAskedForAgain()
{
    // The row claims a thumbnail but the provider has nothing for it, which is
    // exactly the grey-tile case. The view must report it instead of leaving
    // the tile blank forever.
    SyntheticMediaModel model(1, 2);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"),
                            new pimio::browser::ThumbnailImageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    QTRY_COMPARE(model.refreshedRows(), QList<int>({0}));
}

void TestAppSmoke::aScanInProgressShowsActivity()
{
    SyntheticMediaModel model(0);
    pimio::app::LibraryActivity activity;
    activity.setScanning(true);
    activity.setIndexedCount(7);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("libraryActivity"), &activity);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);

    auto *busy = window->findChild<QQuickItem *>(QStringLiteral("scanBusyIndicator"));
    QVERIFY(busy != nullptr);
    QTRY_VERIFY(busy->property("visible").toBool());

    auto *placeholder = window->findChild<QQuickItem *>(QStringLiteral("scanningPlaceholder"));
    QVERIFY(placeholder != nullptr);
    QTRY_VERIFY(placeholder->property("visible").toBool());

    auto *empty = window->findChild<QQuickItem *>(QStringLiteral("emptyLibraryPlaceholder"));
    if (empty != nullptr) {
        QVERIFY(!empty->property("visible").toBool());
    }

    // Finishing the scan puts the window back to its resting state.
    activity.setScanning(false);
    QTRY_VERIFY(!busy->property("visible").toBool());
    QTRY_VERIFY(!placeholder->property("visible").toBool());
}

void TestAppSmoke::preparedLibraryShowsStartupFeedbackBeforeStorageOpens()
{
    pimio::app::LibrarySession session;
    QQmlApplicationEngine engine;
    session.prepare({QStringLiteral("/library/not-opened-yet")}, engine);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *busy = window->findChild<QQuickItem *>(QStringLiteral("scanBusyIndicator"));
    auto *placeholder = window->findChild<QQuickItem *>(QStringLiteral("scanningPlaceholder"));
    QVERIFY(busy != nullptr);
    QVERIFY(placeholder != nullptr);
    QTRY_VERIFY(busy->property("visible").toBool());
    QTRY_VERIFY(placeholder->property("visible").toBool());
}

QTEST_MAIN(TestAppSmoke)

#include "tst_app_smoke.moc"
