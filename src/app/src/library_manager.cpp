#include "pimio/app/library_manager.h"

#include "pimio/projection/projection_database.h"

#ifdef PIMIO_HAVE_LORE
#include "pimio/lore/lore_durable_store.h"
#endif

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <limits>

namespace pimio::app {
namespace {

constexpr quint32 kArchiveMagic = 0x50494d42; // PIMB
constexpr quint32 kArchiveVersion = 1;
constexpr quint32 kMaximumArchiveEntries = 1000000;

void assignError(core::Error *target, core::ErrorCode code, const QString &message)
{
    if (target) {
        *target = core::Error(code, message);
    }
}

QString normalizedLocation(const QString &path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString storePathFor(const QString &location)
{
    return QDir(location).filePath(QStringLiteral("store"));
}

bool copyTree(const QString &source, const QString &destination, core::Error *error)
{
    if (!QDir().mkpath(destination)) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    QObject::tr("Could not create %1.").arg(destination));
        return false;
    }
    QDirIterator iterator(source, QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = QDir(source).relativeFilePath(sourcePath);
        if (relative == QStringLiteral("store/.pimio-writer.lock")) {
            continue;
        }
        const QString targetPath = QDir(destination).filePath(relative);
        if (info.isSymLink()) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("A Library cannot contain symbolic links: %1").arg(relative));
            return false;
        }
        if (info.isDir()) {
            if (!QDir().mkpath(targetPath)) {
                assignError(error, core::ErrorCode::PermissionDenied,
                            QObject::tr("Could not create %1.").arg(targetPath));
                return false;
            }
        } else if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())
                   || !QFile::copy(sourcePath, targetPath)) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not copy %1.").arg(relative));
            return false;
        }
    }
    return true;
}

QStringList archiveFiles(const QString &location, core::Error *error)
{
    const QString storePath = storePathFor(location);
    QStringList files;
    QDirIterator iterator(storePath, QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink()) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("A Library cannot contain symbolic links."));
            return {};
        }
        const QString relative = QDir(location).relativeFilePath(path);
        if (relative != QStringLiteral("store/.pimio-writer.lock")) {
            files.append(relative);
        }
    }
    files.sort();
    return files;
}

bool writeArchive(const QString &location, const QString &archivePath, core::Error *error)
{
    const QStringList files = archiveFiles(location, error);
    if (files.isEmpty() && error && error->isError()) {
        return false;
    }
    if (files.size() > static_cast<qsizetype>(kMaximumArchiveEntries)) {
        assignError(error, core::ErrorCode::Conflict,
                    QObject::tr("The Library contains too many files to archive."));
        return false;
    }

    QSaveFile output(archivePath);
    if (!QDir().mkpath(QFileInfo(archivePath).absolutePath())
        || !output.open(QIODevice::WriteOnly)) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    QObject::tr("Could not create the backup archive."));
        return false;
    }
    QDataStream stream(&output);
    stream.setVersion(QDataStream::Qt_6_4);
    stream << kArchiveMagic << kArchiveVersion << static_cast<quint32>(files.size());

    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (const QString &relative : files) {
        QFile input(QDir(location).filePath(relative));
        if (!input.open(QIODevice::ReadOnly)) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not read %1 for backup.").arg(relative));
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!input.atEnd()) {
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count < 0) {
                assignError(error, core::ErrorCode::Interrupted,
                            QObject::tr("Could not read %1 for backup.").arg(relative));
                return false;
            }
            hash.addData(QByteArrayView(buffer.constData(), count));
        }
        stream << relative << static_cast<quint64>(input.size()) << hash.result();
        input.seek(0);
        while (!input.atEnd()) {
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count <= 0 || stream.writeRawData(buffer.constData(), static_cast<int>(count))
                                      != count) {
                assignError(error, core::ErrorCode::OutOfSpace,
                            QObject::tr("Could not finish the backup archive."));
                return false;
            }
        }
    }
    if (stream.status() != QDataStream::Ok || !output.commit()) {
        assignError(error, core::ErrorCode::OutOfSpace,
                    QObject::tr("Could not finish the backup archive."));
        return false;
    }
    return true;
}

bool isSafeArchivePath(const QString &relative)
{
    return !relative.isEmpty() && !QDir::isAbsolutePath(relative)
           && QDir::cleanPath(relative) == relative && !relative.startsWith(QStringLiteral("../"))
           && relative.startsWith(QStringLiteral("store/"));
}

