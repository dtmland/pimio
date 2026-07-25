#include "pimio/core/edit_recipe.h"

#include "pimio/core/serialization.h"

#include <QJsonArray>

namespace pimio::core {
namespace {

constexpr QLatin1StringView kKindKey{"kind"};
constexpr QLatin1StringView kParametersKey{"parameters"};
constexpr QLatin1StringView kRevisionKey{"revision"};
constexpr QLatin1StringView kOperationsKey{"operations"};

} // namespace

QString toString(EditOperationKind kind)
{
    switch (kind) {
    case EditOperationKind::Crop:
        return QStringLiteral("crop");
    case EditOperationKind::Rotate:
        return QStringLiteral("rotate");
    case EditOperationKind::Orientation:
        return QStringLiteral("orientation");
    case EditOperationKind::Trim:
        return QStringLiteral("trim");
    case EditOperationKind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

EditOperationKind editOperationKindFromString(const QString &value)
{
    if (value == QLatin1StringView("crop")) {
        return EditOperationKind::Crop;
    }
    if (value == QLatin1StringView("rotate")) {
        return EditOperationKind::Rotate;
    }
    if (value == QLatin1StringView("orientation")) {
        return EditOperationKind::Orientation;
    }
    if (value == QLatin1StringView("trim")) {
        return EditOperationKind::Trim;
    }
    return EditOperationKind::Unknown;
}

EditOperation::EditOperation(EditOperationKind kind, QJsonObject parameters)
    : m_kind(kind)
    , m_rawKind(toString(kind))
    , m_parameters(std::move(parameters))
{
}

EditOperationKind EditOperation::kind() const
{
    return m_kind;
}

const QString &EditOperation::rawKind() const
{
    return m_rawKind;
}

const QJsonObject &EditOperation::parameters() const
{
    return m_parameters;
}

bool EditOperation::isRecognized() const
{
    return m_kind != EditOperationKind::Unknown;
}

QJsonObject EditOperation::toJson() const
{
    QJsonObject object;
    object.insert(kKindKey, m_rawKind);
    object.insert(kParametersKey, m_parameters);
    return object;
}

EditOperation EditOperation::fromJson(const QJsonObject &object)
{
    EditOperation operation;
    operation.m_rawKind = object.value(kKindKey).toString();
    operation.m_kind = editOperationKindFromString(operation.m_rawKind);
    operation.m_parameters = object.value(kParametersKey).toObject();
    return operation;
}

int EditRecipe::revision() const
{
    return m_revision;
}

void EditRecipe::setRevision(int revision)
{
    m_revision = revision;
}

const QList<EditOperation> &EditRecipe::operations() const
{
    return m_operations;
}

void EditRecipe::setOperations(QList<EditOperation> operations)
{
    m_operations = std::move(operations);
}

void EditRecipe::append(EditOperation operation)
{
    m_operations.append(std::move(operation));
}

bool EditRecipe::isEmpty() const
{
    return m_operations.isEmpty();
}

bool EditRecipe::isFullyRecognized() const
{
    for (const EditOperation &operation : m_operations) {
        if (!operation.isRecognized()) {
            return false;
        }
    }
    return true;
}

QStringList EditRecipe::knownKeys()
{
    return {kRevisionKey, kOperationsKey};
}

const QJsonObject &EditRecipe::unrecognizedFields() const
{
    return m_unrecognizedFields;
}

void EditRecipe::setUnrecognizedFields(QJsonObject fields)
{
    m_unrecognizedFields = std::move(fields);
}

QJsonObject EditRecipe::toJson() const
{
    QJsonObject object;
    object.insert(kSchemaVersionKey, kRecordSchemaVersion);
    object.insert(kRevisionKey, m_revision);

    QJsonArray operations;
    for (const EditOperation &operation : m_operations) {
        operations.append(operation.toJson());
    }
    object.insert(kOperationsKey, operations);

    mergeUnknownFields(object, m_unrecognizedFields);
    return object;
}

EditRecipe EditRecipe::fromJson(const QJsonObject &object)
{
    EditRecipe recipe;
    recipe.m_revision = object.value(kRevisionKey).toInt();

    const QJsonArray operations = object.value(kOperationsKey).toArray();
    for (const QJsonValue &operation : operations) {
        recipe.m_operations.append(EditOperation::fromJson(operation.toObject()));
    }

    recipe.setUnrecognizedFields(unknownFields(object, knownKeys()));
    return recipe;
}

} // namespace pimio::core
