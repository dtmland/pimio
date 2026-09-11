#include "pimio/metadata/exiftool_metadata_writer.h"

#include "pimio/metadata/builtin_metadata_reader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace pimio::metadata {
namespace {

void assignError(core::Error *error, core::ErrorCode code, const QString &message,
                 const QString &path)
{
    if (error) {
        *error = core::Error(code, message)
                         .withContext({{QStringLiteral("path"), path}});
    }
}

QString sha256(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject writeObject(const core::MetadataWriteRequest &request)
{
    QJsonArray tags;
    for (const QString &tag : request.metadata.tags) {
        tags.append(tag);
    }
    return {
        {QStringLiteral("SourceFile"), request.absolutePath},
        {QStringLiteral("XMP-xmp:Rating"), qBound(0, request.metadata.rating, 5)},
        {QStringLiteral("XMP-dc:Description"), request.metadata.caption},
        {QStringLiteral("XMP-dc:Subject"), tags},
    };
}

bool metadataMatches(const core::MediaMetadata &expected, const core::MediaMetadata &actual)
{
    QStringList expectedTags = expected.tags;
    QStringList actualTags = actual.tags;
    expectedTags.removeDuplicates();
    actualTags.removeDuplicates();
    return actual.rating == qBound(0, expected.rating, 5)
           && actual.caption == expected.caption && actualTags == expectedTags;
}

std::pair<QString, QStringList> defaultCommand()
{
    const QString configured = qEnvironmentVariable("PIMIO_EXIFTOOL_EXECUTABLE");
    if (!configured.isEmpty()) {
        return {configured, {}};
    }

    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    const QString packagedScript =
            QDir(appDir).filePath(QStringLiteral("../Resources/exiftool/exiftool"));
#else
    const QString packagedScript =
            QDir(appDir).filePath(QStringLiteral("../share/pimio/exiftool/exiftool"));
#endif
    QString perl = qEnvironmentVariable("PIMIO_PERL_EXECUTABLE");
#ifdef Q_OS_WIN
    if (perl.isEmpty()) {
        const QString packagedPerl =
                QDir(appDir).filePath(QStringLiteral("../perl/bin/perl.exe"));
        if (QFileInfo(packagedPerl).isExecutable()) {
            perl = packagedPerl;
        }
    }
#endif
    if (perl.isEmpty()) {
        perl = QStringLiteral(PIMIO_EXIFTOOL_PERL);
    }
    if (QFileInfo(packagedScript).isFile()) {
        return {perl, {QDir::cleanPath(packagedScript)}};
    }
    return {perl, {QStringLiteral(PIMIO_EXIFTOOL_SCRIPT)}};
}

} // namespace

ExifToolMetadataWriter::ExifToolMetadataWriter()
{
    auto [program, arguments] = defaultCommand();
    m_program = std::move(program);
    m_prefixArguments = std::move(arguments);
}

ExifToolMetadataWriter::ExifToolMetadataWriter(QString program, QStringList prefixArguments)
    : m_program(std::move(program))
    , m_prefixArguments(std::move(prefixArguments))
{
}

bool ExifToolMetadataWriter::isAvailable() const
{
    return !m_program.isEmpty()
           && (QFileInfo(m_program).isExecutable()
               || !QStandardPaths::findExecutable(m_program).isEmpty());
}

bool ExifToolMetadataWriter::supportsEmbeddedWrite(const QString &absolutePath) const
{
    static const QStringList extensions{QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                        QStringLiteral("png"), QStringLiteral("tif"),
                                        QStringLiteral("tiff")};
    return extensions.contains(QFileInfo(absolutePath).suffix().toLower());
}

bool ExifToolMetadataWriter::writeBatch(
        const QList<core::MetadataWriteRequest> &requests, core::Error *error)
{
    if (requests.isEmpty()) {
        return true;
    }
    if (!isAvailable()) {
        assignError(error, core::ErrorCode::StorageUnavailable,
                    QStringLiteral("The ExifTool metadata adapter is unavailable."), {});
        return false;
    }

    QJsonArray edits;
    QStringList paths;
    for (const core::MetadataWriteRequest &request : requests) {
        if (!supportsEmbeddedWrite(request.absolutePath)) {
            assignError(error, core::ErrorCode::UnsupportedMedia,
                        QStringLiteral("This format does not support safe embedded metadata writes."),
                        request.absolutePath);
            return false;
        }
        if (sha256(request.absolutePath) != request.expectedFingerprint.digest()) {
            assignError(error, core::ErrorCode::Conflict,
                        QStringLiteral("The file changed after editing began."),
                        request.absolutePath);
            return false;
        }
        edits.append(writeObject(request));
        paths.append(request.absolutePath);
    }

    QTemporaryFile importFile;
    if (!importFile.open()
        || importFile.write(QJsonDocument(edits).toJson(QJsonDocument::Compact)) < 0
        || !importFile.flush()) {
        assignError(error, core::ErrorCode::OutOfSpace,
                    QStringLiteral("Could not prepare the ExifTool batch."), {});
        return false;
    }

    QStringList arguments = m_prefixArguments;
    arguments.append({QStringLiteral("-config"), QString(),
                      QStringLiteral("-overwrite_original"),
                      QStringLiteral("-json=%1").arg(importFile.fileName())});
    arguments.append(paths);
    QProcess process;
    process.start(m_program, arguments);
    if (!process.waitForStarted() || !process.waitForFinished(-1)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        assignError(error, core::ErrorCode::Internal,
                    detail.isEmpty() ? QStringLiteral("ExifTool could not write metadata.")
                                     : QStringLiteral("ExifTool could not write metadata: %1")
                                               .arg(detail),
                    {});
        return false;
    }

    BuiltinMetadataReader reader;
    for (const core::MetadataWriteRequest &request : requests) {
        core::Error readError;
        const auto result = reader.read(request.absolutePath, &readError);
        if (!result || !metadataMatches(request.metadata, result->metadata)) {
            assignError(error, core::ErrorCode::CorruptData,
                        result ? QStringLiteral("The embedded metadata failed verification.")
                               : QStringLiteral("The written file could not be reread: %1")
                                         .arg(readError.message()),
                        request.absolutePath);
            return false;
        }
    }

    return true;
}

} // namespace pimio::metadata
