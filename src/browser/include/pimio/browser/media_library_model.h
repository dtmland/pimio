#pragma once

#include "pimio/core/media_request.h"
#include "pimio/core/durable_store.h"
#include "pimio/core/metadata.h"
#include "pimio/core/types.h"
#include "pimio/projection/projection_database.h"

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QList>
#include <QSize>

#include <optional>

namespace pimio::browser {

class ThumbnailImageProvider;

/// A QAbstractListModel that exposes a chronologically ordered library view.
///
/// Each row represents one media item from a ProjectionDatabase. Thumbnails
/// are loaded lazily through a MediaRequestService: the model requests
/// thumbnails for the visible range plus a configurable prefetch margin, and
/// cancels requests that fall outside that window when the visible range
/// changes.
///
/// The model is backed by a flat list of MediaIds loaded at reload() time.
/// Detailed record data (path, capture time) is loaded from the projection
/// on first access and cached in memory. This keeps reload() fast even for
/// large libraries while staying responsive during scrolling.
class MediaLibraryModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /// The custom roles exposed by this model.
    enum Role {
        MediaIdRole = Qt::UserRole + 1, ///< QString: the item's stable id.
        AbsolutePathRole,               ///< QString: absolute filesystem path.
        CaptureTimeStringRole,          ///< QString: ISO-8601 wall-clock time, or empty.
        MediaKindRole,                  ///< int: core::MediaKind cast to int.
        ThumbnailStatusRole,            ///< int: ThumbnailStatus cast to int.
        ThumbnailImageRole,             ///< QImage: the thumbnail, or a null QImage.
    };
    Q_ENUM(Role)

    /// Lifecycle of a row's thumbnail.
    enum class ThumbnailStatus {
        Pending = 0, ///< Not yet requested.
        Loading,     ///< Request in flight.
        Ready,       ///< Thumbnail is available.
        Error,       ///< Rendering failed; placeholder should be shown.
    };
    Q_ENUM(ThumbnailStatus)

    explicit MediaLibraryModel(QObject *parent = nullptr);
    ~MediaLibraryModel() override;

    /// Attaches the model to a projection database. Clears the current data.
    void setDatabase(projection::ProjectionDatabase *db);
    void setDurableStore(core::DurableStore *store);

    /// Attaches the thumbnail request service. May be null (thumbnails stay
    /// in Pending state until a service is provided).
    void setRequestService(core::MediaRequestService *service);

    /// Attaches a QQuickImageProvider so QML's `image://thumbnail/<mediaId>`
    /// URLs can serve the same bytes this model already decoded, without the
    /// provider needing its own copy of the request pipeline. May be null
    /// (the default); not owned by the model.
    void setImageProvider(ThumbnailImageProvider *provider);

    /// Size of requested thumbnails in pixels. Default: 256×256.
    ///
    /// Changing the size invalidates every thumbnail already held: the cache
    /// key includes the requested size, so a new size is a different render.
    /// Rows go back to Pending and the visible window is requested again.
    void setThumbnailSize(const QSize &size);
    QSize thumbnailSize() const;

    /// The thumbnail sizes the model will ask for, smallest first.
    ///
    /// Thumbnails are rendered at a few fixed sizes rather than at whatever
    /// the tile happens to be, so that dragging a tile-size slider re-renders
    /// (and re-caches) the library at most a handful of times instead of once
    /// per pixel of slider travel. See
    /// docs/decisions/0003-settings-and-view-controls.md.
    static QList<int> thumbnailTiers();

    /// The tier that covers a tile \a pixels across: the smallest tier that
    /// is at least that big, or the largest tier when the tile exceeds it.
    static QSize thumbnailSizeForTile(int pixels);

    /// Sets the thumbnail size from the tile size the view is drawing, in
    /// physical pixels (tile size in device-independent pixels multiplied by
    /// the device pixel ratio). Quantised by thumbnailSizeForTile(), so a
    /// slider drag that stays inside one tier costs nothing.
    Q_INVOKABLE void setTilePixelSize(int pixels);

    /// Number of rows to prefetch beyond each edge of the visible range.
    /// Default: 20.
    void setPrefetchMargin(int margin);
    int prefetchMargin() const;

    /// Number of decoded thumbnails kept in memory at once. Default: 512.
    ///
    /// The model, not the image provider, owns this bound, because the model
    /// is the only place that can act on it: dropping a thumbnail also puts
    /// its row back to Pending, so the row is requested again when it returns
    /// to the visible window. A provider that quietly evicted images on its
    /// own would leave rows claiming to be Ready with nothing to serve, which
    /// is exactly the "previously loaded tiles turn grey and never come back"
    /// failure this bound exists to prevent.
    ///
    /// The effective bound is never smaller than the window currently being
    /// fetched, so scrolling can never evict what the grid is looking at.
    void setRetainedThumbnailLimit(int limit);
    int retainedThumbnailLimit() const;

