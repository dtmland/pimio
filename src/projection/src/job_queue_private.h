#pragma once

#include "pimio/projection/job_queue.h"
#include "pimio/projection/migration.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace pimio::projection {

using core::Error;
using core::ErrorCode;
using core::JobId;
using core::JobRecord;

void jqSetError(Error *error, ErrorCode code, const QString &message);
bool jqExecuteStatement(QSqlDatabase &db, const QString &statement, Error *error);

extern const QLatin1StringView kJobSelectCols;

JobRecord recordFromQuery(QSqlQuery &query);

class JobQueue::Private
{
public:
    void discardConnection();

    QString connectionName = QStringLiteral("pimio-jobqueue-")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString path;
    bool open = false;

    QSqlDatabase database() const;
    bool prepared(QSqlQuery &query, const QString &statement, Error *error) const;
    bool applyPragmas(QSqlDatabase &db, bool onDisk, Error *error) const;
    bool setup(QSqlDatabase &db, bool onDisk, Error *error);
};

} // namespace pimio::projection
