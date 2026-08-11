#include "pimio/browser/thumbnail_image_provider.h"

#include <QTest>

using namespace pimio::browser;

/// Covers the image provider that serves QML's `image://thumbnail/<mediaId>`
/// URLs, added so the grid and detail view actually display a decoded
/// thumbnail instead of the previously unregistered "image://thumbnail/"
/// scheme resolving to nothing.
class TestBrowserThumbnailImageProvider : public QObject
{
    Q_OBJECT

private slots:
    void unknownIdReturnsANullImage();
    void setImageMakesItRetrievableById();
    void requestedSizeScalesTheResult();
    void clearForgetsEveryRecordedImage();
    void aLeadingSlashInTheIdIsTolerated();
    void removeImageForgetsOneId();
    void capacityBoundsHowManyImagesAreKept();
};

void TestBrowserThumbnailImageProvider::unknownIdReturnsANullImage()
{
    ThumbnailImageProvider provider;
    QSize size;
    const QImage image = provider.requestImage(QStringLiteral("unknown-id"), &size, QSize());
    QVERIFY(image.isNull());
}

void TestBrowserThumbnailImageProvider::setImageMakesItRetrievableById()
{
    ThumbnailImageProvider provider;
    QImage source(4, 4, QImage::Format_RGB32);
    source.fill(Qt::red);
    provider.setImage(QStringLiteral("media-1"), source);

    QSize size;
    const QImage result = provider.requestImage(QStringLiteral("media-1"), &size, QSize());
    QVERIFY(!result.isNull());
    QCOMPARE(size, QSize(4, 4));
    QCOMPARE(result.pixelColor(0, 0), QColor(Qt::red));
}

void TestBrowserThumbnailImageProvider::requestedSizeScalesTheResult()
{
    ThumbnailImageProvider provider;
    QImage source(100, 50, QImage::Format_RGB32);
    source.fill(Qt::blue);
    provider.setImage(QStringLiteral("media-2"), source);

    QSize size;
    const QImage result = provider.requestImage(QStringLiteral("media-2"), &size, QSize(20, 20));
    QVERIFY(!result.isNull());
    // KeepAspectRatio: width-constrained since the source is wider than tall.
    QCOMPARE(result.width(), 20);
    QVERIFY(result.height() <= 20);
}

void TestBrowserThumbnailImageProvider::clearForgetsEveryRecordedImage()
{
    ThumbnailImageProvider provider;
    QImage source(2, 2, QImage::Format_RGB32);
    provider.setImage(QStringLiteral("media-3"), source);
    provider.clear();

    const QImage result = provider.requestImage(QStringLiteral("media-3"), nullptr, QSize());
    QVERIFY(result.isNull());
}

void TestBrowserThumbnailImageProvider::aLeadingSlashInTheIdIsTolerated()
{
    ThumbnailImageProvider provider;
    QImage source(2, 2, QImage::Format_RGB32);
    provider.setImage(QStringLiteral("media-4"), source);

    const QImage result = provider.requestImage(QStringLiteral("/media-4"), nullptr, QSize());
    QVERIFY(!result.isNull());
}

void TestBrowserThumbnailImageProvider::removeImageForgetsOneId()
{
    ThumbnailImageProvider provider;
    QImage source(2, 2, QImage::Format_RGB32);
    provider.setImage(QStringLiteral("media-5"), source);
    provider.setImage(QStringLiteral("media-6"), source);

    provider.removeImage(QStringLiteral("media-5"));

    QVERIFY(!provider.contains(QStringLiteral("media-5")));
    QVERIFY(provider.contains(QStringLiteral("media-6")));
    QVERIFY(provider.requestImage(QStringLiteral("media-5"), nullptr, QSize()).isNull());
    QVERIFY(!provider.requestImage(QStringLiteral("media-6"), nullptr, QSize()).isNull());
}

void TestBrowserThumbnailImageProvider::capacityBoundsHowManyImagesAreKept()
{
    ThumbnailImageProvider provider;
    provider.setCapacity(2);
    QCOMPARE(provider.capacity(), 2);

    QImage source(2, 2, QImage::Format_RGB32);
    provider.setImage(QStringLiteral("a"), source);
    provider.setImage(QStringLiteral("b"), source);
    provider.setImage(QStringLiteral("c"), source);

    QCOMPARE(provider.count(), 2);
    QVERIFY(provider.contains(QStringLiteral("c")));
}

QTEST_MAIN(TestBrowserThumbnailImageProvider)

#include "tst_browser_thumbnail_image_provider.moc"
