#pragma once

#include <QDateTime>

namespace pimio::core {

/// Source of time for the core.
///
/// Nothing in the core calls QDateTime::currentDateTime() directly. Tests
/// substitute a controllable clock so that retry backoff, job ordering, and
/// timestamp repair are deterministic.
class Clock
{
public:
    virtual ~Clock();

    /// Current wall-clock time in UTC. May jump backwards.
    virtual QDateTime nowUtc() const = 0;

    /// Monotonic milliseconds since an unspecified origin. Never goes
    /// backwards, so it is the only safe basis for measuring durations.
    virtual qint64 monotonicMSecs() const = 0;
};

/// Clock backed by the operating system.
class SystemClock final : public Clock
{
public:
    SystemClock();
    ~SystemClock() override;

    QDateTime nowUtc() const override;
    qint64 monotonicMSecs() const override;
};

} // namespace pimio::core
