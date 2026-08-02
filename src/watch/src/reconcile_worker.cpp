#include "pimio/watch/reconcile_worker.h"

namespace pimio::watch {

namespace {
constexpr QLatin1StringView kRootPathKey{"rootPath"};
constexpr QLatin1StringView kFollowSymlinksKey{"followSymlinks"};
} // namespace

QJsonObject makeRootJobPayload(const scan::LibraryRoot &root)
{
    QJsonObject payload;
    payload.insert(kRootPathKey, root.absolutePath);
    payload.insert(kFollowSymlinksKey, root.followSymlinks);
    return payload;
}

scan::LibraryRoot rootFromJobPayload(const QJsonObject &payload)
{
    scan::LibraryRoot root;
    root.absolutePath = payload.value(kRootPathKey).toString();
    root.followSymlinks = payload.value(kFollowSymlinksKey).toBool(false);
    return root;
}

core::Error runReconcileJob(const core::JobRecord &job, const std::atomic<bool> &isCancelled,
                           scan::Scanner &scanner, projection::ProjectionDatabase *projection,
                           const core::DurableStore &store, scan::Scanner::Result *outResult)
{
    const scan::LibraryRoot root = rootFromJobPayload(job.payload);
    if (root.absolutePath.isEmpty()) {
        return core::Error(core::ErrorCode::Internal,
                           QStringLiteral("Reconcile job is missing its library root path"));
    }

    scan::Scanner::Result result;
    const core::Error scanError = scanner.scan(root, isCancelled, &result);
    if (outResult) {
        *outResult = result;
    }
    if (scanError.isError()) {
        return scanError;
    }
    if (isCancelled.load()) {
        return core::Error::cancelled();
    }

    if (projection != nullptr) {
        core::Error rebuildError;
        if (!projection->rebuildFrom(store, &rebuildError)) {
            return rebuildError;
        }
    }

    return core::Error();
}

} // namespace pimio::watch
