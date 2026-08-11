#include "pimio/projection/migration.h"

namespace pimio::projection {

const QList<Migration> &projectionMigrations()
{
    // Nothing here is authoritative. Every column is derived from the record
    // JSON that sits in the same row, and that JSON is itself a copy of what
    // the durable store holds. The columns exist only so SQLite can answer
    // questions without parsing every record.
    static const QList<Migration> migrations{
        Migration{
            1,
            QStringLiteral("initial-schema"),
            QStringList{
                QStringLiteral(R"(
                    CREATE TABLE media (
                        id                    TEXT    PRIMARY KEY NOT NULL,
                        record_json           TEXT    NOT NULL,
                        fingerprint_algorithm TEXT    NOT NULL DEFAULT '',
                        fingerprint_digest    TEXT    NOT NULL DEFAULT '',
                        absolute_path         TEXT    NOT NULL DEFAULT '',
                        volume_id             TEXT    NOT NULL DEFAULT '',
                        file_id               TEXT    NOT NULL DEFAULT '',
                        size_bytes            INTEGER NOT NULL DEFAULT -1,
                        last_modified_ms      INTEGER,
                        kind                  TEXT    NOT NULL DEFAULT 'unknown',
                        file_name             TEXT    NOT NULL DEFAULT '',
                        folder_path           TEXT    NOT NULL DEFAULT '',
                        capture_sort_key      INTEGER NOT NULL DEFAULT 0,
                        capture_has_offset    INTEGER NOT NULL DEFAULT 0,
                        camera_make           TEXT    NOT NULL DEFAULT '',
                        camera_model          TEXT    NOT NULL DEFAULT '',
                        pixel_width           INTEGER NOT NULL DEFAULT 0,
                        pixel_height          INTEGER NOT NULL DEFAULT 0,
                        duration_ms           INTEGER NOT NULL DEFAULT 0,
                        rating                INTEGER NOT NULL DEFAULT 0,
                        caption               TEXT    NOT NULL DEFAULT '',
                        latitude              REAL,
                        longitude             REAL
                    )
                )"),
                QStringLiteral(R"(
                    CREATE TABLE media_tag (
                        media_id TEXT NOT NULL REFERENCES media(id) ON DELETE CASCADE,
                        tag      TEXT NOT NULL,
                        PRIMARY KEY (media_id, tag)
                    )
                )"),
                // Key/value rather than a one-row table so a later migration
                // can add a fact without rewriting the table.
                QStringLiteral(R"(
                    CREATE TABLE projection_meta (
                        key   TEXT PRIMARY KEY NOT NULL,
                        value TEXT NOT NULL
                    )
                )"),
                QStringLiteral(
                    "CREATE INDEX media_fingerprint ON media(fingerprint_digest)"),
                QStringLiteral("CREATE INDEX media_path ON media(absolute_path)"),
                QStringLiteral("CREATE INDEX media_folder ON media(folder_path)"),
                QStringLiteral(
                    "CREATE INDEX media_capture ON media(capture_sort_key, id)"),
                QStringLiteral("CREATE INDEX media_tag_tag ON media_tag(tag)"),
            },
        },
        Migration{
            2,
            QStringLiteral("full-text-search"),
            QStringList{
                // FTS5 virtual table for caption and file-name text search.
                // The unicode61 tokenizer handles accented characters and CJK
                // decomposition. id is UNINDEXED so it is stored but not
                // tokenised; callers join on it to retrieve the media id.
                QStringLiteral(R"(
                    CREATE VIRTUAL TABLE media_fts USING fts5(
                        id     UNINDEXED,
                        caption,
                        file_name,
                        tokenize = 'unicode61'
                    )
                )"),
            },
        },
        Migration{
            3,
            QStringLiteral("browse-sort-columns"),
            QStringList{
                // The extension is what a user means by "file type" in a sort
                // menu, and deriving it in SQL at query time would need a
                // last-index-of that SQLite does not have. Store it instead;
                // like every other column here it is derived from the record
                // JSON in the same row.
                QStringLiteral(
                    "ALTER TABLE media ADD COLUMN file_extension TEXT NOT NULL DEFAULT ''"),
                // One index per sort order the browser offers, each ending in
                // id so the order stays total when the leading column ties.
                QStringLiteral(
                    "CREATE INDEX media_file_name ON media(file_name COLLATE NOCASE, id)"),
                QStringLiteral("CREATE INDEX media_modified ON media(last_modified_ms, id)"),
                QStringLiteral("CREATE INDEX media_size ON media(size_bytes, id)"),
                QStringLiteral(
                    "CREATE INDEX media_extension ON media(file_extension, file_name"
                    " COLLATE NOCASE, id)"),
                // Existing rows predate the new column and would sort as if
                // every file had no extension. Dropping the state token makes
                // the projection stale, which the startup path already
                // recovers from by rebuilding it from the durable store.
                QStringLiteral(
                    "DELETE FROM projection_meta WHERE key = 'durableStateToken'"),
            },
        },
    };
    return migrations;
}

} // namespace pimio::projection

// ---- Job queue schema -------------------------------------------------------

namespace pimio::projection {

const QList<Migration> &jobQueueMigrations()
{
    static const QList<Migration> migrations{
        Migration{
            1,
            QStringLiteral("create-job-table"),
            QStringList{
                QStringLiteral(R"(
                    CREATE TABLE job (
                        id             TEXT    PRIMARY KEY NOT NULL,
                        kind           TEXT    NOT NULL DEFAULT 'unknown',
                        priority       INTEGER NOT NULL DEFAULT 2,
                        state          TEXT    NOT NULL DEFAULT 'pending',
                        coalescing_key TEXT    NOT NULL DEFAULT '',
                        payload        TEXT    NOT NULL DEFAULT '{}',
                        attempts       INTEGER NOT NULL DEFAULT 0,
                        max_attempts   INTEGER NOT NULL DEFAULT 3,
                        created_at_ms  INTEGER,
                        not_before_ms  INTEGER,
                        last_error     TEXT    NOT NULL DEFAULT '{}'
                    )
                )"),
                QStringLiteral(
                    "CREATE INDEX job_claimable ON job(priority ASC, created_at_ms ASC, id ASC)"
                    " WHERE state = 'pending'"),
            },
        },
    };
    return migrations;
}

} // namespace pimio::projection
