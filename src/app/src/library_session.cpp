#include "pimio/app/library_session.h"

#include "pimio/browser/media_library_model.h"
#include "pimio/browser/thumbnail_image_provider.h"
#include "pimio/core/error.h"
#include "pimio/core/job.h"
#include "pimio/metadata/builtin_metadata_reader.h"
#include "pimio/projection/job_dispatcher.h"
#include "pimio/projection/job_queue.h"
#include "pimio/projection/projection_database.h"
#include "pimio/scan/library_root.h"
#include "pimio/scan/qt_file_system.h"
#include "pimio/scan/scanner.h"
#include "pimio/thumbnail/composite_renderer.h"
#include "pimio/thumbnail/image_renderer.h"
#include "pimio/thumbnail/thumbnail_disk_cache.h"
#include "pimio/thumbnail/thumbnail_service.h"
#include "pimio/thumbnail/video_frame_renderer.h"
#include "pimio/watch/qt_directory_watch_adapter.h"
#include "pimio/watch/reconcile_worker.h"
#include "pimio/watch/watch_service.h"

#ifdef PIMIO_HAVE_LORE
#include "pimio/lore/lore_durable_store.h"
#endif

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include <algorithm>
#include <vector>

namespace pimio::app {

namespace {
Q_LOGGING_CATEGORY(lcLibrary, "pimio.app.library")

/// Directory name derived from the set of library paths, so different sets
/// of --library arguments get independent indexes rather than silently
/// sharing or clobbering one another's projection and job queue.
QString indexDirectoryFor(const QStringList &libraryPaths)
{
    QStringList sorted = libraryPaths;
    std::sort(sorted.begin(), sorted.end());
    const QString joined = sorted.join(QLatin1Char('\n'));
    const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(joined.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));

    const QString base =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/libraries/") + digest;
}

QStringList normalizedLibraryPaths(const QStringList &libraryPaths)
{
    QStringList normalized;
    for (const QString &path : libraryPaths) {
        QFileInfo info(path);
        QString absolutePath = info.canonicalFilePath();
        if (absolutePath.isEmpty()) {
            absolutePath = info.absoluteFilePath();
        }
        absolutePath = QDir::cleanPath(absolutePath);
        if (!normalized.contains(absolutePath,
#ifdef Q_OS_WIN
                                 Qt::CaseInsensitive
#else
                                 Qt::CaseSensitive
#endif
                                 )) {
            normalized.append(absolutePath);
        }
    }
    return normalized;
}

} // namespace

class LibrarySession::Private
{
public:
    ~Private()
    {
        if (dispatcher) {
            dispatcher->stop();
        }
    }

    std::unique_ptr<scan::QtFileSystem> fileSystem;
    std::unique_ptr<metadata::BuiltinMetadataReader> metadataReader;

#ifdef PIMIO_HAVE_LORE
    std::unique_ptr<lore::LoreDurableStore> loreStore;
#endif
    core::DurableStore *store = nullptr;

    std::unique_ptr<projection::ProjectionDatabase> projectionDb;
    std::unique_ptr<projection::JobQueue> jobQueue;
    std::unique_ptr<projection::JobDispatcher> dispatcher;
    std::unique_ptr<scan::Scanner> scanner;

    std::unique_ptr<thumbnail::ThumbnailDiskCache> thumbnailCache;
    std::unique_ptr<thumbnail::ImageRenderer> imageRenderer;
    std::unique_ptr<thumbnail::VideoFrameRenderer> videoRenderer;
    std::unique_ptr<thumbnail::CompositeRenderer> compositeRenderer;
    std::unique_ptr<thumbnail::ThumbnailService> thumbnailService;

    browser::ThumbnailImageProvider *imageProvider = nullptr; // owned by QQmlEngine
    std::unique_ptr<browser::MediaLibraryModel> model;

    std::vector<std::unique_ptr<watch::QtDirectoryWatchAdapter>> watchAdapters;
    std::vector<std::unique_ptr<watch::WatchService>> watchServices;

    bool ready = false;
};

