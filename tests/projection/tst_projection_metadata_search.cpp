#include "tst_projection_metadata_fixture.h"

using namespace pimio;
using namespace pimio::projection;

void TestProjectionMetadata::fullTextSearchMatchesCaption()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("photo.jpg");
        r.metadata.caption = QStringLiteral("Golden Gate sunset");
        const core::MediaId target = addRecord(store, r);
        addRecord(store, makeRecord("other.jpg")); // no match

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found = db.searchText(QStringLiteral("golden"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

void TestProjectionMetadata::fullTextSearchMatchesFileName()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const core::MediaId target = addRecord(store, makeRecord("DSC_0042.jpg"));
        addRecord(store, makeRecord("holiday.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found = db.searchText(QStringLiteral("DSC_0042"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

void TestProjectionMetadata::fullTextSearchUnicodeCaption()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("tokyo.jpg");
        r.metadata.caption = QStringLiteral("東京タワー"); // Tokyo Tower
        const core::MediaId target = addRecord(store, r);
        addRecord(store, makeRecord("paris.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        // unicode61 breaks on character category, so an unbroken CJK run is a
        // single token: "東京タワー" is not split into "東京" and "タワー".
        // searchText compensates with a prefix query, so a leading substring
        // still finds the record.
        const QList<core::MediaId> found = db.searchText(QStringLiteral("東京"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

void TestProjectionMetadata::fullTextSearchEmptyQueryReturnsEmpty()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);
        addRecord(store, makeRecord("photo.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        QVERIFY(db.searchText(QStringLiteral(""), &err).isEmpty());
        QVERIFY(db.searchText(QStringLiteral("   "), &err).isEmpty());
    }

void TestProjectionMetadata::fullTextSearchNoMatchReturnsEmpty()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);
        addRecord(store, makeRecord("photo.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found = db.searchText(QStringLiteral("zzznomatch"), &err);
        QVERIFY(!err.isError());
        QVERIFY(found.isEmpty());
    }

void TestProjectionMetadata::fullTextSearchTreatsOperatorCharactersAsText()
    {
        // FTS5 reads its own operators in a bare match string, so text a user
        // is entitled to type must not reach it unquoted. None of these may
        // report an error, however few rows they find.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("photo.jpg");
        r.metadata.caption = QStringLiteral("Golden Gate sunset");
        addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        const QStringList hostile{
            QStringLiteral("AND"),          QStringLiteral("OR"),
            QStringLiteral("NOT"),          QStringLiteral("NEAR"),
            QStringLiteral("gate:sunset"),  QStringLiteral("foo(bar"),
            QStringLiteral("-golden"),      QStringLiteral("say \"hello\""),
            QStringLiteral("*"),            QStringLiteral("^caption"),
        };
        for (const QString &query : hostile) {
            core::Error err;
            db.searchText(query, &err);
            QVERIFY2(!err.isError(),
                     qPrintable(QStringLiteral("query %1 reported: %2")
                                    .arg(query, err.message())));
        }
    }

void TestProjectionMetadata::fullTextSearchEscapesEmbeddedQuotes()
    {
        // A double quote closes a phrase, so it must be doubled to stay
        // literal. Getting that wrong silently changes which rows match
        // rather than raising an error, so the match itself is asserted.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("quoted.jpg");
        r.metadata.caption = QStringLiteral("say \"hello\" loud");
        const core::MediaId target = addRecord(store, r);
        addRecord(store, makeRecord("other.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found =
            db.searchText(QStringLiteral("\"hello"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

void TestProjectionMetadata::fullTextSearchMatchesAllTerms()
    {
        // Several terms narrow the result rather than widening it.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord both = makeRecord("both.jpg");
        both.metadata.caption = QStringLiteral("Golden Gate sunset");
        const core::MediaId target = addRecord(store, both);

        core::MediaRecord one = makeRecord("one.jpg");
        one.metadata.caption = QStringLiteral("Golden retriever");
        addRecord(store, one);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found =
            db.searchText(QStringLiteral("golden gate"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

