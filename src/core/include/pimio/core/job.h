#pragma once

#include "pimio/core/error.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace pimio::core {

/// What a queued job does. The queue itself is generic; workers are registered
/// per kind so new work types do not change the queue contract.
enum class JobKind {
    Unknown,
    ScanRoot,
    ReconcileRoot,
    ReadMetadata,
    GenerateThumbnail,
    GeneratePreview,
    ExtractVideoFrame,
    WriteMetadata,
    Export,
};

QString toString(JobKind kind);
JobKind jobKindFromString(const QString &value);

/// Scheduling class. Lower numeric values run first.
///
/// Interactive work must always outrank background analysis so that scrolling
/// stays responsive while a large scan is running.
enum class JobPriority {
    Interactive = 0,  ///< Visible thumbnails and direct user actions.
    UserInitiated = 1, ///< Viewer decoding and exports the user asked for.
    Background = 2,   ///< Scans, watching, reconciliation.
    Opportunistic = 3, ///< Optional analysis such as scene detection.
};

QString toString(JobPriority priority);
JobPriority jobPriorityFromString(const QString &value);

/// Lifecycle of a queued job. Terminal states are Succeeded, Failed, and
/// Cancelled.
enum class JobState {
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

QString toString(JobState state);
JobState jobStateFromString(const QString &value);
bool isTerminal(JobState state);

/// Opaque identifier for a queued job.
class JobId
{
public:
    JobId() = default;
    explicit JobId(QString value);

    static JobId generate();

    bool isValid() const;
    const QString &value() const;

    bool operator==(const JobId &other) const = default;

private:
    QString m_value;
};

size_t qHash(const JobId &id, size_t seed = 0);

/// A durable job queue entry.
///
/// The record is the whole persisted state of a job. Restarting the
/// application must be able to resume purely from these fields.
class JobRecord
{
public:
    JobId id;
    JobKind kind = JobKind::Unknown;
    JobPriority priority = JobPriority::Background;
    JobState state = JobState::Pending;

    /// Stable key used to collapse duplicate work, for example one thumbnail
    /// job per fingerprint. Empty means the job is never coalesced.
    QString coalescingKey;

    /// Job-specific input. Kept as JSON so the queue stays generic.
    QJsonObject payload;

    int attempts = 0;
    int maxAttempts = 3;

    QDateTime createdAt;
    QDateTime notBefore;

    Error lastError;

    /// True when the job failed with a retryable error and still has attempts
    /// left.
    bool canRetry() const;

    /// Deterministic ordering: priority first, then creation time, then id.
    /// Ties never depend on hash order, so a restart replays the same order.
    bool runsBefore(const JobRecord &other) const;

    bool operator==(const JobRecord &other) const = default;

    QJsonObject toJson() const;
    static JobRecord fromJson(const QJsonObject &object);

    const QJsonObject &unrecognizedFields() const;
    void setUnrecognizedFields(QJsonObject fields);

    static QStringList knownKeys();

private:
    QJsonObject m_unrecognizedFields;
};

} // namespace pimio::core
