#pragma once

#include "pimio/core/durable_store.h"
#include "pimio/core/error.h"
#include "pimio/core/types.h"
#include "pimio/projection/migration.h"

#include <QList>
#include <QString>
#include <QtCore/qnamespace.h>

#include <memory>
#include <optional>

namespace pimio::projection {

/// A query-shaped view of the library, cached in SQLite.
///
/// The durable store is the ground truth. This database holds nothing that
/// cannot be recomputed from it, so deleting the file is always safe and never
/// loses user data. Everything here follows from that: there is no write path
/// for user edits, only a rebuild from durable state.
class ProjectionDatabase
{
public:
    ProjectionDatabase();
    ~ProjectionDatabase();

    ProjectionDatabase(const ProjectionDatabase &) = delete;
    ProjectionDatabase &operator=(const ProjectionDatabase &) = delete;

    /// Opens (creating if needed) the database at \a path and migrates it to
    /// the current schema version.
    ///
    /// Fails with CorruptData when the file is not a usable database, and with
    /// Conflict when it was written by a newer schema than this build knows.
    /// Both are recoverable by deleting the file and rebuilding, which is the
    /// caller's decision to make, not this class's.
    bool open(const QString &path, core::Error *error);

    /// Opens a private in-memory database. Used by tests and by callers that
    /// want a projection without a file.
    bool openInMemory(core::Error *error);

    /// Deletes the projection at \a path, including its write-ahead log and
    /// shared-memory files.
    ///
    /// This is the supported recovery for a damaged or unreadable cache, and
    /// it is safe by construction: the durable store holds everything this
    /// file contained.
    static bool remove(const QString &path, core::Error *error);

    void close();
    bool isOpen() const;

    const QString &path() const;

    /// Schema version currently stored in the database.
    int schemaVersion() const;

    /// Durable state token the projection was last rebuilt from. Empty when
    /// the projection has never been populated.
    QString projectedStateToken(core::Error *error) const;

    /// True when the projection does not match the store's current state and
    /// must be rebuilt before it is trusted.
    bool isStale(const core::DurableStore &store, core::Error *error) const;

    /// Replaces the projection's contents with the store's committed state.
    ///
    /// Runs as one transaction: a failure part-way leaves the previous
    /// contents in place rather than a half-built index. The store's state
    /// token is recorded in the same transaction, so a projection can never
    /// claim to be current for data it does not hold.
    bool rebuildFrom(const core::DurableStore &store, core::Error *error);

    // Queries. These are what the projection exists for; they must return
    // exactly what the same question asked of the durable store would.

    qsizetype recordCount(core::Error *error) const;
    QList<core::MediaId> listIds(core::Error *error) const;
    std::optional<core::MediaRecord> load(const core::MediaId &id, core::Error *error) const;

    /// Ids sharing a content fingerprint: the duplicate and moved-file query.
    QList<core::MediaId> idsWithFingerprint(const core::ContentFingerprint &fingerprint,
                                            core::Error *error) const;

    QList<core::MediaId> idsWithTag(const QString &tag, core::Error *error) const;

    /// Ids ordered by capture time, oldest first, then by id so the order is
    /// total even when timestamps collide.
    QList<core::MediaId> idsByCaptureTime(core::Error *error) const;

    /// Field a browse query is ordered by.
    ///
    /// Every order ends in the media id, so the result is total: two files
    /// with the same name, size, timestamp, or extension always come back in
    /// the same relative order, and a paginated query cannot repeat or skip a
    /// row because SQLite chose a different tie-break this time.
    enum class SortKey {
        CaptureTime = 0, ///< Metadata capture time.
        FileName,        ///< File name, case-insensitive.
        FileDate,        ///< Filesystem last-modified time.
        FileType,        ///< File extension, then file name.
        FileSize,        ///< Size in bytes.
    };

    /// Ids ordered by \a key in \a order.
    ///
    /// Descending reverses the leading column only; the id tie-break stays
    /// ascending so that reversing the sort does not reshuffle equal rows
    /// relative to each other.
    QList<core::MediaId> idsSorted(SortKey key, Qt::SortOrder order,
                                   core::Error *error) const;

    /// Paginated capture-time query. \a offset is the number of records to
    /// skip; \a limit is the maximum number to return. \a limit < 0 returns
    /// all remaining records from \a offset.
    QList<core::MediaId> idsByCaptureTime(int offset, int limit, core::Error *error) const;

    /// Ids whose kind matches \a kind, ordered chronologically.
    QList<core::MediaId> idsWithKind(core::MediaKind kind, core::Error *error) const;

    /// Ids with rating >= \a minRating, ordered chronologically.
    QList<core::MediaId> idsWithMinimumRating(int minRating, core::Error *error) const;

    /// Full-text search over caption and file name. Results are returned in
    /// relevance order (best match first). An empty query returns an empty list.
    ///
    /// The text is treated as literal terms, never as FTS5 syntax, so any
    /// character the user can type is searchable rather than an error. Terms
    /// match on prefix and all of them must match, so "gold gat" finds a
    /// "Golden Gate" caption and a leading substring finds an unbroken CJK run.
    QList<core::MediaId> searchText(const QString &query, core::Error *error) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::projection
