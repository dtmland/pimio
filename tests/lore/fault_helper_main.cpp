// Fault-injection helper for the Increment 2c gate.
//
// A crash has to be a real process death for the recovery evidence to mean
// anything, so the dangerous work happens here and the test observes the
// aftermath from a surviving process.
//
// Usage: pimio_lore_fault_helper <mode> <store-path> [count]
//
//   crash-before-commit   stage records, copy them into the checkout exactly
//                         as commit() does, then die before LORE commits.
//                         This reproduces the interrupted-commit window
//                         deterministically instead of relying on timing.
//   crash-during-commit   stage records and abort from a watchdog thread while
//                         LORE is committing. The delay selects which part of
//                         the commit is interrupted.
//   commit                stage and commit records, reporting success on
//                         stdout. Used as a concurrent second writer.

#include "pimio/core/durable_store.h"
#include "pimio/lore/lore_durable_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>

#include <cstdlib>

namespace {

pimio::core::MediaRecord makeRecord(const QString &id)
{
    pimio::core::MediaRecord record;
    record.id = pimio::core::MediaId(id);
    record.fingerprint = pimio::core::ContentFingerprint(QStringLiteral("sha256"), id);
    record.metadata.kind = pimio::core::MediaKind::Image;
    record.metadata.caption = QStringLiteral("helper %1").arg(id);
    return record;
}

/// Repeats the checkout copy that commit() performs, so the process can be
/// killed in exactly the state an interrupted commit leaves behind.
bool copyStagingIntoCheckout(const QString &storePath)
{
    const QString staging = storePath + QStringLiteral("/staging");
    const QString records = storePath + QStringLiteral("/repository/records");
    const QDir stagingDir(staging);

    QDirIterator iterator(staging, QStringList{QStringLiteral("*.json")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString source = iterator.next();
        const QString target = records + QLatin1Char('/') + stagingDir.relativeFilePath(source);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            return false;
        }
        QFile::remove(target);
        if (!QFile::copy(source, target)) {
            return false;
        }
    }
    return true;
}

class Watchdog : public QThread
{
public:
    explicit Watchdog(int delayMs)
        : m_delayMs(delayMs)
    {
    }

protected:
    void run() override
    {
        QThread::msleep(static_cast<unsigned long>(m_delayMs));
        // _Exit, not abort: no destructors, no flushing, nothing that would
        // give the adapter a chance to tidy up.
        std::_Exit(9);
    }

private:
    int m_delayMs;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    QTextStream out(stdout);

    if (arguments.size() < 3) {
        out << QStringLiteral("usage: pimio_lore_fault_helper <mode> <store-path> [count]\n");
        return 2;
    }

    const QString mode = arguments.at(1);
    const QString storePath = arguments.at(2);
    const int count = arguments.size() > 3 ? arguments.at(3).toInt() : 3;
    const QString prefix = arguments.size() > 4 ? arguments.at(4) : QStringLiteral("helper");
    const int delayMs = arguments.size() > 5 ? arguments.at(5).toInt() : 30;

    pimio::lore::LoreDurableStore store(storePath);
    pimio::core::Error error;
    if (!store.open(&error)) {
        out << QStringLiteral("open failed: %1\n").arg(error.message());
        return 3;
    }

    for (int index = 0; index < count; ++index) {
        const QString id = QStringLiteral("%1-%2").arg(prefix).arg(index, 4, 10, QLatin1Char('0'));
        if (!store.stage(makeRecord(id), &error)) {
            out << QStringLiteral("stage failed: %1\n").arg(error.message());
            return 4;
        }
    }

    if (mode == QLatin1String("crash-before-commit")) {
        if (!copyStagingIntoCheckout(storePath)) {
            out << QStringLiteral("copy failed\n");
            return 5;
        }
        out << QStringLiteral("checkout populated, dying\n");
        out.flush();
        std::_Exit(9);
    }

    if (mode == QLatin1String("crash-during-commit")) {
        auto *watchdog = new Watchdog(delayMs);
        watchdog->start();
        store.commit(QStringLiteral("Interrupted commit"), &error);
        // Reaching this line means the commit finished before the watchdog
        // fired. That is a legitimate outcome; the test tolerates both.
        out << QStringLiteral("commit completed before the watchdog fired\n");
        out.flush();
        std::_Exit(0);
    }

    if (mode == QLatin1String("commit")) {
        const auto checkpoint = store.commit(QStringLiteral("Helper commit"), &error);
        if (!checkpoint) {
            out << QStringLiteral("commit failed: %1\n").arg(error.message());
            return 6;
        }
        out << QStringLiteral("committed %1\n").arg(checkpoint->id);
        store.close();
        return 0;
    }

    out << QStringLiteral("unknown mode %1\n").arg(mode);
    return 2;
}