bool extractArchive(const QString &archivePath, const QString &destination, core::Error *error)
{
    QFile input(archivePath);
    if (!input.open(QIODevice::ReadOnly)) {
        assignError(error, core::ErrorCode::NotFound,
                    QObject::tr("Could not open the backup archive."));
        return false;
    }
    QDataStream stream(&input);
    stream.setVersion(QDataStream::Qt_6_4);
    quint32 magic = 0;
    quint32 version = 0;
    quint32 count = 0;
    stream >> magic >> version >> count;
    if (magic != kArchiveMagic || version != kArchiveVersion
        || count > kMaximumArchiveEntries) {
        assignError(error, core::ErrorCode::CorruptData,
                    QObject::tr("The backup archive is invalid or unsupported."));
        return false;
    }

    QSet<QString> seen;
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (quint32 index = 0; index < count; ++index) {
        QString relative;
        quint64 size = 0;
        QByteArray expectedHash;
        stream >> relative >> size >> expectedHash;
        if (stream.status() != QDataStream::Ok || !isSafeArchivePath(relative)
            || seen.contains(relative) || expectedHash.size() != 32
            || size > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("The backup archive contains an invalid entry."));
            return false;
        }
        seen.insert(relative);
        const QString targetPath = QDir(destination).filePath(relative);
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not create the restore destination."));
            return false;
        }
        QSaveFile output(targetPath);
        if (!output.open(QIODevice::WriteOnly)) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not restore %1.").arg(relative));
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        quint64 remaining = size;
        while (remaining > 0) {
            const int requested = static_cast<int>(
                    std::min<quint64>(remaining, static_cast<quint64>(buffer.size())));
            const int read = stream.readRawData(buffer.data(), requested);
            if (read != requested || output.write(buffer.constData(), read) != read) {
                assignError(error, core::ErrorCode::CorruptData,
                            QObject::tr("The backup archive ended unexpectedly."));
                return false;
            }
            hash.addData(QByteArrayView(buffer.constData(), read));
            remaining -= static_cast<quint64>(read);
        }
        if (hash.result() != expectedHash || !output.commit()) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("Backup verification failed for %1.").arg(relative));
            return false;
        }
    }
    if (stream.status() != QDataStream::Ok || !input.atEnd()) {
        assignError(error, core::ErrorCode::CorruptData,
                    QObject::tr("The backup archive contains unexpected data."));
        return false;
    }
    return true;
}

QString indexDirectoryFor(const QString &libraryId)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/library-indexes/") + libraryId;
}

QString thumbnailDirectoryFor(const QString &libraryId)
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/libraries/") + libraryId;
}

#ifdef PIMIO_HAVE_LORE
std::optional<LibraryInfo> inspectLibrary(const QString &location, core::Error *error)
{
    const QString normalized = normalizedLocation(location);
    if (!QFileInfo(normalized).isDir() || !QFileInfo(storePathFor(normalized)).isDir()) {
        assignError(error, core::ErrorCode::NotFound,
                    QObject::tr("No Library exists at %1.").arg(normalized));
        return std::nullopt;
    }
    lore::LoreDurableStore store(storePathFor(normalized));
    if (!store.open(error)) {
        return std::nullopt;
    }
    const auto descriptor = store.libraryDescriptor(error);
    store.close();
    if (!descriptor) {
        return std::nullopt;
    }
    return LibraryInfo{descriptor->id, descriptor->name, normalized};
}

bool rebuildDerivedState(const LibraryInfo &library, core::Error *error)
{
    const QString indexPath = indexDirectoryFor(library.id);
    QDir(indexPath).removeRecursively();
    QDir(thumbnailDirectoryFor(library.id)).removeRecursively();
    if (!QDir().mkpath(indexPath)) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    QObject::tr("Could not create derived Library storage."));
        return false;
    }

    lore::LoreDurableStore store(storePathFor(library.location));
    if (!store.open(error)) {
        return false;
    }
    projection::ProjectionDatabase projection;
    if (!projection.open(QDir(indexPath).filePath(QStringLiteral("projection.sqlite3")), error)
        || !projection.rebuildFrom(store, error)) {
        return false;
    }
    store.close();
    return true;
}
#endif

} // namespace

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
#ifndef PIMIO_HAVE_LORE
    Q_UNUSED(id)
    Q_UNUSED(name)
    assignError(error, core::ErrorCode::StorageUnavailable, tr("Library storage is unavailable."));
    return false;
#else
    auto library = find(id);
    if (!library) {
        assignError(error, core::ErrorCode::NotFound, tr("The Library is not known."));
        return false;
    }
    lore::LoreDurableStore store(storePathFor(library->location));
    if (!store.open(error) || !store.renameLibrary(name, error)) {
        return false;
    }
    const auto descriptor = store.libraryDescriptor(error);
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
            QDir(destination).removeRecursively();
            assignError(error, core::ErrorCode::PermissionDenied,
                        tr("Could not remove the old Library after copying it."));
            return false;
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
    lore::LoreDurableStore store(storePathFor(library->location));
    if (!store.open(error)) {
        return false;
    }
    const bool written = writeArchive(library->location, normalizedLocation(archivePath), error);
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
