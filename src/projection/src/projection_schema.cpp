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
    };
    return migrations;
}

} // namespace pimio::projection
