#include "pimio/app/application.h"
#include "pimio/browser/thumbnail_image_provider.h"

#include <QAbstractListModel>
#include <QDir>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
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
        , m_count(count)
        , m_thumbnailStatus(thumbnailStatus)
        , m_absolutePath(std::move(absolutePath))
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
            return m_absolutePath.isEmpty()
                    ? QStringLiteral("/library/item-%1.jpg").arg(index.row())
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
    int m_count;
    int m_thumbnailStatus;
    QString m_absolutePath;
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

QTEST_MAIN(TestAppSmoke)

#include "tst_app_smoke.moc"
