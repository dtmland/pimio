#include "lore_server_test_support.h"
#include "lore_test_support.h"

#include "pimio/lore/lore_durable_store.h"
#include "pimio/testing/qtest_printers.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::core;
using namespace pimio::lore;
using namespace pimio::testing;

namespace {

constexpr auto kIdentity = "promotion-test@example.invalid";

QString repositoryId(const QString &repositoryPath)
{
    const ProcessResult status =
            runLore(repositoryPath,
                    {QStringLiteral("--offline"), QStringLiteral("status"),
                     QStringLiteral("--revision-only")});
    if (!status.succeeded) {
        return {};
    }
    const QRegularExpression expression(QStringLiteral(R"(Repository ([0-9a-f]{32}))"));
    return expression.match(status.output).captured(1);
}

QByteArray fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result();
}

bool writeInterruptionPayload(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QByteArray chunk(1024 * 1024, Qt::Uninitialized);
    for (int block = 0; block < 32; ++block) {
        auto *words = reinterpret_cast<quint32 *>(chunk.data());
        QRandomGenerator generator(static_cast<quint32>(block + 1));
        generator.fillRange(words, chunk.size() / qsizetype(sizeof(quint32)));
        if (file.write(chunk) != chunk.size()) {
            return false;
        }
    }
    return true;
}

bool createRemoteRegistration(const QString &directory, const QString &url,
                              const QString &repositoryId, QString *output)
{
    if (!QDir().mkpath(directory)) {
        return false;
    }
    const ProcessResult result =
            runLore(directory,
                    {QStringLiteral("--identity"), QString::fromLatin1(kIdentity),
                     QStringLiteral("repository"), QStringLiteral("create"),
                     QStringLiteral("--id"), repositoryId, url});
    if (output != nullptr) {
        *output = result.output;
    }
    return result.succeeded;
}

QStringList checkpointIds(const QList<Checkpoint> &history)
{
    QStringList ids;
    for (const Checkpoint &checkpoint : history) {
        ids.append(checkpoint.id);
    }
    return ids;
}

} // namespace

class TestLoreServerPromotion : public QObject
{
    Q_OBJECT

private slots:
    void knownRemotePromotesAndSurvivesFailures();
    void noRemoteRequiresAnUpstreamAttachOperation();
};

