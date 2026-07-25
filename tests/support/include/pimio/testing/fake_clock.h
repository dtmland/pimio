#pragma once

#include "pimio/core/clock.h"

namespace pimio::testing {

/// Clock whose time only advances when a test advances it.
class FakeClock final : public core::Clock
{
public:
    explicit FakeClock(QDateTime startUtc);

    QDateTime nowUtc() const override;
    qint64 monotonicMSecs() const override;

    void advance(qint64 milliseconds);

    /// Moves wall-clock time backwards without moving monotonic time, which is
    /// what a clock correction or DST change looks like to the application.
    void setWallClock(QDateTime utc);

private:
    QDateTime m_nowUtc;
    qint64 m_monotonicMSecs = 0;
};

} // namespace pimio::testing
