#include "pimio/app/application.h"
#include "pimio/app/library_activity.h"
#include "pimio/app/library_session.h"
#include "pimio/browser/thumbnail_image_provider.h"
#include "pimio/settings/settings.h"

#include <QAbstractListModel>
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

#include <utility>

#ifndef PIMIO_FIXTURES_DIR
#error "PIMIO_FIXTURES_DIR must be defined by the build system"
#endif

namespace {

QQuickItem *findVisualItem(QQuickItem *root, const QString &objectName)
{
    if (root->objectName() == objectName) {
        return root;
    }
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *match = findVisualItem(child, objectName)) {
            return match;
        }
    }
    return nullptr;
}

class SyntheticMediaModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        MediaIdRole = Qt::UserRole + 1,
        AbsolutePathRole,
        CaptureTimeStringRole,
        MediaKindRole,
        ThumbnailStatusRole,
        ThumbnailImageRole,
    };

    explicit SyntheticMediaModel(int count, int thumbnailStatus = 0, QString absolutePath = {},
                                 QObject *parent = nullptr)
        : QAbstractListModel(parent)
        , m_thumbnailStatus(thumbnailStatus)
        , m_absolutePath(std::move(absolutePath))
    {
        m_ids.reserve(count);
        for (int row = 0; row < count; ++row) {
            m_ids.append(QStringLiteral("item-%1").arg(row));
        }
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_ids.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_ids.size()) {
            return {};
        }
        switch (role) {
        case MediaIdRole:
            return m_ids.at(index.row());
        case AbsolutePathRole:
            return m_absolutePath.isEmpty()
                    ? QStringLiteral("/library/%1.jpg").arg(m_ids.at(index.row()))
                    : m_absolutePath;
        case CaptureTimeStringRole:
            return QStringLiteral("2026-01-01T00:00:00");
        case MediaKindRole:
            return 1;
        case ThumbnailStatusRole:
            return m_thumbnailStatus;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {MediaIdRole, "mediaId"},
            {AbsolutePathRole, "absolutePath"},
            {CaptureTimeStringRole, "captureTimeString"},
            {MediaKindRole, "mediaKind"},
            {ThumbnailStatusRole, "thumbnailStatus"},
            {ThumbnailImageRole, "thumbnailImage"},
        };
    }

    Q_INVOKABLE void setVisibleRange(int first, int last)
    {
        emit visibleRangeChanged(first, last);
    }

    Q_INVOKABLE void requestThumbnail(int)
    {
    }

    Q_INVOKABLE void refreshThumbnail(int row)
    {
        m_refreshedRows.append(row);
    }

    QList<int> refreshedRows() const
    {
        return m_refreshedRows;
    }

    void prependRows(int count)
    {
        if (count <= 0) {
            return;
        }
        beginInsertRows({}, 0, count - 1);
        for (int row = 0; row < count; ++row) {
            m_ids.insert(row, QStringLiteral("prepended-%1").arg(m_nextPrependedId++));
        }
        endInsertRows();
    }

    void removeLeadingRows(int count)
    {
        const int removed = qBound(0, count, static_cast<int>(m_ids.size()));
        if (removed == 0) {
            return;
        }
        beginRemoveRows({}, 0, removed - 1);
        m_ids.remove(0, removed);
        endRemoveRows();
    }

    Q_INVOKABLE QVariantMap itemAt(int row) const
    {
        const QModelIndex itemIndex = index(row);
        return {
            {QStringLiteral("mediaId"), data(itemIndex, MediaIdRole)},
            {QStringLiteral("absolutePath"), data(itemIndex, AbsolutePathRole)},
            {QStringLiteral("captureTimeString"), data(itemIndex, CaptureTimeStringRole)},
            {QStringLiteral("mediaKind"), data(itemIndex, MediaKindRole)},
            {QStringLiteral("thumbnailStatus"), data(itemIndex, ThumbnailStatusRole)},
        };
    }

signals:
    void visibleRangeChanged(int first, int last);

