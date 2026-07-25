#pragma once

#include "pimio/core/metadata_reader.h"

#include <QHash>
#include <QStringList>

namespace pimio::testing {

/// MetadataReader backed by a table supplied by the test.
///
/// Files that were never registered are reported as unsupported media rather
/// than causing a failure, which is the behavior the scan contract requires.
class FakeMetadataReader final : public core::MetadataReader
{
public:
    void addResult(const QString &absolutePath, core::MetadataReadResult result);

    /// Registers a file that exists but cannot be parsed.
    void addUnreadable(const QString &absolutePath, core::ErrorCode code);

    bool supports(const QString &absolutePath) const override;
    std::optional<core::MetadataReadResult> read(const QString &absolutePath,
                                                 core::Error *error) const override;

    QStringList readPaths() const;

private:
    QHash<QString, core::MetadataReadResult> m_results;
    QHash<QString, core::ErrorCode> m_unreadable;
    mutable QStringList m_readPaths;
};

} // namespace pimio::testing
