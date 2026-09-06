#include "pimio/app/library_manager.h"

#include "library_manager_storage.h"

#ifdef PIMIO_HAVE_LORE
#include "pimio/lore/lore_durable_store.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <cstdio>

namespace pimio::app {
using namespace library_manager_storage;

LibraryManager::LibraryManager(const QString &registryPath, QObject *parent)
    : QAbstractListModel(parent)
    , m_registryPath(registryPath.isEmpty() ? defaultRegistryPath() : registryPath)
{
    loadRegistry();
}

LibraryManager::~LibraryManager() = default;

int LibraryManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_libraries.size();
}

QVariant LibraryManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_libraries.size()) {
        return {};
    }
    const LibraryInfo &library = m_libraries.at(index.row());
    switch (role) {
    case IdRole:
        return library.id;
    case NameRole:
        return library.name;
    case LocationRole:
        return library.location;
    case AvailableRole:
        return QFileInfo::exists(storePathFor(library.location));
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryManager::roleNames() const
{
    return {{IdRole, "libraryId"}, {NameRole, "name"}, {LocationRole, "location"},
            {AvailableRole, "available"}};
}

QList<LibraryInfo> LibraryManager::libraries() const
{
    return m_libraries;
}

std::optional<LibraryInfo> LibraryManager::create(const QString &name, const QString &location,
                                                  core::Error *error)
{
#ifndef PIMIO_HAVE_LORE
    Q_UNUSED(name)
    Q_UNUSED(location)
    assignError(error, core::ErrorCode::StorageUnavailable,
                tr("This build does not include Library storage support."));
    return std::nullopt;
#else
    const QString trimmedName = name.trimmed();
    const QString normalized = normalizedLocation(location);
    if (trimmedName.isEmpty() || location.trimmed().isEmpty()) {
        assignError(error, core::ErrorCode::Conflict,
                    tr("A Library needs a name and location."));
        return std::nullopt;
    }
    QDir destination(normalized);
    if (destination.exists() && !destination.isEmpty()) {
        assignError(error, core::ErrorCode::Conflict,
                    tr("The Library location must be empty."));
        return std::nullopt;
    }
    if (!QDir().mkpath(storePathFor(normalized))) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    tr("Could not create the Library location."));
        return std::nullopt;
    }
    lore::LoreDurableStore store(storePathFor(normalized));
    if (!store.open(error) || !store.createLibrary(trimmedName, error)) {
        store.close();
        QDir(normalized).removeRecursively();
        return std::nullopt;
    }
    const auto descriptor = store.libraryDescriptor(error);
    store.close();
    if (!descriptor) {
        QDir(normalized).removeRecursively();
        return std::nullopt;
    }
    const LibraryInfo library{descriptor->id, descriptor->name, normalized};
    replaceKnown(library);
    if (!saveRegistry(error)) {
        return std::nullopt;
    }
    select(library.id);
    return library;
#endif
}

std::optional<LibraryInfo> LibraryManager::open(const QString &location, core::Error *error)
{
#ifndef PIMIO_HAVE_LORE
    Q_UNUSED(location)
    assignError(error, core::ErrorCode::StorageUnavailable,
                tr("This build does not include Library storage support."));
    return std::nullopt;
#else
    auto library = inspectLibrary(location, error);
    if (!library) {
        return std::nullopt;
    }
    replaceKnown(*library);
    if (!saveRegistry(error)) {
        return std::nullopt;
    }
    select(library->id);
    return library;
#endif
}

bool LibraryManager::close(core::Error *)
{
    select({});
    return true;
}

bool LibraryManager::rename(const QString &id, const QString &name, core::Error *error)
{
    std::fprintf(stderr, "library-manager rename: entered\n");
    std::fflush(stderr);
#ifndef PIMIO_HAVE_LORE
    Q_UNUSED(id)
    Q_UNUSED(name)
    assignError(error, core::ErrorCode::StorageUnavailable, tr("Library storage is unavailable."));
    return false;
#else
    auto library = find(id);
    std::fprintf(stderr, "library-manager rename: found library\n");
    std::fflush(stderr);
    if (!library) {
        assignError(error, core::ErrorCode::NotFound, tr("The Library is not known."));
        return false;
    }
    lore::LoreDurableStore store(storePathFor(library->location));
    std::fprintf(stderr, "library-manager rename: opening store\n");
    std::fflush(stderr);
    if (!store.open(error) || !store.renameLibrary(name, error)) {
        return false;
    }
    std::fprintf(stderr, "library-manager rename: reading descriptor\n");
    std::fflush(stderr);
    const auto descriptor = store.libraryDescriptor(error);
    std::fprintf(stderr, "library-manager rename: closing store\n");
    std::fflush(stderr);
    store.close();
    if (!descriptor) {
        return false;
    }
    library->name = descriptor->name;
    replaceKnown(*library);
    return saveRegistry(error);
#endif
}

