#pragma once

#include "pimio/core/durable_store.h"
#include "pimio/lore/lore_durable_store.h"

#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace pimio::testing {

/// Builds a media record with enough distinguishing fields to prove a round
/// trip through the durable store.
inline core::MediaRecord makeLoreRecord(const QString &id, const QString &caption)
{
    core::MediaRecord record;
    record.id = core::MediaId(id);
    record.fingerprint = core::ContentFingerprint(QStringLiteral("sha256"), id);
    record.identity.absolutePath = QStringLiteral("/library/%1.jpg").arg(id);
    record.identity.sizeBytes = 4096;
    record.metadata.kind = core::MediaKind::Image;
    record.metadata.caption = caption;
    return record;
}

/// Path of the `lore` CLI recorded by the build, or an empty string when the
/// CLI was not acquired. Tests use the CLI as an independent external writer,
/// which is the only honest way to prove that a change made outside pimio is
/// detected.
inline QString loreCliPath()
{
    return QString::fromUtf8(PIMIO_LORE_CLI_PATH);
}

inline bool hasUnmarkedFanOutGroup(const QString &repositoryPath)
{
    const QString indexRoot = repositoryPath + QStringLiteral("/.lore/immutable/index");
    QDirIterator buckets(indexRoot, QStringList{QStringLiteral("index_*")}, QDir::Files,
                         QDirIterator::Subdirectories);
    while (buckets.hasNext()) {
        const QFileInfo bucket(buckets.next());
        if (!QFileInfo::exists(bucket.absolutePath() + QStringLiteral("/level"))) {
            return true;
        }
    }
    return false;
}

/// Runs the `lore` CLI against \a repositoryPath and returns true on success.
inline bool runLoreCli(const QString &repositoryPath, const QStringList &arguments,
                       QString *output = nullptr)
{
    QProcess process;
    process.setWorkingDirectory(repositoryPath);
    process.setProcessChannelMode(QProcess::MergedChannels);
    QStringList full = arguments;
    full << QStringLiteral("--repository") << QDir::toNativeSeparators(repositoryPath)
         << QStringLiteral("--offline") << QStringLiteral("--no-pager");
    process.start(loreCliPath(), full);
    if (!process.waitForStarted(30'000) || !process.waitForFinished(120'000)) {
        if (output != nullptr) {
            *output = process.errorString();
        }
        return false;
    }
    const QString captured = QString::fromLocal8Bit(process.readAll());
    if (output != nullptr) {
        *output = captured;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
           && !captured.contains(QStringLiteral("[Error]"));
}

/// Opens a store, applying the interrupted-write repair when the pinned LORE
/// release refuses to open a store that another process left mid-write.
///
/// The repair only removes empty pending markers, so it never discards
/// committed data; `repaired` reports whether it was needed so a test can say
/// so out loud instead of hiding it.
inline bool openRepairingIfNeeded(pimio::lore::LoreDurableStore *store, pimio::core::Error *error,
                                  bool *repaired = nullptr)
{
    if (repaired != nullptr) {
        *repaired = false;
    }
    if (store->open(error)) {
        return true;
    }
    if (!store->needsRepairAfterInterruptedWrite()) {
        return false;
    }
    if (repaired != nullptr) {
        *repaired = true;
    }
    return store->repairAfterInterruptedWrite(error) && store->open(error);
}

} // namespace pimio::testing

/// Skips the current test function when LORE could not be loaded.
///
/// A contributor without the artifact still gets a green run; CI enables LORE
/// on every platform job, where a skip would hide the increment's evidence.
#define PIMIO_SKIP_WITHOUT_LORE()                                                                 \
    do {                                                                                          \
        if (pimio::lore::loadedLibraryVersion().isEmpty()) {                                      \
            QSKIP("The LORE library is not available in this build.");                            \
        }                                                                                         \
    } while (false)

#define PIMIO_SKIP_WITHOUT_LORE_CLI()                                                             \
    do {                                                                                          \
        if (pimio::testing::loreCliPath().isEmpty()) {                                            \
            QSKIP("The LORE command line tool is not available in this build.");                  \
        }                                                                                         \
    } while (false)
