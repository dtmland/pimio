#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace pimio::core {

/// A single non-destructive operation applied to a media item.
///
/// Operations are stored as data, never as UI actions or renderer commands, so
/// that the 2.0.0 frontend can replay the same recipe.
enum class EditOperationKind {
    Unknown,
    Crop,
    Rotate,
    Orientation,
    Trim,
};

QString toString(EditOperationKind kind);
EditOperationKind editOperationKindFromString(const QString &value);

/// One entry in an edit recipe.
///
/// Parameters are held as a JSON object so that an operation defined by a newer
/// pimio version round trips unchanged through an older one.
class EditOperation
{
public:
    EditOperation() = default;
    EditOperation(EditOperationKind kind, QJsonObject parameters);

    EditOperationKind kind() const;
    const QString &rawKind() const;
    const QJsonObject &parameters() const;

    bool isRecognized() const;

    bool operator==(const EditOperation &other) const = default;

    QJsonObject toJson() const;
    static EditOperation fromJson(const QJsonObject &object);

private:
    EditOperationKind m_kind = EditOperationKind::Unknown;
    QString m_rawKind;
    QJsonObject m_parameters;
};

/// An ordered, versioned list of operations applied to one media item.
///
/// The recipe is the source of truth for both display and export. The original
/// file is never modified by applying a recipe.
class EditRecipe
{
public:
    /// Monotonic revision incremented on every user-visible change. Used to
    /// detect stale recipes and to invalidate derived renders.
    int revision() const;
    void setRevision(int revision);

    const QList<EditOperation> &operations() const;
    void setOperations(QList<EditOperation> operations);
    void append(EditOperation operation);

    bool isEmpty() const;

    /// True when every operation in the recipe is understood by this build.
    /// A recipe containing unknown operations must not be silently flattened
    /// or exported as if it were fully applied.
    bool isFullyRecognized() const;

    bool operator==(const EditRecipe &other) const = default;

    QJsonObject toJson() const;
    static EditRecipe fromJson(const QJsonObject &object);

    const QJsonObject &unrecognizedFields() const;
    void setUnrecognizedFields(QJsonObject fields);

    static QStringList knownKeys();

private:
    int m_revision = 0;
    QList<EditOperation> m_operations;
    QJsonObject m_unrecognizedFields;
};

} // namespace pimio::core