bool LibraryManager::move(const QString &id, const QString &location, core::Error *error)
{
    auto library = find(id);
    if (!library) {
        assignError(error, core::ErrorCode::NotFound, tr("The Library is not known."));
        return false;
    }
    const QString destination = normalizedLocation(location);
    if (destination == library->location) {
        return true;
    }
    const Qt::CaseSensitivity pathCase =
#ifdef Q_OS_WIN
            Qt::CaseInsensitive;
#else
            Qt::CaseSensitive;
#endif
    const QString sourcePrefix =
            QDir::cleanPath(library->location) + QDir::separator();
    if (destination.startsWith(sourcePrefix, pathCase)) {
        assignError(error, core::ErrorCode::Conflict,
                    tr("A Library cannot be moved inside itself."));
        return false;
    }
    if (QFileInfo::exists(destination)) {
        assignError(error, core::ErrorCode::Conflict,
                    tr("The destination already exists."));
        return false;
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QDir sourceParent(QFileInfo(library->location).absolutePath());
    const bool renamed = sourceParent.rename(QFileInfo(library->location).fileName(), destination);
    if (!renamed) {
        if (!copyTree(library->location, destination, error)) {
            QDir(destination).removeRecursively();
            return false;
        }
#ifdef PIMIO_HAVE_LORE
        const auto copied = inspectLibrary(destination, error);
        if (!copied || copied->id != id) {
            QDir(destination).removeRecursively();
            assignError(error, core::ErrorCode::CorruptData,
                        tr("The copied Library did not preserve its identity."));
            return false;
        }
#endif
        if (!QDir(library->location).removeRecursively()) {
            library->location = destination;
            replaceKnown(*library);
            return saveRegistry(error);
        }
    }
    library->location = destination;
    replaceKnown(*library);
    return saveRegistry(error);
}

bool LibraryManager::backup(const QString &id, const QString &archivePath, core::Error *error)
{
#ifndef PIMIO_HAVE_LORE
    Q_UNUSED(id)
    Q_UNUSED(archivePath)
    assignError(error, core::ErrorCode::StorageUnavailable, tr("Library storage is unavailable."));
    return false;
#else
    const auto library = find(id);
    if (!library) {
        assignError(error, core::ErrorCode::NotFound, tr("The Library is not known."));
        return false;
    }
    const QString normalizedArchive = normalizedLocation(archivePath);
    const QString libraryPrefix = QDir::cleanPath(library->location) + QDir::separator();
    if (normalizedArchive.startsWith(libraryPrefix,
#ifdef Q_OS_WIN
                                     Qt::CaseInsensitive
#else
                                     Qt::CaseSensitive
#endif
                                     )) {
        assignError(error, core::ErrorCode::Conflict,
                    tr("A backup archive must be stored outside its Library."));
        return false;
    }
    lore::LoreDurableStore store(storePathFor(library->location));
    if (!store.open(error)) {
        return false;
    }
    const bool written = writeArchive(library->location, normalizedArchive, error);
    store.close();
    return written;
#endif
}

std::optional<LibraryInfo> LibraryManager::restore(const QString &archivePath,
                                                   const QString &location,
                                                   core::Error *error)
{
#ifndef PIMIO_HAVE_LORE
    Q_UNUSED(archivePath)
    Q_UNUSED(location)
    assignError(error, core::ErrorCode::StorageUnavailable, tr("Library storage is unavailable."));
    return std::nullopt;
#else
    const QString destination = normalizedLocation(location);
    if (QFileInfo::exists(destination)) {
        assignError(error, core::ErrorCode::Conflict,
                    tr("The restore destination already exists."));
        return std::nullopt;
    }
    const QString temporary = destination + QStringLiteral(".restore-")
                              + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!extractArchive(normalizedLocation(archivePath), temporary, error)) {
        QDir(temporary).removeRecursively();
        return std::nullopt;
    }

    lore::LoreDurableStore store(storePathFor(temporary));
    if (!store.open(error) || !store.restoreFromDurableState(error)) {
        store.close();
        QDir(temporary).removeRecursively();
        return std::nullopt;
    }
    const auto descriptor = store.libraryDescriptor(error);
    store.close();
    if (!descriptor) {
        QDir(temporary).removeRecursively();
        return std::nullopt;
    }
    QDir parent(QFileInfo(temporary).absolutePath());
    if (!QDir().mkpath(parent.absolutePath())
        || !parent.rename(QFileInfo(temporary).fileName(), destination)) {
        QDir(temporary).removeRecursively();
        assignError(error, core::ErrorCode::PermissionDenied,
                    tr("Could not place the restored Library at its destination."));
        return std::nullopt;
    }
    LibraryInfo library{descriptor->id, descriptor->name, destination};
    if (!rebuildDerivedState(library, error)) {
        QDir(destination).removeRecursively();
        return std::nullopt;
    }
    replaceKnown(library);
    if (!saveRegistry(error)) {
        return std::nullopt;
    }
    select(library.id);
    return library;
#endif
}

