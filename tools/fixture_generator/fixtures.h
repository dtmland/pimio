#pragma once

#include <QByteArray>
#include <QDir>
#include <QJsonArray>
#include <QList>
#include <QString>

namespace pimio::fixtures {

struct Fixture
{
    QString path;
    QByteArray contents;
    QString covers;
    QString notes;
};

QList<Fixture> buildFixtures();
QString sha256Of(const QByteArray &contents);
bool appendExternalFixtureEntries(const QDir &outputDir, QJsonArray *entries);

} // namespace pimio::fixtures