private:
    QStringList m_ids;
    int m_thumbnailStatus;
    QString m_absolutePath;
    QList<int> m_refreshedRows;
    int m_nextPrependedId = 0;
};

} // namespace

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

void TestAppSmoke::arrowKeysMoveTheSelectionByRowsAndColumns()
{
    SyntheticMediaModel model(100);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    QVERIFY(grid != nullptr);
    QTRY_VERIFY(grid->width() > 0 && grid->height() > 0);

    // A running application is handed keyboard focus by the window manager;
    // a headless test window (Xvfb has no window manager, and the offscreen
    // platform has no windows at all) is never activated, so nothing holds
    // active focus and key events are dropped. Asking for it explicitly is
    // what the window manager would have done.
    grid->forceActiveFocus();
    QTRY_VERIFY(grid->hasActiveFocus());

    const int columns = grid->property("columns").toInt();
    QVERIFY(columns > 0);

    QVERIFY(QMetaObject::invokeMethod(grid, "moveSelection", Q_ARG(QVariant, 0)));
    QCOMPARE(grid->property("currentIndex").toInt(), 0);
    const qreal initialContentY = grid->property("contentY").toReal();

    QTest::keyClick(window, Qt::Key_Right);
    QCOMPARE(grid->property("currentIndex").toInt(), 1);
    QCOMPARE(grid->property("contentY").toReal(), initialContentY);

    QTest::keyClick(window, Qt::Key_Down);
    QCOMPARE(grid->property("currentIndex").toInt(), 1 + columns);
    QCOMPARE(grid->property("contentY").toReal(),
             initialContentY + grid->property("cellHeight").toReal());

    QTest::keyClick(window, Qt::Key_Up);
    QCOMPARE(grid->property("currentIndex").toInt(), 1);
    QCOMPARE(grid->property("contentY").toReal(), initialContentY);

    // A page is one screenful of rows, and the selection never leaves the
    // model.
    const int rowsPerPage = grid->property("height").toReal() / grid->property("cellHeight").toReal();
    QTest::keyClick(window, Qt::Key_PageDown);
    QCOMPARE(grid->property("currentIndex").toInt(), 1 + columns * rowsPerPage);

    QTest::keyClick(window, Qt::Key_End);
    QCOMPARE(grid->property("currentIndex").toInt(), model.rowCount() - 1);
    QTest::keyClick(window, Qt::Key_Home);
    QCOMPARE(grid->property("currentIndex").toInt(), 0);
}