QString LibraryManager::currentLibraryId() const
{
    return m_currentId;
}

QString LibraryManager::currentLibraryName() const
{
    const auto library = find(m_currentId);
    return library ? library->name : QString();
}

QString LibraryManager::currentLibraryLocation() const
{
    const auto library = find(m_currentId);
    return library ? library->location : QString();
}

QString LibraryManager::lastError() const
{
    return m_lastError;
}

void LibraryManager::select(const QString &id)
{
    if (m_currentId == id) {
        return;
    }
    m_currentId = id;
    emit currentLibraryChanged();
}

void LibraryManager::updateKnown(const LibraryInfo &library)
{
    replaceKnown(library);
    core::Error error;
    if (!saveRegistry(&error)) {
        setError(error);
    }
}

QString LibraryManager::locationAt(int row) const
{
    return row >= 0 && row < m_libraries.size() ? m_libraries.at(row).location : QString();
}

QString LibraryManager::defaultRegistryPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
            .filePath(QStringLiteral("libraries.json"));
}

std::optional<LibraryInfo> LibraryManager::find(const QString &id) const
{
    for (const LibraryInfo &library : m_libraries) {
        if (library.id == id) {
            return library;
        }
    }
    return std::nullopt;
}

void LibraryManager::setError(const core::Error &error)
{
    if (m_lastError == error.message()) {
        return;
    }
    m_lastError = error.message();
    emit lastErrorChanged();
}

bool LibraryManager::loadRegistry()
{
    QFile file(m_registryPath);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(core::Error(core::ErrorCode::PermissionDenied,
                             tr("Could not read the Library registry.")));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(core::Error(core::ErrorCode::CorruptData,
                             tr("The Library registry is invalid.")));
        return false;
    }
    beginResetModel();
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        LibraryInfo library{object.value(QStringLiteral("id")).toString(),
                            object.value(QStringLiteral("name")).toString(),
                            object.value(QStringLiteral("location")).toString()};
        if (!library.id.isEmpty() && !library.name.isEmpty() && !library.location.isEmpty()) {
            m_libraries.append(library);
        }
    }
    endResetModel();
    return true;
}

bool LibraryManager::saveRegistry(core::Error *error) const
{
    QJsonArray array;
    for (const LibraryInfo &library : m_libraries) {
        array.append(QJsonObject{{QStringLiteral("id"), library.id},
                                 {QStringLiteral("name"), library.name},
                                 {QStringLiteral("location"), library.location}});
    }
    if (!QDir().mkpath(QFileInfo(m_registryPath).absolutePath())) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    tr("Could not create the Library registry directory."));
        return false;
    }
    QSaveFile file(m_registryPath);
    const QByteArray bytes = QJsonDocument(array).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    tr("Could not save the Library registry."));
        return false;
    }
    return true;
}

void LibraryManager::replaceKnown(const LibraryInfo &library)
{
    for (int row = 0; row < m_libraries.size(); ++row) {
        if (m_libraries.at(row).id == library.id) {
            m_libraries[row] = library;
            emit dataChanged(index(row), index(row));
            if (m_currentId == library.id) {
                emit currentLibraryChanged();
            }
            return;
        }
    }
    const int row = m_libraries.size();
    beginInsertRows({}, row, row);
    m_libraries.append(library);
    endInsertRows();
}

} // namespace pimio::app
