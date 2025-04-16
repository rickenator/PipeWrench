cmake_minimum_required(VERSION 3.10)
project(PipeWrench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIBMM REQUIRED glibmm-2.4)
pkg_check_modules(GIOMM REQUIRED giomm-2.4)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
pkg_check_modules(UUID REQUIRED uuid)

include_directories(
    ${GLIBMM_INCLUDE_DIRS}
    ${GIOMM_INCLUDE_DIRS}
    ${GTKMM_INCLUDE_DIRS}
    ${UUID_INCLUDE_DIRS}
)

link_directories(
    ${GLIBMM_LIBRARY_DIRS}
    ${GIOMM_LIBRARY_DIRS}
    ${GTKMM_LIBRARY_DIRS}
    ${UUID_LIBRARY_DIRS}
)

add_executable(pipewrench
    pipewrench.cpp
)

# Use explicit library names for X11
target_link_libraries(pipewrench
    ${GLIBMM_LIBRARIES}
    ${GIOMM_LIBRARIES}
    ${GTKMM_LIBRARIES}
    ${UUID_LIBRARIES}
    /usr/lib/x86_64-linux-gnu/libX11.so
    /usr/lib/x86_64-linux-gnu/libXrandr.so
)

