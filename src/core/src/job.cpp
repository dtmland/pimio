#include "pimio/core/job.h"

#include "pimio/core/serialization.h"

#include <QHash>
#include <QUuid>

namespace pimio::core {
namespace {

constexpr QLatin1StringView kIdKey{"id"};
constexpr QLatin1StringView kKindKey{"kind"};
constexpr QLatin1StringView kPriorityKey{"priority"};
constexpr QLatin1StringView kStateKey{"state"};
constexpr QLatin1StringView kCoalescingKeyKey{"coalescingKey"};
constexpr QLatin1StringView kPayloadKey{"payload"};
constexpr QLatin1StringView kAttemptsKey{"attempts"};
constexpr QLatin1StringView kMaxAttemptsKey{"maxAttempts"};
constexpr QLatin1StringView kCreatedAtKey{"createdAt"};
constexpr QLatin1StringView kNotBeforeKey{"notBefore"};
constexpr QLatin1StringView kLastErrorKey{"lastError"};

QString toIsoString(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime fromIsoString(const QString &value)
{
    if (value.isEmpty()) {
        return QDateTime();
    }
    return QDateTime::fromString(value, Qt::ISODateWithMs).toUTC();
}

struct KindName
{
    JobKind kind;
    QLatin1StringView name;
};

constexpr KindName kKindNames[] = {
    {JobKind::Unknown, QLatin1StringView("unknown")},
    {JobKind::ScanRoot, QLatin1StringView("scanRoot")},
    {JobKind::ReconcileRoot, QLatin1StringView("reconcileRoot")},
    {JobKind::ReadMetadata, QLatin1StringView("readMetadata")},
    {JobKind::GenerateThumbnail, QLatin1StringView("generateThumbnail")},
    {JobKind::GeneratePreview, QLatin1StringView("generatePreview")},
    {JobKind::ExtractVideoFrame, QLatin1StringView("extractVideoFrame")},
    {JobKind::WriteMetadata, QLatin1StringView("writeMetadata")},
    {JobKind::Export, QLatin1StringView("export")},
};

} // namespace

QString toString(JobKind kind)
{
    for (const KindName &entry : kKindNames) {
        if (entry.kind == kind) {
            return QString(entry.name);
        }
    }
    return QStringLiteral("unknown");
}

JobKind jobKindFromString(const QString &value)
{
    for (const KindName &entry : kKindNames) {
        if (value == entry.name) {
            return entry.kind;
        }
    }
    return JobKind::Unknown;
}

QString toString(JobPriority priority)
{
    switch (priority) {
    case JobPriority::Interactive:
        return QStringLiteral("interactive");
    case JobPriority::UserInitiated:
        return QStringLiteral("userInitiated");
    case JobPriority::Background:
        return QStringLiteral("background");
    case JobPriority::Opportunistic:
        return QStringLiteral("opportunistic");
    }
    return QStringLiteral("background");
}

JobPriority jobPriorityFromString(const QString &value)
{
    if (value == QLatin1StringView("interactive")) {
        return JobPriority::Interactive;
    }
    if (value == QLatin1StringView("userInitiated")) {
        return JobPriority::UserInitiated;
    }
    if (value == QLatin1StringView("opportunistic")) {
        return JobPriority::Opportunistic;
    }
    return JobPriority::Background;
}

QString toString(JobState state)
{
    switch (state) {
    case JobState::Pending:
        return QStringLiteral("pending");
    case JobState::Running:
        return QStringLiteral("running");
    case JobState::Succeeded:
        return QStringLiteral("succeeded");
    case JobState::Failed:
        return QStringLiteral("failed");
    case JobState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("pending");
}

JobState jobStateFromString(const QString &value)
{
    if (value == QLatin1StringView("running")) {
        return JobState::Running;
    }
    if (value == QLatin1StringView("succeeded")) {
        return JobState::Succeeded;
    }
    if (value == QLatin1StringView("failed")) {
        return JobState::Failed;
    }
    if (value == QLatin1StringView("cancelled")) {
        return JobState::Cancelled;
    }
    return JobState::Pending;
}

bool isTerminal(JobState state)
{
    switch (state) {
    case JobState::Succeeded:
    case JobState::Failed:
    case JobState::Cancelled:
        return true;
    case JobState::Pending:
    case JobState::Running:
        break;
    }
    return false;
}

JobId::JobId(QString value)
    : m_value(std::move(value))
{
}

JobId JobId::generate()
{
    return JobId(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool JobId::isValid() const
{
    return !m_value.isEmpty();
}

const QString &JobId::value() const
{
    return m_value;
}

size_t qHash(const JobId &id, size_t seed)
{
    return qHash(id.value(), seed);
}

bool JobRecord::canRetry() const
{
    return state == JobState::Failed && lastError.isRetryable() && attempts < maxAttempts;
}

bool JobRecord::runsBefore(const JobRecord &other) const
{
    if (priority != other.priority) {
        return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
    if (createdAt != other.createdAt) {
        // An invalid creation time sorts last so it can never starve real work.
        if (!createdAt.isValid()) {
            return false;
        }
        if (!other.createdAt.isValid()) {
            return true;
        }
        return createdAt < other.createdAt;
    }
    return id.value() < other.id.value();
}

QStringList JobRecord::knownKeys()
{
    return {
        kIdKey,          kKindKey,        kPriorityKey,  kStateKey,     kCoalescingKeyKey,
        kPayloadKey,     kAttemptsKey,    kMaxAttemptsKey, kCreatedAtKey, kNotBeforeKey,
        kLastErrorKey,
    };
}

const QJsonObject &JobRecord::unrecognizedFields() const
{
    return m_unrecognizedFields;
}

void JobRecord::setUnrecognizedFields(QJsonObject fields)
{
    m_unrecognizedFields = std::move(fields);
}

QJsonObject JobRecord::toJson() const
{
    QJsonObject object;
    object.insert(kSchemaVersionKey, kRecordSchemaVersion);
    object.insert(kIdKey, id.value());
    object.insert(kKindKey, toString(kind));
    object.insert(kPriorityKey, toString(priority));
    object.insert(kStateKey, toString(state));
    object.insert(kCoalescingKeyKey, coalescingKey);
    object.insert(kPayloadKey, payload);
    object.insert(kAttemptsKey, attempts);
    object.insert(kMaxAttemptsKey, maxAttempts);
    object.insert(kCreatedAtKey, toIsoString(createdAt));
    object.insert(kNotBeforeKey, toIsoString(notBefore));
    object.insert(kLastErrorKey, lastError.toJson());

    mergeUnknownFields(object, m_unrecognizedFields);
    return object;
}

JobRecord JobRecord::fromJson(const QJsonObject &object)
{
    JobRecord record;
    record.id = JobId(object.value(kIdKey).toString());
    record.kind = jobKindFromString(object.value(kKindKey).toString());
    record.priority = jobPriorityFromString(object.value(kPriorityKey).toString());
    record.state = jobStateFromString(object.value(kStateKey).toString());
    record.coalescingKey = object.value(kCoalescingKeyKey).toString();
    record.payload = object.value(kPayloadKey).toObject();
    record.attempts = object.value(kAttemptsKey).toInt();
    record.maxAttempts = object.value(kMaxAttemptsKey).toInt(3);
    record.createdAt = fromIsoString(object.value(kCreatedAtKey).toString());
    record.notBefore = fromIsoString(object.value(kNotBeforeKey).toString());
    record.lastError = Error::fromJson(object.value(kLastErrorKey).toObject());

    record.setUnrecognizedFields(unknownFields(object, knownKeys()));
    return record;
}

} // namespace pimio::core
