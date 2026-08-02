qt_add_library(pimio_watch STATIC
    include/pimio/watch/event_coalescer.h
    include/pimio/watch/qt_directory_watch_adapter.h
    include/pimio/watch/reconcile_worker.h
    include/pimio/watch/watch_adapter.h
    include/pimio/watch/watch_event.h
    include/pimio/watch/watch_service.h
    src/event_coalescer.cpp
    src/qt_directory_watch_adapter.cpp
    src/reconcile_worker.cpp
    src/watch_adapter.cpp
    src/watch_event.cpp
    src/watch_service.cpp
)

add_library(pimio::watch ALIAS pimio_watch)

target_include_directories(pimio_watch PUBLIC include)

target_link_libraries(pimio_watch PUBLIC
    pimio::core
    pimio::projection
    pimio::scan
    Qt6::Core
)
