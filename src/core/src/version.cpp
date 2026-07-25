#include "pimio/core/version.h"

#ifndef PIMIO_VERSION_STRING
#error "PIMIO_VERSION_STRING must be defined by the build system"
#endif

namespace pimio::core {

QString versionString()
{
    return QStringLiteral(PIMIO_VERSION_STRING);
}

} // namespace pimio::core
