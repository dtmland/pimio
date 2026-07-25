#pragma once

#include "pimio/core/media_request.h"

#include <QHash>
#include <QList>

namespace pimio::testing {

/// MediaRequestService that records requests instead of rendering anything.
///
/// Tests use it to assert that only the expected items were requested and that
/// scrolled-away work is cancelled.
class RecordingMediaRequestService final : public core::MediaRequestService
{
public:
    struct Pending
    {
        core::MediaRequest request;
        ResultCallback onResult;
        ErrorCallback onError;
    };

    core::MediaRequestHandle request(const core::MediaRequest &request, ResultCallback onResult,
                                     ErrorCallback onError) override;
    void cancel(core::MediaRequestHandle handle) override;
    void cancelAllExcept(const QList<core::MediaRequestHandle> &keep) override;

    /// Cache keys of every request received, in order.
    QStringList requestedCacheKeys() const;

    /// Cache keys of requests that are still pending.
    QStringList pendingCacheKeys() const;

    int cancelledCount() const;

    /// Completes a pending request with \a result.
    bool complete(core::MediaRequestHandle handle, const core::MediaResult &result);

    /// Fails a pending request with \a error.
    bool fail(core::MediaRequestHandle handle, const core::Error &error);

private:
    QHash<quint64, Pending> m_pending;
    QList<quint64> m_order;
    QStringList m_requestedCacheKeys;
    int m_cancelledCount = 0;
    quint64 m_nextHandle = 1;
};

} // namespace pimio::testing