void TestLoreServerPromotion::knownRemotePromotesAndSurvivesFailures()
{
    PIMIO_SKIP_WITHOUT_LORE();
    PIMIO_SKIP_WITHOUT_LORE_CLI();
    QVERIFY2(!loreServerPath().isEmpty(), "The opt-in gate requires loreserver.");

    QElapsedTimer runtime;
    runtime.start();
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    LoreTestServer server(temporary.filePath(QStringLiteral("server")));
    QString serverError;
    QVERIFY2(server.start(&serverError), qPrintable(serverError));
    const QString remoteUrl = server.repositoryUrl(QStringLiteral("pimio-promotion"));

    const QString originStore = temporary.filePath(QStringLiteral("origin-store"));
    const QString originRepository = originStore + QStringLiteral("/repository");
    QVERIFY(QDir().mkpath(originRepository));
    ProcessResult result =
            runLore(originRepository,
                    {QStringLiteral("--offline"), QStringLiteral("--identity"),
                     QString::fromLatin1(kIdentity), QStringLiteral("repository"),
                     QStringLiteral("create"), remoteUrl});
    QVERIFY2(result.succeeded, qPrintable(result.output));

    LibraryDescriptor originDescriptor;
    {
        LoreDurableStore origin(originStore);
        Error error;
        QVERIFY2(origin.open(&error), qPrintable(error.message()));
        QVERIFY2(origin.createLibrary(QStringLiteral("Promotion Library"), &error),
                 qPrintable(error.message()));
        const auto descriptor = origin.libraryDescriptor(&error);
        QVERIFY2(descriptor.has_value(), qPrintable(error.message()));
        originDescriptor = *descriptor;
        QVERIFY(origin.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("offline one")),
                             &error));
        QVERIFY(origin.stage(makeLoreRecord(QStringLiteral("m-2"), QStringLiteral("offline two")),
                             &error));
        QVERIFY2(origin.commit(QStringLiteral("Populate offline library"), &error).has_value(),
                 qPrintable(error.message()));
    }

    const QString payloadPath = originRepository + QStringLiteral("/payload.bin");
    QVERIFY(writeInterruptionPayload(payloadPath));
    const QByteArray payloadHash = fileSha256(payloadPath);
    QVERIFY(!payloadHash.isEmpty());
    result = runLore(originRepository,
                     {QStringLiteral("--offline"), QStringLiteral("stage"),
                      QStringLiteral("payload.bin")});
    QVERIFY2(result.succeeded, qPrintable(result.output));
    result = runLore(originRepository,
                     {QStringLiteral("--offline"), QStringLiteral("commit"),
                      QStringLiteral("Add promotion payload")});
    QVERIFY2(result.succeeded, qPrintable(result.output));

    const QString originRepositoryId = repositoryId(originRepository);
    QVERIFY(!originRepositoryId.isEmpty());
    const QString mismatchStore = temporary.filePath(QStringLiteral("mismatch-store"));
    const QString mismatchRepository = mismatchStore + QStringLiteral("/repository");
    QVERIFY(QDir().mkpath(mismatchRepository));
    result = runLore(mismatchRepository,
                     {QStringLiteral("--offline"), QStringLiteral("--identity"),
                      QString::fromLatin1(kIdentity), QStringLiteral("repository"),
                      QStringLiteral("create"), remoteUrl});
    QVERIFY2(result.succeeded, qPrintable(result.output));
    {
        LoreDurableStore mismatchOrigin(mismatchStore);
        Error error;
        QVERIFY2(mismatchOrigin.open(&error), qPrintable(error.message()));
        QVERIFY2(mismatchOrigin.createLibrary(QStringLiteral("Mismatch Library"), &error),
                 qPrintable(error.message()));
        QVERIFY(mismatchOrigin.stage(
                makeLoreRecord(QStringLiteral("mismatch-1"), QStringLiteral("before push")),
                &error));
        QVERIFY2(mismatchOrigin.commit(QStringLiteral("Mismatch content"), &error).has_value(),
                 qPrintable(error.message()));
    }
    const QString mismatchOriginId = repositoryId(mismatchRepository);
    QVERIFY(!mismatchOriginId.isEmpty());
    QString mismatchedId(32, QLatin1Char('f'));
    if (mismatchedId == mismatchOriginId) {
        mismatchedId.fill(QLatin1Char('e'));
    }
    QString registrationOutput;
    QVERIFY2(createRemoteRegistration(temporary.filePath(QStringLiteral("mismatch-registration")),
                                      remoteUrl, mismatchedId, &registrationOutput),
             qPrintable(registrationOutput));

    result = runLore(mismatchRepository, {QStringLiteral("push")});
    const bool mismatchWasAccepted =
            result.exitCode == 0 && result.output.contains(QStringLiteral("Pushed revision"));
    QEXPECT_FAIL("",
                 "LORE 0.9 accepts a push whose repository ID differs from the registered ID.",
                 Continue);
    QVERIFY2(!mismatchWasAccepted, "A remote with a different repository ID accepted the push.");
    QCOMPARE(repositoryId(mismatchRepository), mismatchOriginId);

    {
        LoreDurableStore mismatchOrigin(mismatchStore);
        Error error;
        QVERIFY2(mismatchOrigin.open(&error), qPrintable(error.message()));
        QVERIFY(mismatchOrigin.stage(
                makeLoreRecord(QStringLiteral("mismatch-2"), QStringLiteral("after mismatched push")),
                &error));
        QVERIFY2(mismatchOrigin.commit(QStringLiteral("Post-mismatch write"), &error).has_value(),
                 qPrintable(error.message()));
    }

    QVERIFY2(server.reset(&serverError), qPrintable(serverError));
    QVERIFY2(createRemoteRegistration(temporary.filePath(QStringLiteral("matching-registration")),
                                      server.repositoryUrl(QStringLiteral("pimio-promotion")),
                                      originRepositoryId, &registrationOutput),
             qPrintable(registrationOutput));

    result = runLore(originRepository, {QStringLiteral("push")}, 300'000);
    QVERIFY2(result.succeeded, qPrintable(result.output));
    QCOMPARE(repositoryId(originRepository), originRepositoryId);

    const QString cloneStore = temporary.filePath(QStringLiteral("clone-store"));
    QVERIFY(QDir().mkpath(cloneStore));
    const QString cloneRepository = cloneStore + QStringLiteral("/repository");
    result = runLore(cloneStore,
                     {QStringLiteral("--identity"), QString::fromLatin1(kIdentity),
                      QStringLiteral("clone"),
                      server.repositoryUrl(QStringLiteral("pimio-promotion")), cloneRepository},
                     300'000);
    QVERIFY2(result.succeeded, qPrintable(result.output));
    QCOMPARE(repositoryId(cloneRepository), originRepositoryId);
    QCOMPARE(fileSha256(cloneRepository + QStringLiteral("/payload.bin")), payloadHash);

    {
        LoreDurableStore origin(originStore);
        LoreDurableStore clone(cloneStore);
        Error error;
        QVERIFY2(origin.open(&error), qPrintable(error.message()));
        QVERIFY2(clone.open(&error), qPrintable(error.message()));
        const auto cloneDescriptor = clone.libraryDescriptor(&error);
        QVERIFY2(cloneDescriptor.has_value(), qPrintable(error.message()));
        QCOMPARE(cloneDescriptor->id, originDescriptor.id);
        QCOMPARE(cloneDescriptor->name, originDescriptor.name);
        QCOMPARE(clone.listIds(&error).size(), 2);
        const auto first = clone.load(MediaId(QStringLiteral("m-1")), &error);
        QVERIFY2(first.has_value(), qPrintable(error.message()));
        QCOMPARE(first->metadata.caption, QStringLiteral("offline one"));
        const QStringList originCheckpoints = checkpointIds(origin.history(-1, &error));
        const QStringList cloneCheckpoints = checkpointIds(clone.history(-1, &error));
        QEXPECT_FAIL("", "A fresh LORE 0.9 clone does not retain offline revision history.",
                     Continue);
        QCOMPARE(cloneCheckpoints, originCheckpoints);
    }

    const ProcessResult originHistory =
            runLore(originRepository,
                    {QStringLiteral("--offline"), QStringLiteral("history"),
                     QStringLiteral("--oneline")});
    const ProcessResult cloneHistory =
            runLore(cloneRepository, {QStringLiteral("history"), QStringLiteral("--oneline")});
    QVERIFY2(originHistory.succeeded, qPrintable(originHistory.output));
    QVERIFY2(cloneHistory.succeeded, qPrintable(cloneHistory.output));
    QCOMPARE(cloneHistory.output.trimmed(), originHistory.output.trimmed());

    const ProcessResult originRemote =
            runLore(originRepository,
                    {QStringLiteral("repository"), QStringLiteral("config"),
                     QStringLiteral("get"), QStringLiteral("remote_url")});
    const ProcessResult cloneRemote =
            runLore(cloneRepository,
                    {QStringLiteral("repository"), QStringLiteral("config"),
                     QStringLiteral("get"), QStringLiteral("remote_url")});
    QVERIFY2(originRemote.succeeded, qPrintable(originRemote.output));
    QVERIFY2(cloneRemote.succeeded, qPrintable(cloneRemote.output));
    QCOMPARE(originRemote.output.trimmed(), cloneRemote.output.trimmed());

    QVERIFY2(server.reset(&serverError), qPrintable(serverError));
    QVERIFY2(createRemoteRegistration(
                     temporary.filePath(QStringLiteral("interruption-registration")),
                     server.repositoryUrl(QStringLiteral("pimio-promotion")), originRepositoryId,
                     &registrationOutput),
             qPrintable(registrationOutput));

    QProcess interruptedPush;
    interruptedPush.setWorkingDirectory(originRepository);
    interruptedPush.setProcessChannelMode(QProcess::MergedChannels);
    interruptedPush.start(
            loreCliPath(),
            {QStringLiteral("--no-pager"), QStringLiteral("--non-interactive"),
             QStringLiteral("--max-connections"), QStringLiteral("1"), QStringLiteral("push")});
    QVERIFY(interruptedPush.waitForStarted(30'000));
    QString interruptedOutput;
    QElapsedTimer interruptWait;
    interruptWait.start();
    while (interruptedPush.state() != QProcess::NotRunning && interruptWait.elapsed() < 120'000
           && !interruptedOutput.contains(QStringLiteral("Pushing"))) {
        interruptedPush.waitForReadyRead(1'000);
        interruptedOutput += QString::fromLocal8Bit(interruptedPush.readAll());
    }
    QVERIFY2(interruptedOutput.contains(QStringLiteral("Pushing")),
             qPrintable(QStringLiteral("Push completed before it could be interrupted: %1")
                                .arg(interruptedOutput)));
    interruptedPush.kill();
    QVERIFY(interruptedPush.waitForFinished(30'000));
    QVERIFY(interruptedPush.exitStatus() != QProcess::NormalExit || interruptedPush.exitCode() != 0);

    {
        LoreDurableStore origin(originStore);
        Error error;
        QVERIFY2(origin.open(&error), qPrintable(error.message()));
        QVERIFY(origin.stage(makeLoreRecord(QStringLiteral("m-3"),
                                            QStringLiteral("after interrupted push")),
                             &error));
        QVERIFY2(origin.commit(QStringLiteral("Post-interruption write"), &error).has_value(),
                 qPrintable(error.message()));
    }

    result = runLore(originRepository, {QStringLiteral("push")}, 300'000);
    QVERIFY2(result.output.contains(QStringLiteral("Branch main already exists")),
             qPrintable(result.output));
    QEXPECT_FAIL("", "LORE 0.9 cannot retry after an interrupted initial push created the branch.",
                 Continue);
    QVERIFY2(result.succeeded, qPrintable(result.output));
    QCOMPARE(repositoryId(originRepository), originRepositoryId);

    LoreDurableStore readableOrigin(originStore);
    Error readError;
    QVERIFY2(readableOrigin.open(&readError), qPrintable(readError.message()));
    const auto afterInterruption =
            readableOrigin.load(MediaId(QStringLiteral("m-3")), &readError);
    QVERIFY2(afterInterruption.has_value(), qPrintable(readError.message()));
    QCOMPARE(afterInterruption->metadata.caption, QStringLiteral("after interrupted push"));

    qInfo("PROMOTION_GATE_RUNTIME_MS=%lld", runtime.elapsed());
}

void TestLoreServerPromotion::noRemoteRequiresAnUpstreamAttachOperation()
{
    PIMIO_SKIP_WITHOUT_LORE();
    PIMIO_SKIP_WITHOUT_LORE_CLI();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("no-remote-store"));
    const QString repositoryPath = storePath + QStringLiteral("/repository");
    QVERIFY(QDir().mkpath(repositoryPath));

    ProcessResult result =
            runLore(repositoryPath,
                    {QStringLiteral("--offline"), QStringLiteral("--identity"),
                     QString::fromLatin1(kIdentity), QStringLiteral("repository"),
                     QStringLiteral("create"), QStringLiteral("pimio-no-remote")});
    QVERIFY2(result.succeeded, qPrintable(result.output));
    const QString idBefore = repositoryId(repositoryPath);
    QVERIFY(!idBefore.isEmpty());

    result = runLore(repositoryPath,
                     {QStringLiteral("repository"), QStringLiteral("config"),
                      QStringLiteral("get"), QStringLiteral("remote_url")});
    QVERIFY2(result.succeeded, qPrintable(result.output));
    QVERIFY(result.output.trimmed().isEmpty());

    result = runLore(repositoryPath,
                     {QStringLiteral("repository"), QStringLiteral("config"),
                      QStringLiteral("set"), QStringLiteral("remote_url"),
                      QStringLiteral("lore://127.0.0.1:1/pimio-no-remote")});
    QVERIFY2(!result.succeeded,
             "LORE 0.9 unexpectedly exposed a public post-creation config setter.");

    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY2(store.createLibrary(QStringLiteral("No Remote Library"), &error),
             qPrintable(error.message()));
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("m-1"), QStringLiteral("still writable")),
                        &error));
    QVERIFY2(store.commit(QStringLiteral("Offline write after attach rejection"), &error).has_value(),
             qPrintable(error.message()));
    store.close();
    QCOMPARE(repositoryId(repositoryPath), idBefore);
}

QTEST_GUILESS_MAIN(TestLoreServerPromotion)

#include "tst_lore_server_promotion.moc"
