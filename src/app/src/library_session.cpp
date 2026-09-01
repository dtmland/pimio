#include "pimio/app/library_session.h"

#include "pimio/app/application.h"
#include "pimio/app/library_activity.h"
#include "pimio/browser/media_library_model.h"
#include "pimio/browser/thumbnail_image_provider.h"
#include "pimio/core/error.h"
#include "pimio/core/job.h"
#include "pimio/metadata/builtin_metadata_reader.h"
#include "pimio/projection/job_dispatcher.h"
#include "pimio/projection/job_queue.h"
#include "pimio/projection/projection_database.h"
#include "pimio/scan/library_root.h"
#include "pimio/settings/settings.h"
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
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <vector>

namespace pimio::app {

namespace {
Q_LOGGING_CATEGORY(lcLibrary, "pimio.app.library")

/// Locates the repository created for the current path-based v1.0 command-line
/// entry point. The repository's descriptor, not this locator, is its identity.
QString repositoryDirectoryFor(const QStringList &libraryPaths)
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

QString indexDirectoryFor(const QString &libraryId)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/library-indexes/") + libraryId;
}

QString thumbnailDirectoryFor(const QString &libraryId)
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/libraries/") + libraryId + QStringLiteral("/thumbnails");
}

/// How often, at most, the grid is refreshed while a scan is running. Four
/// times a second is fast enough to look continuous and slow enough that
/// re-querying the ordered id list is not what the scan spends its time on.
constexpr int kModelRefreshIntervalMs = 250;

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
    std::unique_ptr<LibraryActivity> activity;

    std::vector<std::unique_ptr<watch::QtDirectoryWatchAdapter>> watchAdapters;
    std::vector<std::unique_ptr<watch::WatchService>> watchServices;

    // Coalesces the model reloads a batching scan would otherwise ask for
    // once per commit.
    QTimer refreshTimer;

    QStringList libraryPaths;
    bool prepared = false;
    bool started = false;
};

LibrarySession::LibrarySession(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->refreshTimer.setSingleShot(true);
    d->refreshTimer.setInterval(kModelRefreshIntervalMs);
    connect(&d->refreshTimer, &QTimer::timeout, this, [this] {
        if (d->model) {
            d->model->reload();
        }
    });
}

LibrarySession::~LibrarySession() = default;

namespace {

/// Maps a stored sort choice to the projection's ORDER BY. Written as a
/// switch rather than a cast so the two enumerations can be reordered or
/// extended independently and the compiler reports the mismatch.
projection::ProjectionDatabase::SortKey projectionSortKey(settings::SortKey key)
{
    switch (key) {
    case settings::SortKey::CaptureTime:
        return projection::ProjectionDatabase::SortKey::CaptureTime;
    case settings::SortKey::FileName:
        return projection::ProjectionDatabase::SortKey::FileName;
    case settings::SortKey::FileDate:
        return projection::ProjectionDatabase::SortKey::FileDate;
    case settings::SortKey::FileType:
        return projection::ProjectionDatabase::SortKey::FileType;
    case settings::SortKey::FileSize:
        return projection::ProjectionDatabase::SortKey::FileSize;
    }
    return projection::ProjectionDatabase::SortKey::CaptureTime;
}

} // namespace

void LibrarySession::applySettings()
{
    if (!d->model) {
        return;
    }
    const settings::Settings &userSettings = applicationSettings();

    d->model->setSorting(static_cast<int>(projectionSortKey(userSettings.sortKey())),
                         userSettings.sortDescending());

    // A tile is sized in device-independent pixels but drawn in physical
    // ones, so a 176-point tile on a 2x display needs a 352-pixel image if it
    // is not to be an upscale of a smaller render.
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal pixelRatio = screen ? screen->devicePixelRatio() : 1.0;
    d->model->setTilePixelSize(qRound(userSettings.tileSize() * pixelRatio));

    if (d->scanner) {
        d->scanner->setCommitBatchSize(userSettings.scanBatchSize());
    }
}

void LibrarySession::applyScanBatch(const QList<core::MediaRecord> &records, int indexedCount)
{
    if (d->activity) {
        d->activity->setIndexedCount(indexedCount);
    }
    if (!d->projectionDb || !d->projectionDb->isOpen() || records.isEmpty()) {
        return;
    }

    // The projection's state token is deliberately not advanced here: only a
    // full rebuild, once the scan finishes, can vouch for the whole store.
    core::Error applyError;
    if (!d->projectionDb->applyRecords(records, &applyError)) {
        qCWarning(lcLibrary) << "Could not project a scan batch:" << applyError.message();
        return;
    }
    scheduleModelRefresh();
}

