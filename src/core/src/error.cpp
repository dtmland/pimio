#include "pimio/core/error.h"

namespace pimio::core {
namespace {

constexpr QLatin1StringView kCodeKey{"code"};
constexpr QLatin1StringView kMessageKey{"message"};
constexpr QLatin1StringView kContextKey{"context"};

struct CodeName
{
    ErrorCode code;
    QLatin1StringView name;
};

constexpr CodeName kCodeNames[] = {
    {ErrorCode::None, QLatin1StringView("none")},
    {ErrorCode::Cancelled, QLatin1StringView("cancelled")},
    {ErrorCode::NotFound, QLatin1StringView("notFound")},
    {ErrorCode::PermissionDenied, QLatin1StringView("permissionDenied")},
    {ErrorCode::UnsupportedMedia, QLatin1StringView("unsupportedMedia")},
    {ErrorCode::CorruptData, QLatin1StringView("corruptData")},
    {ErrorCode::OutOfSpace, QLatin1StringView("outOfSpace")},
    {ErrorCode::Timeout, QLatin1StringView("timeout")},
    {ErrorCode::Conflict, QLatin1StringView("conflict")},
    {ErrorCode::StorageUnavailable, QLatin1StringView("storageUnavailable")},
    {ErrorCode::Interrupted, QLatin1StringView("interrupted")},
    {ErrorCode::Internal, QLatin1StringView("internal")},
};

} // namespace

QString toString(ErrorCode code)
{
    for (const CodeName &entry : kCodeNames) {
        if (entry.code == code) {
            return QString(entry.name);
        }
    }
    return QStringLiteral("internal");
}

ErrorCode errorCodeFromString(const QString &value)
{
    for (const CodeName &entry : kCodeNames) {
        if (value == entry.name) {
            return entry.code;
        }
    }
    return ErrorCode::Internal;
}

bool isRetryable(ErrorCode code)
{
    switch (code) {
    case ErrorCode::Timeout:
    case ErrorCode::StorageUnavailable:
    case ErrorCode::Interrupted:
        return true;
    case ErrorCode::None:
    case ErrorCode::Cancelled:
    case ErrorCode::NotFound:
    case ErrorCode::PermissionDenied:
    case ErrorCode::UnsupportedMedia:
    case ErrorCode::CorruptData:
    case ErrorCode::OutOfSpace:
    case ErrorCode::Conflict:
    case ErrorCode::Internal:
        break;
    }
    return false;
}

Error::Error(ErrorCode code, QString message)
    : m_code(code)
    , m_message(std::move(message))
{
}

Error Error::cancelled()
{
    return Error(ErrorCode::Cancelled, QStringLiteral("The operation was cancelled."));
}

bool Error::isError() const
{
    return m_code != ErrorCode::None;
}

ErrorCode Error::code() const
{
    return m_code;
}

const QString &Error::message() const
{
    return m_message;
}

const QJsonObject &Error::context() const
{
    return m_context;
}

void Error::setContext(QJsonObject context)
{
    m_context = std::move(context);
}

Error Error::withContext(QJsonObject context) const
{
    Error copy = *this;
    copy.setContext(std::move(context));
    return copy;
}

bool Error::isRetryable() const
{
    return core::isRetryable(m_code);
}

QJsonObject Error::toJson() const
{
    QJsonObject object;
    object.insert(kCodeKey, toString(m_code));
    object.insert(kMessageKey, m_message);
    object.insert(kContextKey, m_context);
    return object;
}

Error Error::fromJson(const QJsonObject &object)
{
    Error error;
    error.m_code = errorCodeFromString(object.value(kCodeKey).toString());
    error.m_message = object.value(kMessageKey).toString();
    error.m_context = object.value(kContextKey).toObject();
    return error;
}

} // namespace pimio::core
