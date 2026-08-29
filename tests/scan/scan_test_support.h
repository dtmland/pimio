#pragma once

#include "pimio/core/types.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/memory_file_system.h"

namespace pimio::tests::scan_support {

inline const QString kRoot = QStringLiteral("/library");
inline const QDateTime kT0 = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

inline testing::FakeClock makeClock()
{
    return testing::FakeClock(kT0);
}

inline QString addFile(testing::MemoryFileSystem &fs, const QString &path,
                       const QByteArray &contents = "test-content",
                       const QDateTime &modified = kT0)
{
    fs.addFile(path, contents, modified);
    return path;
}

inline QList<core::MediaRecord> loadAll(testing::MemoryDurableStore &store)
{
    core::Error err;
    const QList<core::MediaId> ids = store.listIds(&err);
    Q_ASSERT(!err.isError());
    QList<core::MediaRecord> records;
    for (const core::MediaId &id : ids) {
        core::Error loadErr;
        auto rec = store.load(id, &loadErr);
        Q_ASSERT(rec.has_value());
        records.append(*rec);
    }
    return records;
}

} // namespace pimio::tests::scan_support
