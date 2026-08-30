#pragma once

#include "pimio/core/durable_store.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QThread>

#include <optional>

namespace pimio::lore::detail {

void setError(core::Error *target, core::ErrorCode code, const QString &message,
              QJsonObject context = {});

QString recordFileName(const core::MediaId &id);
QString shardFor(const QString &fileName);

bool writeRecordFile(const QString &path, const core::MediaRecord &record, core::Error *error);
std::optional<core::MediaRecord> readRecordFile(const QString &path, core::Error *error);

QStringList emptyPendingMarkers(const QString &repositoryPath);

bool removeDirectoryContents(const QString &path);
bool copyDirectory(const QString &sourcePath, const QString &targetPath);

QByteArray nativePath(const QString &path);

template <typename Step>
bool withTransientRetries(Step step)
{
    constexpr int kAttempts = 25;
    constexpr unsigned long kDelayMs = 20;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (step()) {
            return true;
        }
        QThread::msleep(kDelayMs);
    }
    return step();
}

} // namespace pimio::lore::detail
