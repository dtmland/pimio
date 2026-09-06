#pragma once

#include "pimio/core/edit_recipe.h"
#include "pimio/core/error.h"
#include "pimio/core/library.h"
#include "pimio/core/metadata.h"
#include "pimio/core/types.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace pimio::core {

/// A user-visible save point in the durable store.
struct Checkpoint
{
    QString id;
    QString message;
    QDateTime createdAtUtc;
    QString authorId = QString(kUnknownAuthorId);
    QString applicationVersion;
    QString parentId;
    QJsonObject unrecognizedFields;

    QJsonObject toJson() const;
    static Checkpoint fromJson(const QJsonObject &object);

    bool operator==(const Checkpoint &other) const = default;
};

/// Everything durably stored about one media item.
///
/// The SQLite projection is rebuildable from these records; the durable store
/// is the ground truth.
struct MediaRecord
{
    enum class OriginalStorage {
        Referenced,
        Managed,
    };

    MediaId id;
    ContentFingerprint fingerprint;
    /// Identity of the file at the import source. It is provenance, not the
    /// location consumers should use to read a managed original.
    FileIdentity identity;
    OriginalStorage originalStorage = OriginalStorage::Referenced;
    /// Portable path inside the durable repository checkout.
    QString managedOriginalPath;
    MediaMetadata metadata;
    EditRecipe recipe;

    QJsonObject toJson() const;
    static MediaRecord fromJson(const QJsonObject &object);

    bool operator==(const MediaRecord &other) const = default;
};

/// Durable, versioned storage boundary.
///
/// This is the contract the LORE feasibility gate (Increment 2) must satisfy.
/// Keeping it abstract means a no-go decision changes one adapter rather than
/// the whole application.
class DurableStore
{
public:
    virtual ~DurableStore();

    virtual bool isAvailable() const = 0;

    /// Creates the reserved descriptor record for a fresh repository.
    /// Returns Conflict when the repository already has a descriptor.
    virtual bool createLibrary(const QString &name, Error *error) = 0;

    /// Loads the reserved descriptor record. It is not included in listIds().
    virtual std::optional<LibraryDescriptor> libraryDescriptor(Error *error) const = 0;

    /// Changes only the display name in the reserved descriptor and records the
    /// change in canonical history. The stable library and local-user ids are
    /// preserved.
    virtual bool renameLibrary(const QString &name, Error *error) = 0;

    /// Stages a record without publishing it. Staged changes survive in the
    /// working area but are not part of history until commit().
    virtual bool stage(const MediaRecord &record, Error *error) = 0;

    /// Copies and stages an original together with its record. Neither becomes
    /// committed independently of the other.
    virtual bool stageOriginal(const MediaRecord &record, const QString &sourcePath,
                               Error *error) = 0;

    /// Resolves the path consumers should read. Legacy referenced records remain
    /// readable from their source path and explicitly report Referenced storage.
    virtual QString originalPath(const MediaRecord &record, Error *error) const = 0;

    /// Publishes all staged changes as one checkpoint.
    ///
    /// Must be atomic from a reader's point of view: either every staged
    /// change is committed or none is. A failed commit must leave the staged
    /// changes recoverable rather than lost.
    virtual std::optional<Checkpoint> commit(const QString &message, Error *error) = 0;

    /// Discards staged changes that have not been committed.
    virtual bool discardStaged(Error *error) = 0;

    /// True when there are staged changes that are not yet committed.
    virtual bool hasStagedChanges() const = 0;

    /// Stages the removal of the record identified by \a id. If no record with
    /// that id exists, returns true without error (idempotent).
    virtual bool remove(const MediaId &id, Error *error) = 0;

    virtual std::optional<MediaRecord> load(const MediaId &id, Error *error) const = 0;
    virtual QList<MediaId> listIds(Error *error) const = 0;

    virtual QList<Checkpoint> history(int limit, Error *error) const = 0;

    /// Opaque token identifying the current committed state. It changes
    /// whenever the repository is modified, including by an external tool, so
    /// the SQLite projection can detect that it must be rebuilt.
    virtual QString stateToken() const = 0;
};

} // namespace pimio::core
