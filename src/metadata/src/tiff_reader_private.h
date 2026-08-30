#pragma once

#include "field_set.h"

#include <QByteArray>

namespace pimio::metadata::detail {

void applyPrimaryTiffDimensions(const QByteArray &bytes, FieldSet *fields);

} // namespace pimio::metadata::detail
