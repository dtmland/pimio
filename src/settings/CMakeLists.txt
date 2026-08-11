qt_add_library(pimio_settings STATIC
    include/pimio/settings/settings.h
    src/settings.cpp
)

add_library(pimio::settings ALIAS pimio_settings)

target_include_directories(pimio_settings PUBLIC include)

# Settings are UI-independent: QML is one way to change them, not the only
# one, so this component links no GUI module.
target_link_libraries(pimio_settings PUBLIC
    Qt6::Core
)
