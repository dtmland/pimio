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
