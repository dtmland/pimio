#include "pimio/testing/recording_media_request_service.h"

namespace pimio::testing {

core::MediaRequestHandle RecordingMediaRequestService::request(const core::MediaRequest &request,
                                                               ResultCallback onResult,
                                                               ErrorCallback onError)
{
    const quint64 handle = m_nextHandle++;
    m_pending.insert(handle, Pending{request, std::move(onResult), std::move(onError)});
    m_order.append(handle);
    m_requestedCacheKeys.append(request.cacheKey());
    return core::MediaRequestHandle(handle);
}

void RecordingMediaRequestService::cancel(core::MediaRequestHandle handle)
{
    const auto it = m_pending.constFind(handle.value());
    if (it == m_pending.constEnd()) {
        // Cancelling an unknown or already finished request is not an error.
        return;
    }
    const Pending pending = it.value();
    m_pending.remove(handle.value());
    m_order.removeAll(handle.value());
    ++m_cancelledCount;
    if (pending.onError) {
        pending.onError(pending.request, core::Error::cancelled());
    }
}

void RecordingMediaRequestService::cancelAllExcept(const QList<core::MediaRequestHandle> &keep)
{
    QList<quint64> keepValues;
    keepValues.reserve(keep.size());
    for (const core::MediaRequestHandle &handle : keep) {
        keepValues.append(handle.value());
    }

    const QList<quint64> current = m_order;
    for (quint64 handle : current) {
        if (!keepValues.contains(handle)) {
            cancel(core::MediaRequestHandle(handle));
        }
    }
}

QStringList RecordingMediaRequestService::requestedCacheKeys() const
{
    return m_requestedCacheKeys;
}

QStringList RecordingMediaRequestService::pendingCacheKeys() const
{
    QStringList keys;
    keys.reserve(m_order.size());
    for (quint64 handle : m_order) {
        keys.append(m_pending.value(handle).request.cacheKey());
    }
    return keys;
}

int RecordingMediaRequestService::cancelledCount() const
{
    return m_cancelledCount;
}

bool RecordingMediaRequestService::complete(core::MediaRequestHandle handle,
                                            const core::MediaResult &result)
{
    const auto it = m_pending.constFind(handle.value());
    if (it == m_pending.constEnd()) {
        return false;
    }
    const Pending pending = it.value();
    m_pending.remove(handle.value());
    m_order.removeAll(handle.value());
    if (pending.onResult) {
        pending.onResult(pending.request, result);
    }
    return true;
}

bool RecordingMediaRequestService::fail(core::MediaRequestHandle handle, const core::Error &error)
{
    const auto it = m_pending.constFind(handle.value());
    if (it == m_pending.constEnd()) {
        return false;
    }
    const Pending pending = it.value();
    m_pending.remove(handle.value());
    m_order.removeAll(handle.value());
    if (pending.onError) {
        pending.onError(pending.request, error);
    }
    return true;
}

} // namespace pimio::testing
