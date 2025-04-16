cmake_minimum_required(VERSION 3.10)
project(PipeWrench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add X11 libraries first to ensure proper linking order
find_package(X11 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIBMM REQUIRED glibmm-2.4)
pkg_check_modules(GIOMM REQUIRED giomm-2.4)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)
pkg_check_modules(UUID REQUIRED uuid)
pkg_check_modules(XRANDR REQUIRED xrandr)

# Add Cairo for image saving
pkg_check_modules(CAIRO REQUIRED cairo)

# Add XComposite for better window capture
pkg_check_modules(XCOMPOSITE REQUIRED xcomposite)

include_directories(
    ${X11_INCLUDE_DIR}
    ${XRANDR_INCLUDE_DIRS}
    ${GLIBMM_INCLUDE_DIRS}
    ${GIOMM_INCLUDE_DIRS}
    ${GTKMM_INCLUDE_DIRS}
    ${UUID_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${XCOMPOSITE_INCLUDE_DIRS}
)

link_directories(
    ${XRANDR_LIBRARY_DIRS}
    ${GLIBMM_LIBRARY_DIRS}
    ${GIOMM_LIBRARY_DIRS}
    ${GTKMM_LIBRARY_DIRS}
    ${UUID_LIBRARY_DIRS}
    ${CAIRO_LIBRARY_DIRS}
    ${XCOMPOSITE_LIBRARY_DIRS}
)

add_executable(pipewrench
    pipewrench.cpp
)

target_link_libraries(pipewrench
    ${X11_LIBRARIES}
    ${XRANDR_LIBRARIES}
    ${GLIBMM_LIBRARIES}
    ${GIOMM_LIBRARIES}
    ${GTKMM_LIBRARIES}
    ${UUID_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${XCOMPOSITE_LIBRARIES}
)

