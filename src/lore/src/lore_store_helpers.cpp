#include "lore_store_helpers.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

#include <utility>

namespace pimio::lore::detail {

using core::Error;
using core::ErrorCode;

namespace {

constexpr int kShardNameLength = 2;

} // namespace

void setError(Error *target, ErrorCode code, const QString &message, QJsonObject context)
{
    if (target == nullptr) {
        return;
    }
    Error error(code, message);
    if (!context.isEmpty()) {
        error.setContext(std::move(context));
    }
    *target = error;
}

QByteArray nativePath(const QString &path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(path)).toUtf8();
}

QString recordFileName(const core::MediaId &id)
{
    static const QRegularExpression safe(QStringLiteral("\\A[a-z0-9._-]{1,64}\\z"));
    const QString value = id.value();
    if (safe.match(value).hasMatch() && !value.startsWith(QLatin1Char('.'))) {
        return value + QStringLiteral(".json");
    }
    return QStringLiteral("x-") + QString::fromLatin1(value.toUtf8().toHex())
           + QStringLiteral(".json");
}

QString shardFor(const QString &fileName)
{
    QString shard = fileName.left(kShardNameLength);
    while (shard.size() < kShardNameLength) {
        shard.append(QLatin1Char('_'));
    }
    return shard;
}

bool writeRecordFile(const QString &path, const core::MediaRecord &record, Error *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create the directory %1.").arg(info.absolutePath()));
        return false;
    }

    // QSaveFile writes to a temporary file and renames it into place, so a
    // crash mid-write cannot leave a half-written record behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not open %1 for writing: %2").arg(path, file.errorString()));
        return false;
    }
    const QJsonDocument document(record.toJson());
    const QByteArray bytes = document.toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, ErrorCode::OutOfSpace,
                 QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

std::optional<core::MediaRecord> readRecordFile(const QString &path, Error *error)
{
    QFile file(path);
    if (!file.exists()) {
        setError(error, ErrorCode::NotFound, QStringLiteral("No record at %1.").arg(path));
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not read %1: %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, ErrorCode::CorruptData,
                 QStringLiteral("The record at %1 is not valid JSON: %2")
                     .arg(path, parseError.errorString()));
        return std::nullopt;
    }
    return core::MediaRecord::fromJson(document.object());
}

QStringList emptyPendingMarkers(const QString &repositoryPath)
{
    QStringList markers;
    const QString root = repositoryPath + QStringLiteral("/.lore");
    if (!QFileInfo::exists(root)) {
        return markers;
    }
    QDirIterator iterator(root, QStringList{QStringLiteral("level.pending")},
                          QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (QFileInfo(path).size() == 0) {
            markers.append(path);
        }
    }
    markers.sort();
    return markers;
}

bool removeDirectoryContents(const QString &path)
{
    QDir directory(path);
    if (!directory.exists()) {
        return true;
    }
    bool removed = directory.removeRecursively();
    removed = QDir().mkpath(path) && removed;
    return removed;
}

bool copyDirectory(const QString &sourcePath, const QString &targetPath)
{
    if (!removeDirectoryContents(targetPath) || !QDir().mkpath(targetPath)) {
        return false;
    }

    const QDir source(sourcePath);
    QDirIterator iterator(sourcePath, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourceFilePath = iterator.next();
        const QString targetFilePath = targetPath + QLatin1Char('/')
                                       + source.relativeFilePath(sourceFilePath);
        const QFileInfo sourceFile(sourceFilePath);
        if (sourceFile.isDir()) {
            if (!QDir().mkpath(targetFilePath)) {
                return false;
            }
        } else if (sourceFile.fileName() == QLatin1String("lock")) {
            // LORE recreates these transient files when it opens the restored store.
            continue;
        } else if (!QDir().mkpath(QFileInfo(targetFilePath).absolutePath())
                   || !QFile::copy(sourceFilePath, targetFilePath)) {
            return false;
        }
    }
    return true;
}

} // namespace pimio::lore::detail
