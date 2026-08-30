#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QQuickItem>
#include <QStringList>
#include <QVariantMap>

namespace pimio::tests::app_support {

class SyntheticMediaModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        MediaIdRole = Qt::UserRole + 1,
        AbsolutePathRole,
        CaptureTimeStringRole,
        MediaKindRole,
        ThumbnailStatusRole,
        ThumbnailImageRole,
    };

    explicit SyntheticMediaModel(int count, int thumbnailStatus = 0, QString absolutePath = {},
                                  QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setVisibleRange(int first, int last);
    Q_INVOKABLE void requestThumbnail(int);
    Q_INVOKABLE void refreshThumbnail(int row);

    QList<int> refreshedRows() const;
    void prependRows(int count);
    void removeLeadingRows(int count);
    Q_INVOKABLE QVariantMap itemAt(int row) const;

signals:
    void visibleRangeChanged(int first, int last);

private:
    QStringList m_ids;
    int m_thumbnailStatus;
    QString m_absolutePath;
    QList<int> m_refreshedRows;
    int m_nextPrependedId = 0;
};

QQuickItem *findVisualItem(QQuickItem *root, const QString &objectName);

} // namespace pimio::tests::app_support
