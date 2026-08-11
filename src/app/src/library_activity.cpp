#include "pimio/app/library_activity.h"

namespace pimio::app {

LibraryActivity::LibraryActivity(QObject *parent)
    : QObject(parent)
{
}

LibraryActivity::~LibraryActivity() = default;

bool LibraryActivity::isScanning() const
{
    return m_scanning;
}

void LibraryActivity::setScanning(bool scanning)
{
    if (scanning == m_scanning) {
        return;
    }
    m_scanning = scanning;
    if (scanning) {
        // The count describes the scan being shown, not the one before it.
        setIndexedCount(0);
    }
    emit scanningChanged();
}

int LibraryActivity::indexedCount() const
{
    return m_indexedCount;
}

void LibraryActivity::setIndexedCount(int count)
{
    if (count == m_indexedCount) {
        return;
    }
    m_indexedCount = count;
    emit indexedCountChanged();
}

} // namespace pimio::app
