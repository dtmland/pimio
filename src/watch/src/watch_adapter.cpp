#include "pimio/watch/watch_adapter.h"

namespace pimio::watch {

WatchAdapter::WatchAdapter(QObject *parent)
    : QObject(parent)
{
}

WatchAdapter::~WatchAdapter() = default;

} // namespace pimio::watch
