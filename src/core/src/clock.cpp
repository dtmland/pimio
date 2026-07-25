#include "pimio/core/clock.h"

#include <QElapsedTimer>

namespace pimio::core {
namespace {

QElapsedTimer &processTimer()
{
    static QElapsedTimer timer = [] {
        QElapsedTimer created;
        created.start();
        return created;
    }();
    return timer;
}

} // namespace

Clock::~Clock() = default;

SystemClock::SystemClock()
{
    processTimer();
}

SystemClock::~SystemClock() = default;

QDateTime SystemClock::nowUtc() const
{
    return QDateTime::currentDateTimeUtc();
}

qint64 SystemClock::monotonicMSecs() const
{
    return processTimer().elapsed();
}

} // namespace pimio::core
