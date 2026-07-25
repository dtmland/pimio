#pragma once

#include <QJsonObject>
#include <QString>

namespace pimio::core {

/// Stable, user-facing classification of a failure.
///
/// Codes exist so the UI can react without parsing messages, and so that a
/// failure recorded in storage keeps its meaning across releases.
enum class ErrorCode {
    None,
    Cancelled,
    NotFound,
    PermissionDenied,
    UnsupportedMedia,
    CorruptData,
    OutOfSpace,
    Timeout,
    Conflict,
    StorageUnavailable,
    Interrupted,
    Internal,
};

QString toString(ErrorCode code);
ErrorCode errorCodeFromString(const QString &value);

/// True for failures that are worth retrying automatically.
bool isRetryable(ErrorCode code);

/// A failure with enough context to be shown, logged, and diagnosed.
class Error
{
public:
    Error() = default;
    Error(ErrorCode code, QString message);

    static Error cancelled();

    bool isError() const;
    ErrorCode code() const;
    const QString &message() const;

    /// Free-form diagnostic context such as the path or job that failed.
    const QJsonObject &context() const;
    void setContext(QJsonObject context);
    Error withContext(QJsonObject context) const;

    bool isRetryable() const;

    bool operator==(const Error &other) const = default;

    QJsonObject toJson() const;
    static Error fromJson(const QJsonObject &object);

private:
    ErrorCode m_code = ErrorCode::None;
    QString m_message;
    QJsonObject m_context;
};

} // namespace pimio::core
