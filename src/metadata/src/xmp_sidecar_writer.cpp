#include "pimio/metadata/xmp_sidecar_writer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>

namespace pimio::metadata {
namespace {

void setError(core::Error *error, core::ErrorCode code, const QString &message, const QString &path)
{
    if (error) {
        *error = core::Error(code, message).withContext({{QStringLiteral("path"), path}});
    }
}

QString xmlEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return escaped;
}

QString xmpDate(const core::CaptureTime &time)
{
    if (!time.isValid()) {
        return {};
    }
    const QString local = time.wallClock().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    if (!time.hasKnownOffset()) {
        return local;
    }
    const int offset = *time.utcOffsetSeconds() / 60;
    if (offset == 0) {
        return local + QLatin1Char('Z');
    }
    return QStringLiteral("%1%2%3:%4")
            .arg(local, offset < 0 ? QStringLiteral("-") : QStringLiteral("+"))
            .arg(qAbs(offset) / 60, 2, 10, QLatin1Char('0'))
            .arg(qAbs(offset) % 60, 2, 10, QLatin1Char('0'));
}

QByteArray managedDescription(const core::MediaMetadata &metadata, const core::EditRecipe &recipe)
{
    QByteArray result;
    result += "\n<rdf:Description xmlns:pimio=\"https://pimio.local/ns/1.0/\""
              " xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\""
              " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
              " pimio:owner=\"pimio\"";
    if (!xmpDate(metadata.captureTime).isEmpty()) {
        result += " xmp:CreateDate=\"" + xmlEscape(xmpDate(metadata.captureTime)).toUtf8() + "\"";
    }
    result += " xmp:Rating=\"" + QByteArray::number(metadata.rating) + "\">";
    result += "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">"
              + xmlEscape(metadata.caption).toUtf8()
              + "</rdf:li></rdf:Alt></dc:title><dc:subject><rdf:Bag>";
    for (const QString &tag : metadata.tags) {
        result += "<rdf:li>" + xmlEscape(tag).toUtf8() + "</rdf:li>";
    }
    result += "</rdf:Bag></dc:subject><pimio:recipe>"
              + xmlEscape(QString::fromUtf8(
                      QJsonDocument(recipe.toJson()).toJson(QJsonDocument::Compact).toBase64()))
                        .toUtf8()
              + "</pimio:recipe></rdf:Description>\n";
    return result;
}

QByteArray freshPacket(const QByteArray &description)
{
    return "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
           "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
           "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
           + description
           + "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n";
}

bool validXmp(const QByteArray &bytes)
{
    QXmlStreamReader reader(bytes);
    bool rdf = false;
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement
            && reader.name() == QLatin1String("RDF")) {
            rdf = true;
        }
    }
    return rdf && !reader.hasError();
}

QByteArray replaceManagedDescription(QByteArray packet, const QByteArray &description)
{
    // This pattern only targets the exact, namespace-qualified block produced
    // above; unknown descriptions are never parsed, normalized, or removed.
    QRegularExpression old(
            QStringLiteral(R"(<rdf:Description\b(?=[^>]*pimio:owner="pimio")[^>]*>.*?</rdf:Description>\s*)"),
            QRegularExpression::DotMatchesEverythingOption);
    QString text = QString::fromUtf8(packet);
    text.replace(old, QString());
    const QRegularExpression closing(QStringLiteral(R"(</rdf:RDF\s*>)"));
    const QRegularExpressionMatch match = closing.match(text);
    if (!match.hasMatch()) {
        return {};
    }
    text.insert(match.capturedStart(), QString::fromUtf8(description));
    return text.toUtf8();
}

} // namespace

bool XmpSidecarWriter::supportsEmbeddedWrite(const QString &) const
{
    // Rewriting an arbitrary embedded packet while retaining all unknown tags
    // cannot be proved safe with v1's bounded parsers. Sidecars are portable
    // and keep managed originals byte-for-byte stable.
    return false;
}

QString XmpSidecarWriter::sidecarPathFor(const QString &absolutePath) const
{
    const QFileInfo info(absolutePath);
    const QString appended = absolutePath + QStringLiteral(".xmp");
    if (QFile::exists(appended)) {
        return appended;
    }
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".xmp"));
}

