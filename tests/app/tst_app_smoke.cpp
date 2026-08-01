#include "pimio/app/application.h"

#include <QAbstractListModel>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

namespace {

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

    explicit SyntheticMediaModel(int count, QObject *parent = nullptr)
        : QAbstractListModel(parent)
        , m_count(count)
    {
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_count;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_count) {
            return {};
        }
        switch (role) {
        case MediaIdRole:
            return QStringLiteral("item-%1").arg(index.row());
        case AbsolutePathRole:
            return QStringLiteral("/library/item-%1.jpg").arg(index.row());
        case CaptureTimeStringRole:
            return QStringLiteral("2026-01-01T00:00:00");
        case MediaKindRole:
            return 1;
        case ThumbnailStatusRole:
            return 0;
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

    Q_INVOKABLE QVariantMap itemAt(int row) const
    {
        const QModelIndex itemIndex = index(row);
        return {
            {QStringLiteral("mediaId"), data(itemIndex, MediaIdRole)},
            {QStringLiteral("absolutePath"), data(itemIndex, AbsolutePathRole)},
            {QStringLiteral("captureTimeString"), data(itemIndex, CaptureTimeStringRole)},
            {QStringLiteral("mediaKind"), data(itemIndex, MediaKindRole)},
        };
    }

signals:
    void visibleRangeChanged(int first, int last);

private:
    int m_count;
};

} // namespace

class TestAppSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mainQmlLoadsRootWindow();
    void gridTracksVisibleRangeAndOpensDetail();
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

    QTRY_VERIFY(!rangeSpy.isEmpty());
    const QList<QVariant> initialRange = rangeSpy.constLast();
    QCOMPARE(initialRange.at(0).toInt(), 0);
    QVERIFY(initialRange.at(1).toInt() > 0);
    QVERIFY(initialRange.at(1).toInt() < model.rowCount() - 1);

    const int callsBeforeScroll = rangeSpy.size();
    grid->setProperty("contentY", 1800);
    QTRY_VERIFY(rangeSpy.size() > callsBeforeScroll);
    QVERIFY(rangeSpy.constLast().at(0).toInt() > 0);

    QVERIFY(QMetaObject::invokeMethod(window, "showDetail", Q_ARG(QVariant, 5)));
    auto *detail = window->findChild<QQuickItem *>(QStringLiteral("detailView"));
    QVERIFY(detail != nullptr);
    QTRY_VERIFY(detail->isVisible());
    QCOMPARE(detail->property("mediaId").toString(), QStringLiteral("item-5"));
    QCOMPARE(detail->property("absolutePath").toString(),
             QStringLiteral("/library/item-5.jpg"));
}

QTEST_MAIN(TestAppSmoke)

#include "tst_app_smoke.moc"
