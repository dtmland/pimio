#pragma once

#include "pimio/projection/projection_database.h"
#include "pimio/projection/migration.h"

#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QVariant>

namespace pimio::projection {

using core::Error;
using core::ErrorCode;
using core::MediaId;

void setError(Error *error, ErrorCode code, const QString &message);

QString notNull(const QString &value);
QString fileExtension(const QString &fileName);
QString ftsMatchExpression(const QString &query);

class ProjectionDatabase::Private
{
public:
    void discardConnection();

    QString connectionName = QStringLiteral("pimio-projection-")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString path;
    int schemaVersion = 0;
    bool open = false;

    QSqlDatabase database() const;
    bool prepared(QSqlQuery &query, const QString &statement, Error *error) const;
    bool applyPragmas(QSqlDatabase &db, bool onDisk, Error *error) const;
    bool checkIntegrity(QSqlDatabase &db, Error *error) const;
    bool insertRecord(QSqlDatabase &db, const core::MediaRecord &record, Error *error) const;
    QList<MediaId> idsFrom(const QString &statement, const QVariantList &bindings,
                           Error *error) const;
};

} // namespace pimio::projection
