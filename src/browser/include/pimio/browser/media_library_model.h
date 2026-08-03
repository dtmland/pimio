#pragma once

#include "pimio/core/media_request.h"
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

    /// Attaches the thumbnail request service. May be null (thumbnails stay
    /// in Pending state until a service is provided).
    void setRequestService(core::MediaRequestService *service);

    /// Attaches a QQuickImageProvider so QML's `image://thumbnail/<mediaId>`
    /// URLs can serve the same bytes this model already decoded, without the
    /// provider needing its own copy of the request pipeline. May be null
    /// (the default); not owned by the model.
    void setImageProvider(ThumbnailImageProvider *provider);

    /// Size of requested thumbnails in device-independent pixels. Default: 160×160.
    void setThumbnailSize(const QSize &size);
    QSize thumbnailSize() const;

    /// Number of rows to prefetch beyond each edge of the visible range.
    /// Default: 20.
    void setPrefetchMargin(int margin);
    int prefetchMargin() const;

    /// Reloads the item list from the attached database.
    ///
    /// Call after the projection is rebuilt or the database reference changes.
    /// No-op when no database is attached.
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
    void onThumbnailResult(const core::MediaRequest &request, const core::MediaResult &result);
    void onThumbnailError(const core::MediaRequest &request, const core::Error &error);

    projection::ProjectionDatabase *m_db = nullptr;
    core::MediaRequestService *m_service = nullptr;
    ThumbnailImageProvider *m_imageProvider = nullptr;
    QSize m_thumbnailSize{160, 160};
    int m_prefetchMargin = 20;

    QList<Item> m_items;
    // Maps request cache key to row index for fast callback lookup.
    mutable QHash<QString, int> m_requestIndex;
};

} // namespace pimio::browser
