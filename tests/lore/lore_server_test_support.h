#pragma once

#include "lore_test_support.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QThread>

namespace pimio::testing {

struct ProcessResult
{
    bool succeeded = false;
    int exitCode = -1;
    QString output;
};

inline QString loreServerPath()
{
    return QString::fromUtf8(PIMIO_LORE_SERVER_PATH);
}

inline ProcessResult runProcess(const QString &program, const QString &workingDirectory,
                                const QStringList &arguments, int timeoutMs = 120'000)
{
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(30'000)) {
        return {false, -1, process.errorString()};
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(10'000);
        return {false, -1,
                QStringLiteral("Process timed out: %1").arg(QString::fromLocal8Bit(process.readAll()))};
    }
    const QString output = QString::fromLocal8Bit(process.readAll());
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
                    && !output.contains(QStringLiteral("[Error]")),
            process.exitCode(), output};
}

inline ProcessResult runLore(const QString &workingDirectory, const QStringList &arguments,
                             int timeoutMs = 120'000)
{
    QStringList full{QStringLiteral("--no-pager"), QStringLiteral("--non-interactive")};
    full.append(arguments);
    return runProcess(loreCliPath(), workingDirectory, full, timeoutMs);
}

class LoreTestServer
{
public:
    explicit LoreTestServer(QString rootPath)
        : m_rootPath(std::move(rootPath))
    {
    }

    ~LoreTestServer() { stop(); }

    bool start(QString *error)
    {
        stop();
        for (int attempt = 0; attempt < 10; ++attempt) {
            m_rpcPort = QRandomGenerator::global()->bounded(20'000, 55'000);
            m_httpPort = m_rpcPort + 1;
            QString output;
            if (launch(&output)) {
                return true;
            }
            stop();
            if (attempt == 9 && error != nullptr) {
                *error = QStringLiteral("loreserver did not become ready: %1").arg(output);
            }
        }
        return false;
    }

    void stop()
    {
        if (m_process.state() == QProcess::NotRunning) {
            return;
        }
        m_process.terminate();
        if (!m_process.waitForFinished(15'000)) {
            m_process.kill();
            m_process.waitForFinished(10'000);
        }
    }

    bool reset(QString *error)
    {
        stop();
        if (!QDir(storeDirectory()).removeRecursively()) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not remove %1").arg(storeDirectory());
            }
            return false;
        }
        // Keep the endpoint stable because offline origins persist this URL.
        QString output;
        if (launch(&output)) {
            return true;
        }
        if (error != nullptr) {
            *error = QStringLiteral("loreserver did not restart: %1").arg(output);
        }
        return false;
    }

    QString baseUrl() const
    {
        return QStringLiteral("lore://127.0.0.1:%1").arg(m_rpcPort);
    }

    QString repositoryUrl(const QString &name) const
    {
        return baseUrl() + QLatin1Char('/') + name;
    }

private:
    QString configDirectory() const { return m_rootPath + QStringLiteral("/config"); }
    QString storeDirectory() const { return m_rootPath + QStringLiteral("/store"); }

    bool launch(QString *output)
    {
        QString error;
        if (!writeConfig(&error)) {
            if (output != nullptr) {
                *output = error;
            }
            return false;
        }

        m_process.setWorkingDirectory(m_rootPath);
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.start(loreServerPath(), {QStringLiteral("--config"), configDirectory()});
        if (!m_process.waitForStarted(30'000)) {
            if (output != nullptr) {
                *output = m_process.errorString();
            }
            return false;
        }

        for (int probe = 0; probe < 100; ++probe) {
            if (m_process.state() == QProcess::NotRunning) {
                break;
            }
            const ProcessResult result =
                    runLore(m_rootPath,
                            {QStringLiteral("repository"), QStringLiteral("list"), baseUrl()}, 1'000);
            if (result.succeeded) {
                return true;
            }
            QThread::msleep(50);
        }
        if (output != nullptr) {
            *output = QString::fromLocal8Bit(m_process.readAll());
        }
        return false;
    }

    bool writeConfig(QString *error)
    {
        if (!QDir().mkpath(configDirectory()) || !QDir().mkpath(storeDirectory())) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not create isolated server directories.");
            }
            return false;
        }

        const QString store = QDir::fromNativeSeparators(storeDirectory());
        const QString text = QStringLiteral(
                                     "[server.quic]\n"
                                     "host = \"127.0.0.1\"\n"
                                     "port = %1\n\n"
                                     "[server.grpc]\n"
                                     "host = \"127.0.0.1\"\n"
                                     "port = %1\n\n"
                                     "[server.http]\n"
                                     "host = \"127.0.0.1\"\n"
                                     "port = %2\n\n"
                                     "[immutable_store.local]\n"
                                     "path = \"%3\"\n"
                                     "flush_delay_seconds = 1\n\n"
                                     "[mutable_store.local]\n"
                                     "path = \"%3\"\n"
                                     "flush_delay_seconds = 1\n")
                                     .arg(m_rpcPort)
                                     .arg(m_httpPort)
                                     .arg(store);
        QFile file(configDirectory() + QStringLiteral("/local.toml"));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(text.toUtf8()) != text.toUtf8().size()) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        return true;
    }

    QString m_rootPath;
    quint16 m_rpcPort = 0;
    quint16 m_httpPort = 0;
    QProcess m_process;
};

} // namespace pimio::testing
