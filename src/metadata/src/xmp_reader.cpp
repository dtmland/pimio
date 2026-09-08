#include "xmp_reader.h"

#include <QDateTime>
#include <QXmlStreamReader>

namespace pimio::metadata {
namespace {

const QLatin1String kXmpNamespace("http://ns.adobe.com/xap/1.0/");
const QLatin1String kDublinCoreNamespace("http://purl.org/dc/elements/1.1/");

/// Converts an XMP date, which is ISO 8601 with an optional zone designator.
/// A value with no designator keeps its offset unknown rather than being
/// assumed to be UTC or local.
std::optional<core::CaptureTime> parseXmpDate(const QString &text)
{
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const QDateTime parsed = QDateTime::fromString(text, Qt::ISODate);
    if (!parsed.isValid()) {
        return std::nullopt;
    }
    if (parsed.timeSpec() == Qt::LocalTime) {
        return core::CaptureTime::fromLocalWallClock(parsed);
    }
    return core::CaptureTime::fromOffset(parsed, parsed.offsetFromUtc());
}

void applyRating(const QString &text, FieldSet *fields)
{
    bool ok = false;
    const int rating = text.trimmed().toInt(&ok);
    if (ok) {
        fields->rating = qBound(0, rating, 5);
    }
}

/// Reads the first `rdf:li` inside an alternative or bag, which is how XMP
/// stores a language-tagged title or description.
QStringList readLanguageAlternativeOrBag(QXmlStreamReader &xml)
{
    QStringList values;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement && xml.name() == QLatin1String("li")) {
            values.append(xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
        } else if (token == QXmlStreamReader::EndElement
                   && (xml.name() == QLatin1String("Alt") || xml.name() == QLatin1String("Bag")
                       || xml.name() == QLatin1String("Seq"))) {
            break;
        }
    }
    return values;
}

} // namespace

bool readXmpPacket(const QByteArray &xmp, FieldSet *fields, QStringList *warnings)
{
    QXmlStreamReader xml(xmp);
    bool sawDescription = false;
    QString pendingProperty;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }

        const QString namespaceUri = xml.namespaceUri().toString();
        const QString name = xml.name().toString();

        if (name == QLatin1String("Description")) {
            sawDescription = true;
            // XMP allows every simple property to appear as an attribute.
            const QXmlStreamAttributes attributes = xml.attributes();
            for (const QXmlStreamAttribute &attribute : attributes) {
                const QString attributeName = attribute.name().toString();
                const QString attributeNamespace = attribute.namespaceUri().toString();
                if (attributeNamespace == kXmpNamespace
                    && attributeName == QLatin1String("CreateDate")) {
                    if (const auto captureTime = parseXmpDate(attribute.value().toString())) {
                        fields->captureTime = captureTime;
                    }
                } else if (attributeNamespace == kXmpNamespace
                           && attributeName == QLatin1String("Rating")) {
                    applyRating(attribute.value().toString(), fields);
                } else if (attributeNamespace == kDublinCoreNamespace
                           && attributeName == QLatin1String("title")) {
                    fields->caption = attribute.value().toString();
                }
            }
            continue;
        }

        // The same properties may also appear as elements.
        if (namespaceUri == kXmpNamespace && name == QLatin1String("CreateDate")) {
            if (const auto captureTime = parseXmpDate(xml.readElementText().trimmed())) {
                fields->captureTime = captureTime;
            }
        } else if (namespaceUri == kXmpNamespace && name == QLatin1String("Rating")) {
            applyRating(xml.readElementText(), fields);
        } else if (namespaceUri == kDublinCoreNamespace
                   && (name == QLatin1String("title") || name == QLatin1String("description"))) {
            const QStringList values = readLanguageAlternativeOrBag(xml);
            // A title is the caption; a description only fills one that is
            // still absent, so a file carrying both keeps the more specific.
            if (!values.isEmpty()
                && (name == QLatin1String("title") || !fields->caption.has_value())) {
                fields->caption = values.first();
            }
        } else if (namespaceUri == kDublinCoreNamespace && name == QLatin1String("subject")) {
            const QStringList values = readLanguageAlternativeOrBag(xml);
            if (!values.isEmpty()) {
                fields->tags = values;
            }
        }
    }

    if (xml.hasError()) {
        warnings->append(QStringLiteral("The XMP metadata is not well-formed XML: %1")
                                 .arg(xml.errorString()));
        return false;
    }
    if (!sawDescription) {
        warnings->append(QStringLiteral("The XMP packet contains no RDF description."));
        return false;
    }
    return true;
}

} // namespace pimio::metadata