    /// Number of decoded thumbnails currently held.
    int retainedThumbnailCount() const;

    /// Field the rows are ordered by, and its direction.
    ///
    /// The order is the projection's, not the model's: the model asks the
    /// database for ids in the requested order rather than sorting rows it
    /// has already loaded, so the order is the same one pagination and any
    /// other consumer of the projection would see. Changing either reloads.
    void setSortKey(projection::ProjectionDatabase::SortKey key);
    projection::ProjectionDatabase::SortKey sortKey() const;

    void setSortOrder(Qt::SortOrder order);
    Qt::SortOrder sortOrder() const;

    /// Sets both at once, reloading only once. \a key is a
    /// ProjectionDatabase::SortKey cast to int, so QML can call it.
    Q_INVOKABLE void setSorting(int key, bool descending);

    /// Reloads the item list from the attached database.
    ///
    /// Call after the projection is rebuilt or the database reference changes.
    /// No-op when no database is attached.
    ///
    /// Rows that survive the reload keep the thumbnail they already have, and
    /// a reload that only inserts new rows without removing or reordering old
    /// ones (which is what a scan in progress normally does) is reported as
    /// insertions rather than a model reset, so the grid keeps its scroll
    /// position and its pictures while a library is still being indexed.
    void reload();

    /// Sets the currently visible row range.
    ///
    /// The model requests thumbnails for rows in
    /// [first − prefetchMargin, last + prefetchMargin] and cancels all other
    /// in-flight requests. Views should call this whenever the visible window
    /// changes.
    Q_INVOKABLE void setVisibleRange(int first, int last);

    /// Returns the roles used by the detail view for a row, or an empty map.
    Q_INVOKABLE QVariantMap itemAt(int row) const;

    /// Ensures a thumbnail request exists for a selected row.
    Q_INVOKABLE void requestThumbnail(int row);

    /// Drops whatever the model holds for \a row and requests it again.
    ///
    /// The view calls this when an `image://thumbnail/<id>` request fails
    /// even though the row says Ready: the model and the provider have
    /// disagreed about what is available, and re-rendering the row is the
    /// only thing that can turn a grey tile back into a picture. A row whose
    /// request is already in flight is left alone.
    Q_INVOKABLE void refreshThumbnail(int row);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Item
    {
        core::MediaId id;
        mutable std::optional<core::MediaRecord> record; // loaded on first access
        mutable ThumbnailStatus thumbnailStatus = ThumbnailStatus::Pending;
        mutable QImage thumbnailImage;
        mutable core::MediaRequestHandle thumbnailHandle;
        mutable QString thumbnailRequestKey;
    };

    const core::MediaRecord *ensureRecord(int row) const;
    void requestThumbnailIfNeeded(int row) const;
    /// Drops every loaded thumbnail and re-requests the ones that were
    /// already on screen. Used when the requested size changes.
    void invalidateThumbnails();
    /// Records \a row as the most recently loaded thumbnail and drops the
    /// least recently loaded ones beyond the retention bound.
    void retainThumbnail(int row);
    /// Drops the thumbnail held for \a mediaId (image, provider entry, and
    /// Ready status), so the row is requested again when it is next needed.
    void releaseThumbnail(const QString &mediaId);
    /// Rebuilds the media-id → row lookup after the row list changes.
    void rebuildRowIndex();
    /// Retention bound for the current window: never smaller than the range
    /// setVisibleRange() last asked for, plus its prefetch margins.
    int effectiveRetentionLimit() const;
    /// Keeps the provider's cache large enough to hold everything the model
    /// retains, so the provider never evicts behind the model's back.
    void syncImageProviderCapacity();
    void onThumbnailResult(const core::MediaRequest &request, const core::MediaResult &result);
    void onThumbnailError(const core::MediaRequest &request, const core::Error &error);

    projection::ProjectionDatabase *m_db = nullptr;
    core::DurableStore *m_store = nullptr;
    core::MediaRequestService *m_service = nullptr;
    ThumbnailImageProvider *m_imageProvider = nullptr;
    QSize m_thumbnailSize{256, 256};
    int m_prefetchMargin = 20;
    int m_retainedLimit = 512;
    projection::ProjectionDatabase::SortKey m_sortKey =
            projection::ProjectionDatabase::SortKey::CaptureTime;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    QList<Item> m_items;
    // Media ids of the rows whose thumbnails are held, most recently loaded
    // first. The tail is what gets dropped when the bound is reached.
    QList<QString> m_retainedIds;
    // Media id → row, so a retained id can be found after the row list has
    // been rebuilt by reload().
    QHash<QString, int> m_rowById;
    // Last window setVisibleRange() was told about, so a thumbnail-size
    // change can re-request exactly what the user is looking at.
    int m_visibleFirst = -1;
    int m_visibleLast = -1;
    // Maps request cache key to row index for fast callback lookup.
    mutable QHash<QString, int> m_requestIndex;
};

} // namespace pimio::browser