LibrarySession::LibrarySession(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

LibrarySession::~LibrarySession() = default;

void LibrarySession::start(const QStringList &libraryPaths, QQmlApplicationEngine &engine)
{
    // The image provider and (empty, for now) model are registered
    // unconditionally, even with no library path at all or if opening
    // storage fails below: Main.qml's "typeof mediaLibraryModel ===
    // undefined" guard means either state renders the same empty-library
    // placeholder, so there is no behavioural difference, but registering
    // them up front means a library added by a future session never has to
    // race a context property appearing after QML already bound to it.
    d->model = std::make_unique<browser::MediaLibraryModel>();
    auto *provider = new browser::ThumbnailImageProvider();
    d->imageProvider = provider;
    engine.addImageProvider(QStringLiteral("thumbnail"), provider);
    d->model->setImageProvider(d->imageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), d->model.get());

    const QStringList normalizedPaths = normalizedLibraryPaths(libraryPaths);
    if (normalizedPaths.isEmpty()) {
        return;
    }

#ifndef PIMIO_HAVE_LORE
    qCWarning(lcLibrary) << "Cannot open a library: this build has no durable store (LORE was "
                           "unavailable at compile time). The library stays empty; see "
                           "docs/decisions/0001-lore-durable-store.md.";
    return;
#else
    const QString indexDir = indexDirectoryFor(normalizedPaths);
    QDir().mkpath(indexDir);

    d->loreStore = std::make_unique<lore::LoreDurableStore>(indexDir + QStringLiteral("/store"));
    core::Error storeError;
    if (!d->loreStore->open(&storeError)) {
        qCWarning(lcLibrary) << "Cannot open durable storage for" << normalizedPaths << ":"
                              << storeError.message();
        d->loreStore.reset();
        return;
    }
    d->store = d->loreStore.get();

    d->projectionDb = std::make_unique<projection::ProjectionDatabase>();
    core::Error projectionError;
    if (!d->projectionDb->open(indexDir + QStringLiteral("/projection.sqlite3"),
                               &projectionError)) {
        qCWarning(lcLibrary) << "Cannot open the projection cache:" << projectionError.message();
        d->projectionDb.reset();
        return;
    }

    d->jobQueue = std::make_unique<projection::JobQueue>();
    core::Error jobQueueError;
    if (!d->jobQueue->open(indexDir + QStringLiteral("/jobs.sqlite3"), &jobQueueError)) {
        qCWarning(lcLibrary) << "Cannot open the job queue:" << jobQueueError.message();
        d->jobQueue.reset();
        return;
    }
    d->jobQueue->recoverInterruptedJobs(nullptr);

    d->fileSystem = std::make_unique<scan::QtFileSystem>();
    d->metadataReader = std::make_unique<metadata::BuiltinMetadataReader>(d->fileSystem.get());
    d->scanner = std::make_unique<scan::Scanner>(d->fileSystem.get(), d->metadataReader.get(),
                                                 d->store);

    // The thumbnail cache and image/video renderers back the model's
    // request service. CompositeRenderer tries the image renderer first and
    // falls back to decoding a video frame through Qt Multimedia, so the
    // model never needs to know which kind of file it asked for.
    const QString thumbnailCacheDir =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/thumbnails");
    d->thumbnailCache = std::make_unique<thumbnail::ThumbnailDiskCache>(thumbnailCacheDir);
    d->imageRenderer = std::make_unique<thumbnail::ImageRenderer>();
    d->videoRenderer = std::make_unique<thumbnail::VideoFrameRenderer>();
    d->compositeRenderer = std::make_unique<thumbnail::CompositeRenderer>(d->imageRenderer.get(),
                                                                          d->videoRenderer.get());
    d->thumbnailService = std::make_unique<thumbnail::ThumbnailService>(
            d->thumbnailCache.get(), d->compositeRenderer.get());

    d->model->setDatabase(d->projectionDb.get());
    d->model->setRequestService(d->thumbnailService.get());

    // The job dispatcher runs ScanRoot/ReconcileRoot workers on its own
    // thread pool. The worker itself only touches the scanner and the
    // durable store, never the projection's SQLite connection: rebuilding
    // the projection and reloading the model happen below, in the
    // jobSucceeded handler, which JobDispatcher guarantees runs back on this
    // object's own thread. This mirrors how JobDispatcher already posts its
    // own queue mutations back to its owning thread rather than mutating
    // SQLite from a pool thread.
    d->dispatcher = std::make_unique<projection::JobDispatcher>(d->jobQueue.get());
    scan::Scanner *scanner = d->scanner.get();
    core::DurableStore *store = d->store;
    d->dispatcher->registerWorker(
            core::JobKind::ScanRoot,
            [scanner, store](const core::JobRecord &job, const std::atomic<bool> &isCancelled) {
                return watch::runReconcileJob(job, isCancelled, *scanner, nullptr, *store);
            });
    d->dispatcher->registerWorker(
            core::JobKind::ReconcileRoot,
            [scanner, store](const core::JobRecord &job, const std::atomic<bool> &isCancelled) {
                return watch::runReconcileJob(job, isCancelled, *scanner, nullptr, *store);
            });

    projection::ProjectionDatabase *projectionDb = d->projectionDb.get();
    browser::MediaLibraryModel *model = d->model.get();
    connect(d->dispatcher.get(), &projection::JobDispatcher::jobSucceeded, this,
            [projectionDb, store, model](const QString &) {
                // Every job on this queue is a ScanRoot or ReconcileRoot;
                // both mean "the durable store may have changed", so the
                // projection is rebuilt and the model reloaded unconditionally.
                core::Error rebuildError;
                if (projectionDb->rebuildFrom(*store, &rebuildError)) {
                    model->reload();
                } else {
                    qCWarning(lcLibrary) << "Projection rebuild failed:" << rebuildError.message();
                }
            });
    d->dispatcher->start();

    for (const QString &path : normalizedPaths) {
        scan::LibraryRoot root;
        root.absolutePath = path;

        core::JobRecord scanJob;
        scanJob.id = core::JobId::generate();
        scanJob.kind = core::JobKind::ScanRoot;
        scanJob.priority = core::JobPriority::Background;
        scanJob.createdAt = QDateTime::currentDateTimeUtc();
        scanJob.coalescingKey = QStringLiteral("watch-reconcile:%1").arg(path);
        scanJob.payload = watch::makeRootJobPayload(root);
        core::Error enqueueError;
        if (!d->jobQueue->enqueue(scanJob, &enqueueError)) {
            qCWarning(lcLibrary) << "Cannot enqueue initial scan for" << path << ":"
                                  << enqueueError.message();
        }

        auto adapter = std::make_unique<watch::QtDirectoryWatchAdapter>();
        auto service = std::make_unique<watch::WatchService>(adapter.get(), d->jobQueue.get());
        core::Error watchError;
        if (!service->start(root, &watchError)) {
            qCWarning(lcLibrary) << "Cannot watch" << path << ":" << watchError.message();
        }
        d->watchAdapters.push_back(std::move(adapter));
        d->watchServices.push_back(std::move(service));
    }

    d->ready = true;
#endif
}

} // namespace pimio::app