void LibrarySession::scheduleModelRefresh()
{
    if (!d->refreshTimer.isActive()) {
        d->refreshTimer.start();
    }
}

void LibrarySession::prepare(const QStringList &libraryPaths, QQmlApplicationEngine &engine)
{
    if (d->prepared) {
        return;
    }
    d->prepared = true;
    d->libraryPaths = libraryPaths;

    // The image provider and (empty, for now) model are registered
    // unconditionally, even with no library path at all or if opening
    // storage fails below: Main.qml's "typeof mediaLibraryModel ===
    // undefined" guard means either state renders the same empty-library
    // placeholder, so there is no behavioural difference, but registering
    // them up front means a library added by a future session never has to
    // race a context property appearing after QML already bound to it.
    d->model = std::make_unique<browser::MediaLibraryModel>();
    d->activity = std::make_unique<LibraryActivity>();
    auto *provider = new browser::ThumbnailImageProvider();
    d->imageProvider = provider;
    engine.addImageProvider(QStringLiteral("thumbnail"), provider);
    d->model->setImageProvider(d->imageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaLibraryModel"), d->model.get());
    engine.rootContext()->setContextProperty(QStringLiteral("libraryActivity"),
                                             d->activity.get());

    // Settings drive the model directly rather than through QML: the
    // dialog is one way to change a setting, not the only one, and the sort
    // order and thumbnail size are properties of the library view itself.
    applySettings();
    settings::Settings &userSettings = applicationSettings();
    connect(&userSettings, &settings::Settings::sortKeyChanged, this,
            [this] { applySettings(); });
    connect(&userSettings, &settings::Settings::sortDescendingChanged, this,
            [this] { applySettings(); });
    connect(&userSettings, &settings::Settings::tileSizeChanged, this,
            [this] { applySettings(); });
    connect(&userSettings, &settings::Settings::scanBatchSizeChanged, this,
            [this] { applySettings(); });

    // This state is visible in Main.qml's first frame. The synchronous storage
    // and recursive-watch setup happens later in start(), after that frame has
    // reached the display.
    d->activity->setScanning(!libraryPaths.isEmpty());
}

void LibrarySession::start()
{
    if (!d->prepared || d->started) {
        return;
    }
    d->started = true;

    const QStringList normalizedPaths = normalizedLibraryPaths(d->libraryPaths);
    if (normalizedPaths.isEmpty()) {
        d->activity->setScanning(false);
        return;
    }

#ifndef PIMIO_HAVE_LORE
    qCWarning(lcLibrary) << "Cannot open a library: this build has no durable store (LORE was "
                           "unavailable at compile time). The library stays empty; see "
                           "docs/decisions/0001-lore-durable-store.md.";
    d->activity->setScanning(false);
    return;
#else
    settings::Settings &userSettings = applicationSettings();
    const QString repositoryDir = repositoryDirectoryFor(normalizedPaths);
    QDir().mkpath(repositoryDir);

    d->loreStore =
            std::make_unique<lore::LoreDurableStore>(repositoryDir + QStringLiteral("/store"));
    core::Error storeError;
    if (!d->loreStore->open(&storeError)) {
        qCWarning(lcLibrary) << "Cannot open durable storage for" << normalizedPaths << ":"
                              << storeError.message();
        d->loreStore.reset();
        d->activity->setScanning(false);
        return;
    }
    d->store = d->loreStore.get();

    core::Error descriptorError;
    auto descriptor = d->store->libraryDescriptor(&descriptorError);
    if (!descriptor && descriptorError.code() == core::ErrorCode::NotFound) {
        const QString defaultName = QFileInfo(normalizedPaths.constFirst()).fileName();
        if (!d->store->createLibrary(defaultName, &descriptorError)) {
            qCWarning(lcLibrary) << "Cannot create the library descriptor:"
                                 << descriptorError.message();
            d->activity->setScanning(false);
            return;
        }
        descriptor = d->store->libraryDescriptor(&descriptorError);
    }
    if (!descriptor || !descriptor->isValid()) {
        qCWarning(lcLibrary) << "Cannot load the library descriptor:"
                             << descriptorError.message();
        d->activity->setScanning(false);
        return;
    }

    const QString indexDir = indexDirectoryFor(descriptor->id);
    QDir().mkpath(indexDir);

    d->projectionDb = std::make_unique<projection::ProjectionDatabase>();
    core::Error projectionError;
    if (!d->projectionDb->open(indexDir + QStringLiteral("/projection.sqlite3"),
                               &projectionError)) {
        qCWarning(lcLibrary) << "Cannot open the projection cache:" << projectionError.message();
        d->projectionDb.reset();
        d->activity->setScanning(false);
        return;
    }

    d->jobQueue = std::make_unique<projection::JobQueue>();
    core::Error jobQueueError;
    if (!d->jobQueue->open(indexDir + QStringLiteral("/jobs.sqlite3"), &jobQueueError)) {
        qCWarning(lcLibrary) << "Cannot open the job queue:" << jobQueueError.message();
        d->jobQueue.reset();
        d->activity->setScanning(false);
        return;
    }
    d->jobQueue->recoverInterruptedJobs(nullptr);

    d->fileSystem = std::make_unique<scan::QtFileSystem>();
    d->metadataReader = std::make_unique<metadata::BuiltinMetadataReader>(d->fileSystem.get());
    d->scanner = std::make_unique<scan::Scanner>(d->fileSystem.get(), d->metadataReader.get(),
                                                 d->store);
    // Committing in batches is what lets the grid fill in while the scan is
    // still walking the library; see settings::Settings::scanBatchSize().
    d->scanner->setCommitBatchSize(userSettings.scanBatchSize());

    // The thumbnail cache and image/video renderers back the model's
    // request service. CompositeRenderer tries the image renderer first and
    // falls back to decoding a video frame through Qt Multimedia, so the
    // model never needs to know which kind of file it asked for.
    const QString thumbnailCacheDir = thumbnailDirectoryFor(descriptor->id);
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
    // durable store, never the projection's SQLite connection: projecting
    // what a scan has committed so far, rebuilding the projection, and
    // reloading the model all happen on this object's own thread, either in
    // the jobSucceeded handler (which JobDispatcher guarantees runs there) or
    // in applyScanBatch(), which the worker reaches only by posting to it.
    // This mirrors how JobDispatcher already posts its own queue mutations
    // back to its owning thread rather than mutating SQLite from a pool
    // thread.
    d->dispatcher = std::make_unique<projection::JobDispatcher>(d->jobQueue.get());
    scan::Scanner *scanner = d->scanner.get();
    core::DurableStore *store = d->store;

    // Copies the batch, because the scan resumes as soon as this returns and
    // the records it committed are its own local state.
    LibrarySession *session = this;
    const scan::Scanner::ProgressCallback onScanProgress =
            [session](const QList<core::MediaRecord> &committed,
                      const scan::Scanner::Result &progress) {
                const int indexed = progress.added + progress.updated + progress.unchanged;
                QMetaObject::invokeMethod(
                        session,
                        [session, committed, indexed] {
                            session->applyScanBatch(committed, indexed);
                        },
                        Qt::QueuedConnection);
            };

    const auto worker = [scanner, store, onScanProgress](const core::JobRecord &job,
                                                         const std::atomic<bool> &isCancelled) {
        return watch::runReconcileJob(job, isCancelled, *scanner, nullptr, *store, nullptr,
                                      onScanProgress);
    };
    d->dispatcher->registerWorker(core::JobKind::ScanRoot, worker);
    d->dispatcher->registerWorker(core::JobKind::ReconcileRoot, worker);

    projection::ProjectionDatabase *projectionDb = d->projectionDb.get();
    browser::MediaLibraryModel *model = d->model.get();
    LibraryActivity *activity = d->activity.get();
    projection::JobDispatcher *dispatcher = d->dispatcher.get();

    connect(d->dispatcher.get(), &projection::JobDispatcher::jobStarted, this,
            [activity](const QString &) { activity->setScanning(true); });

    // A job that failed or was cancelled leaves the same question as one that
    // succeeded — is anything still running? — so all three settle the
    // indicator the same way.
    const auto settleActivity = [activity, dispatcher](const QString &) {
        activity->setScanning(dispatcher->runningCount() > 0);
    };
    connect(d->dispatcher.get(), &projection::JobDispatcher::jobFailed, this, settleActivity);
    connect(d->dispatcher.get(), &projection::JobDispatcher::jobCancelled, this, settleActivity);

    connect(d->dispatcher.get(), &projection::JobDispatcher::jobSucceeded, this,
            [this, projectionDb, store, model, settleActivity](const QString &jobId) {
                // Every job on this queue is a ScanRoot or ReconcileRoot;
                // both mean "the durable store may have changed", so the
                // projection is rebuilt and the model reloaded unconditionally.
                // This rebuild is also what makes the projection trustworthy
                // again: the batches applied while the scan ran deliberately
                // left its recorded state token behind.
                core::Error rebuildError;
                if (projectionDb->rebuildFrom(*store, &rebuildError)) {
                    model->reload();
                } else {
                    qCWarning(lcLibrary) << "Projection rebuild failed:" << rebuildError.message();
                }
                settleActivity(jobId);
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

#endif
}

} // namespace pimio::app
