#pragma once

#include "pimio/app/library_manager.h"

namespace pimio::app::library_manager_storage {

void assignError(core::Error *target, core::ErrorCode code, const QString &message);
QString normalizedLocation(const QString &path);
QString storePathFor(const QString &location);
bool copyTree(const QString &source, const QString &destination, core::Error *error);
bool writeArchive(const QString &location, const QString &archivePath, core::Error *error);
bool extractArchive(const QString &archivePath, const QString &destination, core::Error *error);

#ifdef PIMIO_HAVE_LORE
std::optional<LibraryInfo> inspectLibrary(const QString &location, core::Error *error);
bool rebuildDerivedState(const LibraryInfo &library, core::Error *error);
#endif

} // namespace pimio::app::library_manager_storage
