cmake_minimum_required(VERSION 3.10)
project(SauronEye)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add debug symbols and disable optimizations for debugging
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -Wextra -fstack-protector-all")

# Enable generation of compile_commands.json for IDE includePath resolution
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Make sure core dumps are enabled
message(STATUS "Note: Enable core dumps with 'ulimit -c unlimited' before running")

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

# Add JPEG library support
pkg_check_modules(JPEG REQUIRED libjpeg)

# Find Mosquitto - Use a more robust approach
# First check if pkg-config can find it
pkg_check_modules(MOSQUITTO libmosquitto)

if(NOT MOSQUITTO_FOUND)
    # If pkg-config fails, try to find the library directly
    find_library(MOSQUITTO_LIBRARY mosquitto)
    if(MOSQUITTO_LIBRARY)
        set(MOSQUITTO_LIBRARIES ${MOSQUITTO_LIBRARY})
        find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h
            PATH_SUFFIXES mosquitto
        )
        if(MOSQUITTO_INCLUDE_DIR)
            set(MOSQUITTO_INCLUDE_DIRS ${MOSQUITTO_INCLUDE_DIR})
            set(MOSQUITTO_FOUND TRUE)
        endif()
    endif()
endif()

if(NOT MOSQUITTO_FOUND)
    message(FATAL_ERROR "Could not find Mosquitto library. Please install libmosquitto-dev package.")
endif()

# Find OpenSSL for Base64 encoding
find_package(OpenSSL REQUIRED)

message(STATUS "Found Mosquitto: ${MOSQUITTO_LIBRARIES}")
message(STATUS "Found OpenSSL: ${OPENSSL_LIBRARIES}")

# Set include directories for all targets
include_directories(
    ${X11_INCLUDE_DIR}
    ${XRANDR_INCLUDE_DIRS}
    ${GLIBMM_INCLUDE_DIRS}
    ${GIOMM_INCLUDE_DIRS}
    ${GTKMM_INCLUDE_DIRS}
    ${UUID_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${XCOMPOSITE_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
    ${MOSQUITTO_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

link_directories(
    ${XRANDR_LIBRARY_DIRS}
    ${GLIBMM_LIBRARY_DIRS}
    ${GIOMM_LIBRARY_DIRS}
    ${GTKMM_LIBRARY_DIRS}
    ${UUID_LIBRARY_DIRS}
    ${CAIRO_LIBRARY_DIRS}
    ${XCOMPOSITE_LIBRARY_DIRS}
    ${JPEG_LIBRARY_DIRS}
    ${MOSQUITTO_LIBRARY_DIRS}
)

# Define our source files grouped by module
set(CAPTURE_SOURCES
    src/capture/X11ScreenCapturer.cpp
)

set(MQTT_SOURCES
    src/mqtt/MqttClient.cpp
)

set(INPUT_SOURCES
    src/input/KeyboardController.cpp
)

set(UI_SOURCES
    src/ui/SauronWindow.cpp
    src/ui/RecentCapturesPanel.cpp
    src/ui/SauronEyePanel.cpp
)

set(MAIN_SOURCES
    src/Main.cpp
)

# Create the executable with all source files
add_executable(sauron
    ${MAIN_SOURCES}
    ${CAPTURE_SOURCES}
    ${MQTT_SOURCES}
    ${INPUT_SOURCES}
    ${UI_SOURCES}
)

# Rename from 'pipewrench' to 'sauron' as mentioned in TODO.txt
set_target_properties(sauron PROPERTIES OUTPUT_NAME "sauron")

target_link_libraries(sauron
    ${X11_LIBRARIES}
    ${XRANDR_LIBRARIES}
    ${GLIBMM_LIBRARIES}
    ${GIOMM_LIBRARIES}
    ${GTKMM_LIBRARIES}
    ${UUID_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${XCOMPOSITE_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${MOSQUITTO_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    -lmosquitto
    -lcrypto
)

# Install target
install(TARGETS sauron DESTINATION bin)

# Create a captures directory in build dir if it doesn't exist
add_custom_command(
    TARGET sauron POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/captures
    COMMENT "Creating captures directory"
)

# Output build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Executable name: sauron")
message(STATUS "C++ Flags: ${CMAKE_CXX_FLAGS}")

