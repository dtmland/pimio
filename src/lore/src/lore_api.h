#pragma once

// Dynamic binding to the LORE C API.
//
// pimio never links against LORE. Every entry point is resolved at run time so
// a missing or mismatched library degrades to a visible "storage unavailable"
// state instead of preventing the process from starting. Keeping the header
// out of pimio's public API also keeps a LORE version bump contained to this
// directory.

#include <lore.h>

#include <QByteArray>
#include <QString>

namespace pimio::lore {

/// One resolved LORE entry point per operation the adapter uses.
///
/// A single instance is shared by the whole process because LORE keeps global
/// state (thread pools, store handles) behind these calls.
class LoreApi
{
public:
    /// Loads the library once and returns the shared instance. The returned
    /// object is always valid; check isLoaded().
    static LoreApi &instance();

    bool isLoaded() const { return m_loaded; }

    /// Why loading failed, for inclusion in a user-visible error.
    const QString &loadError() const { return m_loadError; }

    /// LORE_INTERFACE_VERSION of the header this was built against.
    static QString headerVersion();

    QString libraryPath() const { return m_libraryPath; }

    using RepositoryCreateFn = int32_t (*)(const lore_global_args_t *,
                                           const lore_repository_create_args_t *,
                                           lore_event_callback_config_t);
    using RepositoryStatusFn = int32_t (*)(const lore_global_args_t *,
                                           const lore_repository_status_args_t *,
                                           lore_event_callback_config_t);
    using RepositoryInfoFn = int32_t (*)(const lore_global_args_t *,
                                         const lore_repository_info_args_t *,
                                         lore_event_callback_config_t);
    using RepositoryReleaseFn = int32_t (*)(const lore_global_args_t *,
                                            const lore_repository_release_args_t *,
                                            lore_event_callback_config_t);
    using RepositoryFlushFn = int32_t (*)(const lore_global_args_t *,
                                          const lore_repository_flush_args_t *,
                                          lore_event_callback_config_t);
    using FileStageFn = int32_t (*)(const lore_global_args_t *,
                                    const lore_file_stage_args_t *,
                                    lore_event_callback_config_t);
    using FileUnstageFn = int32_t (*)(const lore_global_args_t *,
                                      const lore_file_unstage_args_t *,
                                      lore_event_callback_config_t);
    using FileResetFn = int32_t (*)(const lore_global_args_t *,
                                    const lore_file_reset_args_t *,
                                    lore_event_callback_config_t);
    using RevisionCommitFn = int32_t (*)(const lore_global_args_t *,
                                         const lore_revision_commit_args_t *,
                                         lore_event_callback_config_t);
    using RevisionHistoryFn = int32_t (*)(const lore_global_args_t *,
                                          const lore_revision_history_args_t *,
                                          lore_event_callback_config_t);
    using BranchPushFn = int32_t (*)(const lore_global_args_t *,
                                     const lore_branch_push_args_t *,
                                     lore_event_callback_config_t);

    RepositoryCreateFn repositoryCreate = nullptr;
    RepositoryStatusFn repositoryStatus = nullptr;
    RepositoryInfoFn repositoryInfo = nullptr;
    RepositoryReleaseFn repositoryRelease = nullptr;
    RepositoryFlushFn repositoryFlush = nullptr;
    FileStageFn fileStage = nullptr;
    FileUnstageFn fileUnstage = nullptr;
    FileResetFn fileReset = nullptr;
    RevisionCommitFn revisionCommit = nullptr;
    RevisionHistoryFn revisionHistory = nullptr;
    BranchPushFn branchPush = nullptr;

private:
    LoreApi();

    bool m_loaded = false;
    QString m_loadError;
    QString m_libraryPath;
};

/// Borrowed view of UTF-8 bytes. LORE copies argument data before the call
/// returns, so the owning QByteArray only has to outlive the call.
inline lore_string_t loreString(const QByteArray &utf8)
{
    lore_string_t value;
    value.string = utf8.isEmpty() ? nullptr : utf8.constData();
    value.length = static_cast<uintptr_t>(utf8.size());
    return value;
}

/// Copies a string carried by an event, which is only valid for the duration
/// of the callback.
inline QString fromLoreString(const lore_string_t &value)
{
    if (value.string == nullptr || value.length == 0) {
        return {};
    }
    return QString::fromUtf8(value.string, static_cast<qsizetype>(value.length));
}

/// Lowercase hex of a revision signature. All-zero means "no revision".
QString hashToHex(const lore_hash_t &hash);
bool isZeroHash(const lore_hash_t &hash);

} // namespace pimio::lore
