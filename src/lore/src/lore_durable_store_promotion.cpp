#include "pimio/lore/lore_durable_store.h"

#include "lore_durable_store_private.h"

#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>

namespace pimio::lore {
namespace {

bool attachRemote(const QString &configPath, const QString &remoteUrl, core::Error *error)
{
    if (remoteUrl.contains(QLatin1Char('"')) || remoteUrl.contains(QLatin1Char('\n'))
        || remoteUrl.contains(QLatin1Char('\r'))) {
        detail::setError(error, core::ErrorCode::InvalidArgument,
                         QStringLiteral("The server URL cannot be stored safely."));
        return false;
    }

    QFile source(configPath);
    if (!source.open(QIODevice::ReadOnly)) {
        detail::setError(error, core::ErrorCode::Internal,
                         QStringLiteral("Could not read LORE configuration: %1")
                                 .arg(source.errorString()));
        return false;
    }
    QString config = QString::fromUtf8(source.readAll());
    source.close();

    const QString setting = QStringLiteral("remote_url = \"%1\"").arg(remoteUrl);
    const QRegularExpression remoteLine(QStringLiteral(R"((?m)^remote_url\s*=.*$)"));
    if (config.contains(remoteLine)) {
        config.replace(remoteLine, setting);
    } else {
        config.prepend(setting + QLatin1Char('\n'));
    }

    QSaveFile destination(configPath);
    const QByteArray encoded = config.toUtf8();
    if (!destination.open(QIODevice::WriteOnly)
        || destination.write(encoded) != encoded.size() || !destination.commit()) {
        detail::setError(error, core::ErrorCode::Internal,
                         QStringLiteral("Could not update LORE configuration: %1")
                                 .arg(destination.errorString()));
        return false;
    }
    return true;
}

lore_global_args_t connectedGlobals(const QByteArray &repositoryPath)
{
    lore_global_args_t globals;
    std::memset(&globals, 0, sizeof(globals));
    globals.repository_path = loreString(repositoryPath);
    globals.sync_data = 1;
    return globals;
}

} // namespace

bool LoreDurableStore::promoteToServer(const QString &remoteUrl, core::Error *error)
{
    if (!d->available()) {
        detail::setError(error, core::ErrorCode::StorageUnavailable,
                         QStringLiteral("The durable store is not open."));
        return false;
    }

    const QUrl url(remoteUrl);
    if (!url.isValid() || (url.scheme() != QLatin1String("http")
                           && url.scheme() != QLatin1String("https"))
        || url.host().isEmpty()) {
        detail::setError(error, core::ErrorCode::InvalidArgument,
                         QStringLiteral("Enter a valid HTTP or HTTPS LORE repository URL."));
        return false;
    }

    Operation status;
    core::Error statusError;
    if (!d->runStatus(status, false, &statusError) || status.repositoryId.isEmpty()) {
        if (statusError.isError() && error != nullptr) {
            *error = statusError;
        } else {
            detail::setError(error, core::ErrorCode::Internal,
                             QStringLiteral("LORE did not report the local repository identity."));
        }
        return false;
    }

    QTemporaryDir registration;
    if (!registration.isValid()) {
        detail::setError(error, core::ErrorCode::Internal,
                         QStringLiteral("Could not create a temporary registration directory."));
        return false;
    }

    LoreApi &api = LoreApi::instance();
    const QByteArray urlUtf8 = remoteUrl.toUtf8();
    lore_global_args_t globals = connectedGlobals(d->repositoryPathUtf8);
    lore_repository_info_args_t infoArgs;
    std::memset(&infoArgs, 0, sizeof(infoArgs));
    infoArgs.repository_url = loreString(urlUtf8);
    Operation preflight;
    api.repositoryInfo(&globals, &infoArgs, preflight.config());
    if (preflight.status == 0 && preflight.remoteRepositoryId != status.repositoryId) {
        QJsonObject context =
                failureContext(preflight, QStringLiteral("lore_repository_info"));
        context.insert(QStringLiteral("localRepositoryId"), status.repositoryId);
        context.insert(QStringLiteral("remoteRepositoryId"), preflight.remoteRepositoryId);
        detail::setError(error, core::ErrorCode::Conflict,
                         QStringLiteral("The server repository identity does not match this "
                                        "library."),
                         context);
        return false;
    }

    const QString temporaryRepositoryPath =
            registration.path() + QStringLiteral("/repository");
    QDir().mkpath(temporaryRepositoryPath);
    const QByteArray temporaryPath = QFile::encodeName(temporaryRepositoryPath);
    const QByteArray idUtf8 = status.repositoryId.toUtf8();
    lore_global_args_t registrationGlobals = connectedGlobals(temporaryPath);
    lore_repository_create_args_t createArgs;
    std::memset(&createArgs, 0, sizeof(createArgs));
    createArgs.repository_url = loreString(urlUtf8);
    createArgs.id = loreString(idUtf8);

    Operation create;
    api.repositoryCreate(&registrationGlobals, &createArgs, create.config());
    if (create.status != 0) {
        detail::setError(error, mapFailure(create),
                         QStringLiteral("Could not register this library on the server: %1")
                                 .arg(create.message),
                         failureContext(create, QStringLiteral("lore_repository_create")));
        return false;
    }

    Operation info;
    api.repositoryInfo(&registrationGlobals, &infoArgs, info.config());
    if (info.status != 0 || info.remoteRepositoryId != status.repositoryId) {
        QJsonObject context = failureContext(info, QStringLiteral("lore_repository_info"));
        context.insert(QStringLiteral("localRepositoryId"), status.repositoryId);
        context.insert(QStringLiteral("remoteRepositoryId"), info.remoteRepositoryId);
        detail::setError(error, core::ErrorCode::Conflict,
                         QStringLiteral("The server repository identity does not match this "
                                        "library."),
                         context);
        return false;
    }

    const QString configPath = d->lorePath() + QStringLiteral("/config.toml");
    if (!attachRemote(configPath, remoteUrl, error)) {
        return false;
    }

    lore_branch_push_args_t pushArgs;
    std::memset(&pushArgs, 0, sizeof(pushArgs));
    Operation push;
    api.branchPush(&globals, &pushArgs, push.config());
    if (push.status != 0) {
        detail::setError(error, mapFailure(push),
                         QStringLiteral("The server was attached, but the initial push failed: %1")
                                 .arg(push.message),
                         failureContext(push, QStringLiteral("lore_branch_push")));
        return false;
    }
    return true;
}

} // namespace pimio::lore
