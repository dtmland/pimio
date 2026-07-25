#include "pimio/app/application.h"

#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTest>

class TestAppSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mainQmlLoadsRootWindow();
};

void TestAppSmoke::initTestCase()
{
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

QTEST_MAIN(TestAppSmoke)

#include "tst_app_smoke.moc"
