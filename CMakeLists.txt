cmake_minimum_required(VERSION 3.20)
project(DadsIPodTransferator9000 VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGPOD REQUIRED IMPORTED_TARGET libgpod-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)

# GUI process deliberately does NOT link libgpod/GLib/TagLib. Old libgpod is
# isolated in a short-lived helper process so allocator bugs cannot take down Qt.
add_executable(dads-ipod-transferator-9000
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/IpodBackend.cpp
    src/IpodBackend.h
)

target_link_libraries(dads-ipod-transferator-9000 PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
)

target_compile_options(dads-ipod-transferator-9000 PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(dads-ipod-transferator-9000-helper
    src/IpodHelper.cpp
)

target_link_libraries(dads-ipod-transferator-9000-helper PRIVATE
    PkgConfig::LIBGPOD
    PkgConfig::TAGLIB
    PkgConfig::GLIB
)

target_compile_options(dads-ipod-transferator-9000-helper PRIVATE
    -Wall -Wextra -Wpedantic
)

install(TARGETS
    dads-ipod-transferator-9000
    dads-ipod-transferator-9000-helper
    RUNTIME DESTINATION bin
)
install(FILES data/dads-ipod-transferator-9000.desktop DESTINATION share/applications)
