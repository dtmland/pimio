#pragma once

#include "pimio/core/error.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include <optional>

namespace pimio::app {

struct LibraryInfo
{
    QString id;
    QString name;
    QString location;

    bool operator==(const LibraryInfo &other) const = default;
};

/// Transport-neutral lifecycle boundary consumed by the desktop UI.
///
/// v1 implements these operations in-process. A future client can implement
/// the same request/response-shaped interface over a network without exposing
/// LORE, SQLite, cache paths, or filesystem internals to QML.
class LibraryService
{
public:
    virtual ~LibraryService() = default;

    virtual QList<LibraryInfo> libraries() const = 0;
    virtual std::optional<LibraryInfo> create(const QString &name, const QString &location,
                                              core::Error *error) = 0;
    virtual std::optional<LibraryInfo> open(const QString &location, core::Error *error) = 0;
    virtual bool close(core::Error *error) = 0;
    virtual bool rename(const QString &id, const QString &name, core::Error *error) = 0;
    virtual bool move(const QString &id, const QString &location, core::Error *error) = 0;
    virtual bool backup(const QString &id, const QString &archivePath, core::Error *error) = 0;
    virtual std::optional<LibraryInfo> restore(const QString &archivePath,
                                               const QString &location,
                                               core::Error *error) = 0;
};

/// Persistent list model and local implementation of LibraryService.
class LibraryManager final : public QAbstractListModel, public LibraryService
{
    Q_OBJECT
    Q_PROPERTY(QString currentLibraryId READ currentLibraryId NOTIFY currentLibraryChanged)
    Q_PROPERTY(QString currentLibraryName READ currentLibraryName NOTIFY currentLibraryChanged)
    Q_PROPERTY(QString currentLibraryLocation READ currentLibraryLocation
                       NOTIFY currentLibraryChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        LocationRole,
        AvailableRole,
    };

    explicit LibraryManager(const QString &registryPath = {}, QObject *parent = nullptr);
    ~LibraryManager() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QList<LibraryInfo> libraries() const override;
    std::optional<LibraryInfo> create(const QString &name, const QString &location,
                                      core::Error *error) override;
    std::optional<LibraryInfo> open(const QString &location, core::Error *error) override;
    bool close(core::Error *error) override;
    bool rename(const QString &id, const QString &name, core::Error *error) override;
    bool move(const QString &id, const QString &location, core::Error *error) override;
    bool backup(const QString &id, const QString &archivePath, core::Error *error) override;
    std::optional<LibraryInfo> restore(const QString &archivePath, const QString &location,
                                       core::Error *error) override;

    QString currentLibraryId() const;
    QString currentLibraryName() const;
    QString currentLibraryLocation() const;
    QString lastError() const;

    void select(const QString &id);
    void updateKnown(const LibraryInfo &library);

    Q_INVOKABLE QString locationAt(int row) const;

    static QString defaultRegistryPath();

signals:
    void currentLibraryChanged();
    void lastErrorChanged();

private:
    std::optional<LibraryInfo> find(const QString &id) const;
    void setError(const core::Error &error);
    bool loadRegistry();
    bool saveRegistry(core::Error *error) const;
    void replaceKnown(const LibraryInfo &library);

    QString m_registryPath;
    QList<LibraryInfo> m_libraries;
    QString m_currentId;
    QString m_lastError;
};

} // namespace pimio::app
