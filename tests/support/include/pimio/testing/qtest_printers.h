#pragma once

#include "pimio/core/edit_recipe.h"
#include "pimio/core/error.h"
#include "pimio/core/job.h"
#include "pimio/core/metadata.h"
#include "pimio/core/types.h"

#include <QTest>

/// QCOMPARE for the core's scoped enumerations.
///
/// QTest cannot format a scoped enum that is not registered with the meta
/// object system, so comparing the names keeps failures readable without
/// forcing the UI-independent core to depend on moc.
#define PIMIO_COMPARE_ENUM(actual, expected)                                                      \
    QCOMPARE(pimio::core::toString(actual), pimio::core::toString(expected))

/// QCOMPARE for identifier value types.
#define PIMIO_COMPARE_ID(actual, expected) QCOMPARE((actual).value(), (expected).value())
