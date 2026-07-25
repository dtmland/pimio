#include "pimio/testing/fake_metadata_reader.h"

namespace pimio::testing {

void FakeMetadataReader::addResult(const QString &absolutePath, core::MetadataReadResult result)
{
    m_results.insert(absolutePath, std::move(result));
}

void FakeMetadataReader::addUnreadable(const QString &absolutePath, core::ErrorCode code)
{
    m_unreadable.insert(absolutePath, code);
}

bool FakeMetadataReader::supports(const QString &absolutePath) const
{
    return m_results.contains(absolutePath) || m_unreadable.contains(absolutePath);
}

std::optional<core::MetadataReadResult> FakeMetadataReader::read(const QString &absolutePath,
                                                                 core::Error *error) const
{
    m_readPaths.append(absolutePath);

    const auto unreadable = m_unreadable.constFind(absolutePath);
    if (unreadable != m_unreadable.constEnd()) {
        if (error) {
            *error = core::Error(unreadable.value(), QStringLiteral("Cannot read metadata."));
        }
        return std::nullopt;
    }

    const auto result = m_results.constFind(absolutePath);
    if (result == m_results.constEnd()) {
        if (error) {
            *error = core::Error(core::ErrorCode::UnsupportedMedia,
                                 QStringLiteral("Unsupported media."));
        }
        return std::nullopt;
    }
    return result.value();
}

QStringList FakeMetadataReader::readPaths() const
{
    return m_readPaths;
}

} // namespace pimio::testing
