#include "pimio/testing/fake_clock.h"

namespace pimio::testing {

FakeClock::FakeClock(QDateTime startUtc)
    : m_nowUtc(std::move(startUtc))
{
}

QDateTime FakeClock::nowUtc() const
{
    return m_nowUtc;
}

qint64 FakeClock::monotonicMSecs() const
{
    return m_monotonicMSecs;
}

void FakeClock::advance(qint64 milliseconds)
{
    m_monotonicMSecs += milliseconds;
    if (m_nowUtc.isValid()) {
        m_nowUtc = m_nowUtc.addMSecs(milliseconds);
    }
}

void FakeClock::setWallClock(QDateTime utc)
{
    m_nowUtc = std::move(utc);
}

} // namespace pimio::testing
