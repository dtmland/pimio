#pragma once

#include "pimio/core/edit_recipe.h"
#include "pimio/core/error.h"
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

    bool operator==(const Checkpoint &other) const = default;
};

/// Everything durably stored about one media item.
///
/// The SQLite projection is rebuildable from these records; the durable store
/// is the ground truth.
struct MediaRecord
{
    MediaId id;
    ContentFingerprint fingerprint;
    FileIdentity identity;
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

    /// Stages a record without publishing it. Staged changes survive in the
    /// working area but are not part of history until commit().
    virtual bool stage(const MediaRecord &record, Error *error) = 0;

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

    virtual std::optional<MediaRecord> load(const MediaId &id, Error *error) const = 0;
    virtual QList<MediaId> listIds(Error *error) const = 0;

    virtual QList<Checkpoint> history(int limit, Error *error) const = 0;

    /// Opaque token identifying the current committed state. It changes
    /// whenever the repository is modified, including by an external tool, so
    /// the SQLite projection can detect that it must be rebuilt.
    virtual QString stateToken() const = 0;
};

} // namespace pimio::core