void TestAppSmoke::gridFocusFollowsTheBrowsingContext()
{
    SyntheticMediaModel model(100);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    auto *detail = window->findChild<QQuickItem *>(QStringLiteral("detailView"));
    auto *dialog = window->findChild<QObject *>(QStringLiteral("settingsDialog"));
    auto *settingsButton = window->findChild<QQuickItem *>(QStringLiteral("settingsButton"));
    QVERIFY(grid != nullptr);
    QVERIFY(detail != nullptr);
    QVERIFY(dialog != nullptr);
    QVERIFY(settingsButton != nullptr);

    // The normal browsing view owns keyboard focus without requiring a tile
    // click first.
    QTRY_VERIFY(grid->hasActiveFocus());
    QVERIFY(QMetaObject::invokeMethod(grid, "moveSelection", Q_ARG(QVariant, 0)));
    QTest::keyClick(window, Qt::Key_Right);
    QCOMPARE(grid->property("currentIndex").toInt(), 1);

    // Preview navigation is a separate context, so grid-only keys do nothing.
    QVERIFY(QMetaObject::invokeMethod(window, "showDetail", Q_ARG(QVariant, 5)));
    QTRY_VERIFY(detail->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_PageDown);
    QCOMPARE(grid->property("currentIndex").toInt(), 5);
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!detail->property("visible").toBool());
    QTRY_VERIFY(grid->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Right);
    QCOMPARE(grid->property("currentIndex").toInt(), 6);

    // A modal settings dialog likewise suspends grid navigation and returns
    // focus when it closes.
    QVERIFY(QMetaObject::invokeMethod(settingsButton, "clicked"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(!grid->hasActiveFocus());
    const int indexBeforeSettingsKey = grid->property("currentIndex").toInt();
    QTest::keyClick(window, Qt::Key_PageDown);
    QCOMPARE(grid->property("currentIndex").toInt(), indexBeforeSettingsKey);
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(grid->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_Right);
    QCOMPARE(grid->property("currentIndex").toInt(), indexBeforeSettingsKey + 1);
}

void TestAppSmoke::holdingANavigationKeyAcceleratesUnlessDisabled()
{
    pimio::settings::Settings settings;
    settings.resetToDefaults();
    SyntheticMediaModel model(100);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);

    // The step a key press produces, given the synthetic event the QML
    // handler sees. A first press always moves exactly one item.
    const auto step = [window](bool autoRepeat, int key = Qt::Key_Down) {
        QVariantMap event{{QStringLiteral("key"), key},
                          {QStringLiteral("isAutoRepeat"), autoRepeat}};
        QVariant result;
        const bool ok = QMetaObject::invokeMethod(window, "navigationStep",
                                                  Q_RETURN_ARG(QVariant, result),
                                                  Q_ARG(QVariant, event));
        return ok ? result.toInt() : -1;
    };

    QCOMPARE(step(false), 1);
    int last = 1;
    for (int repeat = 0; repeat < 60; ++repeat) {
        const int current = step(true);
        QVERIFY(current >= last); // never slows down mid-hold
        last = current;
    }
    QVERIFY2(last > 1, "a held key must move further than a single press");
    const int maximum = window->property("maximumKeyStep").toInt();
    QCOMPARE(last, maximum);

    // Switching keys starts the ramp over, so releasing Down and holding Up
    // does not inherit the speed.
    QCOMPARE(step(false, Qt::Key_Up), 1);

    // With acceleration off, a held key keeps the single-press step.
    settings.setKeyRepeatAcceleration(false);
    QVERIFY(QMetaObject::invokeMethod(window, "endKeyRepeat"));
    QCOMPARE(step(false), 1);
    for (int repeat = 0; repeat < 20; ++repeat) {
        QCOMPARE(step(true), 1);
    }
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

void TestAppSmoke::previewArrowKeysFollowTheGridOrder()
{
    SyntheticMediaModel model(20);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), &model);
    QVERIFY(pimio::app::loadMainQml(engine));

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    QVERIFY(QMetaObject::invokeMethod(window, "showDetail", Q_ARG(QVariant, 5)));

    auto *detail = window->findChild<QQuickItem *>(QStringLiteral("detailView"));
    QVERIFY(detail != nullptr);
    QTRY_VERIFY(detail->property("visible").toBool());
    QCOMPARE(detail->property("mediaId").toString(), QStringLiteral("item-5"));

    // showDetail() moves keyboard focus to the preview; the keys below only
    // arrive once it holds it.
    QTRY_VERIFY(detail->hasActiveFocus());

    // Right and left step one item along the order the grid is showing.
    QTest::keyClick(window, Qt::Key_Right);
    QCOMPARE(detail->property("mediaId").toString(), QStringLiteral("item-6"));
    QTest::keyClick(window, Qt::Key_Left);
    QCOMPARE(detail->property("mediaId").toString(), QStringLiteral("item-5"));

    // The grid selection follows the preview, so closing it leaves the user
    // where they were looking.
    auto *grid = window->findChild<QQuickItem *>(QStringLiteral("mediaGrid"));
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->property("currentIndex").toInt(), 5);

    // Stepping past the ends clamps rather than wrapping or emptying.
    QVERIFY(QMetaObject::invokeMethod(window, "stepDetail", Q_ARG(QVariant, -100)));
    QCOMPARE(detail->property("mediaId").toString(), QStringLiteral("item-0"));
    QVERIFY(QMetaObject::invokeMethod(window, "stepDetail", Q_ARG(QVariant, 100)));
    QCOMPARE(detail->property("mediaId").toString(), QStringLiteral("item-19"));

    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!detail->property("visible").toBool());
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
