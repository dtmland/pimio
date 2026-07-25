#include "pimio/core/media_request.h"

namespace pimio::core {

QString toString(MediaRequestKind kind)
{
    switch (kind) {
    case MediaRequestKind::Thumbnail:
        return QStringLiteral("thumbnail");
    case MediaRequestKind::Preview:
        return QStringLiteral("preview");
    case MediaRequestKind::VideoFrame:
        return QStringLiteral("videoFrame");
    }
    return QStringLiteral("thumbnail");
}

QString MediaRequest::cacheKey() const
{
    // Every field that can change the produced bytes is part of the key.
    // The media id is deliberately excluded: identical content rendered the
    // same way produces the same bytes, so two items sharing a fingerprint
    // share the cache entry.
    return QStringLiteral("%1/%2/%3x%4/%5/r%6")
            .arg(fingerprint.cacheKey(), toString(kind))
            .arg(targetSize.width())
            .arg(targetSize.height())
            .arg(positionMs)
            .arg(recipeRevision);
}

MediaRequestHandle::MediaRequestHandle(quint64 value)
    : m_value(value)
{
}

bool MediaRequestHandle::isValid() const
{
    return m_value != 0;
}

quint64 MediaRequestHandle::value() const
{
    return m_value;
}

MediaRequestService::~MediaRequestService() = default;

} // namespace pimio::core
