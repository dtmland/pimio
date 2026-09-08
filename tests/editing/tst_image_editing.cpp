#include "pimio/editing/image_export_service.h"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio;

class TestImageEditing : public QObject
{
    Q_OBJECT

private slots:
    void recipePreviewAndExportPreserveSource();
    void rejectedExportLeavesSourceUnchanged();
    void derivativeSerializesExplicitLinkage();
};

namespace {

QString fixture()
{
    return QDir(QStringLiteral(PIMIO_FIXTURES_DIR)).filePath(QStringLiteral("images/png-solid.png"));
}

core::MediaRecord sourceRecord()
{
    core::MediaRecord source;
    source.id = core::MediaId(QStringLiteral("source-id"));
    source.metadata.kind = core::MediaKind::Image;
    source.recipe.setRevision(2);
    source.recipe.append(core::EditOperation(core::EditOperationKind::Crop,
                                             {{"x", 0}, {"y", 0}, {"width", 1}, {"height", 1}}));
    source.recipe.append(core::EditOperation(core::EditOperationKind::Rotate, {{"degrees", 90}}));
    return source;
}

} // namespace

void TestImageEditing::recipePreviewAndExportPreserveSource()
{
    QFile sourceFile(fixture());
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray before = sourceFile.readAll();
    sourceFile.close();
    core::Error error;
    editing::ImageRecipeRenderer renderer;
    const core::EditRecipe recipe = sourceRecord().recipe;

    // Full-size recipe result stays 1x1 after crop+rotate.
    const QImage full = renderer.preview(fixture(), recipe, {}, &error);
    QVERIFY2(!full.isNull(), qPrintable(error.message()));
    QCOMPARE(full.size(), QSize(1, 1));

    // A larger targetSize is only a downscale bound; never upscale a tiny crop.
    const QImage preview = renderer.preview(fixture(), recipe, {100, 100}, &error);
    QVERIFY2(!preview.isNull(), qPrintable(error.message()));
    QCOMPARE(preview.size(), QSize(1, 1));

    // A smaller bound does downscale a larger recipe result.
    core::EditRecipe noCrop;
    noCrop.setRevision(1);
    noCrop.append(core::EditOperation(core::EditOperationKind::Rotate, {{"degrees", 90}}));
    const QImage downscaled = renderer.preview(fixture(), noCrop, {8, 8}, &error);
    QVERIFY2(!downscaled.isNull(), qPrintable(error.message()));
    QCOMPARE(downscaled.size(), QSize(8, 8));

    QTemporaryDir directory;
    QVERIFY(renderer.exportImage(fixture(), recipe,
                                 directory.filePath(QStringLiteral("export.png")), &error));
    QImageReader exported(directory.filePath(QStringLiteral("export.png")));
    QVERIFY(exported.canRead());
    QCOMPARE(exported.size(), QSize(1, 1));
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFile.readAll(), before);
}

void TestImageEditing::rejectedExportLeavesSourceUnchanged()
{
    QFile sourceFile(fixture());
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray before = sourceFile.readAll();
    sourceFile.close();

    core::Error error;
    editing::ImageRecipeRenderer renderer;
    QVERIFY(!renderer.exportImage(fixture(), sourceRecord().recipe, fixture(), &error));
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(core::ErrorCode::Conflict));
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFile.readAll(), before);
}

void TestImageEditing::derivativeSerializesExplicitLinkage()
{
    QTemporaryDir directory;
    core::Error error;
    editing::ImageExportService service;
    const auto derivative = service.exportImage(sourceRecord(), fixture(),
                                                directory.filePath(QStringLiteral("export.png")), &error);
    QVERIFY2(derivative.has_value(), qPrintable(error.message()));
    QVERIFY(derivative->derivative.has_value());
    QCOMPARE(derivative->derivative->sourceMediaId, core::MediaId(QStringLiteral("source-id")));
    QCOMPARE(derivative->derivative->recipeRevision, 2);
    QCOMPARE(derivative->derivative->kind, QStringLiteral("export"));
    const core::MediaRecord restored = core::MediaRecord::fromJson(derivative->toJson());
    QVERIFY(restored.derivative == derivative->derivative);
}

QTEST_APPLESS_MAIN(TestImageEditing)

#include "tst_image_editing.moc"
