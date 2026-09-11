#include "pimio/core/file_system.h"
#include "pimio/core/metadata_reader.h"

namespace pimio::core {

FileSystem::~FileSystem() = default;

MetadataReader::~MetadataReader() = default;

MetadataWriter::~MetadataWriter() = default;

bool MetadataWriter::write(const QString &absolutePath, const MediaMetadata &metadata,
                           const ContentFingerprint &expectedFingerprint, Error *error)
{
    return writeBatch({MetadataWriteRequest{absolutePath, metadata, expectedFingerprint}}, error);
}

} // namespace pimio::core
