#include "lore_api.h"

#include "pimio/lore/lore_durable_store.h"

#include <QFileInfo>
#include <QLibrary>

namespace pimio::lore {
namespace {

// Recorded by the build from the acquired artifact. See cmake/PimioLore.cmake.
#ifndef PIMIO_LORE_SHARED_LIBRARY
#define PIMIO_LORE_SHARED_LIBRARY ""
#endif

} // namespace

QString defaultLibraryPath()
{
    const QByteArray fromEnvironment = qgetenv("PIMIO_LORE_LIBRARY");
    if (!fromEnvironment.isEmpty()) {
        return QString::fromLocal8Bit(fromEnvironment);
    }
    return QString::fromUtf8(PIMIO_LORE_SHARED_LIBRARY);
}

QString loadedLibraryVersion()
{
    return LoreApi::instance().isLoaded() ? LoreApi::headerVersion() : QString();
}

QString LoreApi::headerVersion()
{
    return QString::fromUtf8(LORE_INTERFACE_VERSION);
}

LoreApi &LoreApi::instance()
{
    static LoreApi api;
    return api;
}

LoreApi::LoreApi()
{
    m_libraryPath = defaultLibraryPath();
    if (m_libraryPath.isEmpty()) {
        m_loadError = QStringLiteral("No LORE library path was recorded by the build.");
        return;
    }
    if (!QFileInfo::exists(m_libraryPath)) {
        m_loadError = QStringLiteral("The LORE library %1 does not exist.").arg(m_libraryPath);
        return;
    }

    auto *library = new QLibrary(m_libraryPath);
    if (!library->load()) {
        m_loadError = QStringLiteral("The LORE library %1 could not be loaded: %2")
                          .arg(m_libraryPath, library->errorString());
        delete library;
        return;
    }

    // Deliberately never unloaded: LORE owns worker threads whose lifetime
    // outlives individual calls, so unloading it during shutdown would be a
    // use-after-unload risk for no benefit.
    const auto resolve = [library, this](const char *name) -> QFunctionPointer {
        QFunctionPointer symbol = library->resolve(name);
        if (symbol == nullptr && m_loadError.isEmpty()) {
            m_loadError = QStringLiteral("The LORE library %1 does not export %2. "
                                         "Expected interface version %3.")
                              .arg(m_libraryPath, QString::fromUtf8(name), headerVersion());
        }
        return symbol;
    };

    repositoryCreate = reinterpret_cast<RepositoryCreateFn>(resolve("lore_repository_create"));
    repositoryStatus = reinterpret_cast<RepositoryStatusFn>(resolve("lore_repository_status"));
    repositoryRelease = reinterpret_cast<RepositoryReleaseFn>(resolve("lore_repository_release"));
    repositoryFlush = reinterpret_cast<RepositoryFlushFn>(resolve("lore_repository_flush"));
    fileStage = reinterpret_cast<FileStageFn>(resolve("lore_file_stage"));
    fileUnstage = reinterpret_cast<FileUnstageFn>(resolve("lore_file_unstage"));
    fileReset = reinterpret_cast<FileResetFn>(resolve("lore_file_reset"));
    revisionCommit = reinterpret_cast<RevisionCommitFn>(resolve("lore_revision_commit"));
    revisionHistory = reinterpret_cast<RevisionHistoryFn>(resolve("lore_revision_history"));

    m_loaded = m_loadError.isEmpty();
}

QString hashToHex(const lore_hash_t &hash)
{
    const QByteArray bytes(reinterpret_cast<const char *>(hash.data), sizeof(hash.data));
    return QString::fromLatin1(bytes.toHex());
}

bool isZeroHash(const lore_hash_t &hash)
{
    for (const uint8_t byte : hash.data) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

} // namespace pimio::lore