XmpSidecarWriter::Snapshot XmpSidecarWriter::snapshot(const QString &absolutePath,
                                                       core::Error *error) const
{
    Snapshot result;
    result.path = sidecarPathFor(absolutePath);
    QFile sidecar(result.path);
    result.existed = sidecar.exists();
    if (!result.existed) {
        return result;
    }
    if (!sidecar.open(QIODevice::ReadOnly)) {
        setError(error, core::ErrorCode::PermissionDenied,
                 QStringLiteral("Cannot read XMP sidecar: %1").arg(sidecar.errorString()), result.path);
        return {};
    }
    result.bytes = sidecar.readAll();
    return result;
}

bool XmpSidecarWriter::write(const QString &absolutePath, const core::MediaMetadata &metadata,
                             core::MetadataOrigin expectedOrigin, core::Error *error)
{
    Snapshot expected = snapshot(absolutePath, error);
    if (error && error->isError()) {
        return false;
    }
    if ((expected.existed && expectedOrigin != core::MetadataOrigin::Sidecar)
        || (!expected.existed && expectedOrigin == core::MetadataOrigin::Sidecar)) {
        setError(error, core::ErrorCode::Conflict,
                 QStringLiteral("The XMP sidecar changed before it could be saved."), expected.path);
        return false;
    }
    return write(expected, metadata, {}, error);
}

bool XmpSidecarWriter::write(const Snapshot &expected, const core::MediaMetadata &metadata,
                             const core::EditRecipe &recipe, core::Error *error) const
{
    QLockFile lock(expected.path + QStringLiteral(".pimio-lock"));
    if (!lock.tryLock(0)) {
        setError(error, core::ErrorCode::Conflict,
                 QStringLiteral("Another metadata writer is updating this sidecar."), expected.path);
        return false;
    }

    QFile existing(expected.path);
    const bool nowExists = existing.exists();
    QByteArray current;
    if (nowExists) {
        if (!existing.open(QIODevice::ReadOnly)) {
            setError(error, core::ErrorCode::PermissionDenied,
                     QStringLiteral("Cannot read XMP sidecar: %1").arg(existing.errorString()), expected.path);
            return false;
        }
        current = existing.readAll();
    }
    if (nowExists != expected.existed || current != expected.bytes) {
        setError(error, core::ErrorCode::Conflict,
                 QStringLiteral("The XMP sidecar was changed outside this edit session."), expected.path);
        return false;
    }
    if (nowExists && !validXmp(current)) {
        setError(error, core::ErrorCode::CorruptData,
                 QStringLiteral("Refusing to overwrite malformed XMP sidecar."), expected.path);
        return false;
    }
    if (nowExists && QString::fromUtf8(current).toUtf8() != current) {
        setError(error, core::ErrorCode::UnsupportedMedia,
                 QStringLiteral("Refusing to rewrite a non-UTF-8 XMP sidecar."), expected.path);
        return false;
    }

    const QByteArray description = managedDescription(metadata, recipe);
    const QByteArray output = nowExists ? replaceManagedDescription(current, description)
                                        : freshPacket(description);
    if (output.isEmpty()) {
        setError(error, core::ErrorCode::CorruptData,
                 QStringLiteral("The XMP sidecar has no RDF container."), expected.path);
        return false;
    }

    QSaveFile file(expected.path);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size()) {
        const core::ErrorCode code = file.error() == QFileDevice::PermissionsError
                ? core::ErrorCode::PermissionDenied : core::ErrorCode::OutOfSpace;
        setError(error, code, QStringLiteral("Cannot atomically save XMP sidecar: %1")
                 .arg(file.errorString()), expected.path);
        file.cancelWriting();
        return false;
    }
    QFile verify(expected.path);
    const bool verifyExists = verify.exists();
    if (verifyExists && !verify.open(QIODevice::ReadOnly)) {
        file.cancelWriting();
        setError(error, core::ErrorCode::PermissionDenied,
                 QStringLiteral("Cannot verify XMP sidecar before replacement."), expected.path);
        return false;
    }
    const QByteArray verified = verifyExists ? verify.readAll() : QByteArray();
    if (verifyExists != expected.existed || verified != expected.bytes) {
        file.cancelWriting();
        setError(error, core::ErrorCode::Conflict,
                 QStringLiteral("The XMP sidecar changed while it was being saved."), expected.path);
        return false;
    }
    if (!file.commit()) {
        const core::ErrorCode code = file.error() == QFileDevice::PermissionsError
                ? core::ErrorCode::PermissionDenied : core::ErrorCode::OutOfSpace;
        setError(error, code, QStringLiteral("Cannot atomically save XMP sidecar: %1")
                 .arg(file.errorString()), expected.path);
        file.cancelWriting();
        return false;
    }
    return true;
}

} // namespace pimio::metadata
