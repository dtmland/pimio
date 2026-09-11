#include "pimio/metadata/exiftool_metadata_writer.h"

#include "pimio/metadata/builtin_metadata_reader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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

QStringList writerArguments(const core::MediaMetadata &metadata)
{
    QStringList arguments{
        QStringLiteral("-config"),
        QString(),
        QStringLiteral("-overwrite_original"),
        QStringLiteral("-XMP-xmp:Rating=%1").arg(qBound(0, metadata.rating, 5)),
        QStringLiteral("-XMP-dc:Description=%1").arg(metadata.caption),
        QStringLiteral("-XMP-dc:Subject="),
    };
    for (const QString &tag : metadata.tags) {
        arguments.append(QStringLiteral("-XMP-dc:Subject+=%1").arg(tag));
    }
    return arguments;
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
        if (QFileInfo::isExecutable(packagedPerl)) {
            perl = packagedPerl;
        }
    }
#endif
    if (perl.isEmpty()) {
        perl = QStringLiteral(PIMIO_EXIFTOOL_PERL);
    }
    if (QFileInfo::isFile(packagedScript)) {
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
           && (QFileInfo::isExecutable(m_program)
               || !QStandardPaths::findExecutable(m_program).isEmpty());
}

bool ExifToolMetadataWriter::supportsEmbeddedWrite(const QString &absolutePath) const
{
    static const QStringList extensions{QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                        QStringLiteral("png"), QStringLiteral("tif"),
                                        QStringLiteral("tiff")};
    return extensions.contains(QFileInfo(absolutePath).suffix().toLower());
}

bool ExifToolMetadataWriter::write(const QString &absolutePath,
                                   const core::MediaMetadata &metadata,
                                   const core::ContentFingerprint &expectedFingerprint,
                                   core::Error *error)
{
    if (!supportsEmbeddedWrite(absolutePath)) {
        assignError(error, core::ErrorCode::UnsupportedMedia,
                    QStringLiteral("This format does not support safe embedded metadata writes."),
                    absolutePath);
        return false;
    }
    if (!isAvailable()) {
        assignError(error, core::ErrorCode::StorageUnavailable,
                    QStringLiteral("The ExifTool metadata adapter is unavailable."), absolutePath);
        return false;
    }
    if (sha256(absolutePath) != expectedFingerprint.digest()) {
        assignError(error, core::ErrorCode::Conflict,
                    QStringLiteral("The file changed after editing began."), absolutePath);
        return false;
    }

    QStringList arguments = m_prefixArguments;
    arguments.append(writerArguments(metadata));
    arguments.append(absolutePath);
    QProcess process;
    process.start(m_program, arguments);
    if (!process.waitForStarted() || !process.waitForFinished(-1)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        assignError(error, core::ErrorCode::Internal,
                    detail.isEmpty() ? QStringLiteral("ExifTool could not write metadata.")
                                     : QStringLiteral("ExifTool could not write metadata: %1")
                                               .arg(detail),
                    absolutePath);
        return false;
    }

    BuiltinMetadataReader reader;
    core::Error readError;
    const auto result = reader.read(absolutePath, &readError);
    if (!result || !metadataMatches(metadata, result->metadata)) {
        assignError(error, core::ErrorCode::CorruptData,
                    result ? QStringLiteral("The embedded metadata failed verification.")
                           : QStringLiteral("The written file could not be reread: %1")
                                     .arg(readError.message()),
                    absolutePath);
        return false;
    }

    return true;
}

} // namespace pimio::metadata
