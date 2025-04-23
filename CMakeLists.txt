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

# Add SQLite3 for database support
# pkg_check_modules(SQLITE3 REQUIRED sqlite3) # Use find_package instead
find_package(SQLite3 REQUIRED)

# Add JSON library for message format
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via CMake, checking pkg-config")
    pkg_check_modules(NLOHMANN_JSON QUIET nlohmann_json)
    if(NOT NLOHMANN_JSON_FOUND)
        message(STATUS "nlohmann_json not found, will download and use the header-only version")
        include(FetchContent)
        FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
        FetchContent_MakeAvailable(json)
    endif()
endif()

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

# Find CURL for HTTP requests
find_package(CURL REQUIRED)

message(STATUS "Found Mosquitto: ${MOSQUITTO_LIBRARIES}")
message(STATUS "Found OpenSSL: ${OPENSSL_LIBRARIES}")
message(STATUS "Found CURL: ${CURL_LIBRARIES}")

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
    ${SQLite3_INCLUDE_DIRS} # Add SQLite3 include dir from find_package
    ${MOSQUITTO_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

link_directories(${MOSQUITTO_LIBRARY_DIRS})

# Add nlohmann/json using FetchContent
include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # Or any other desired version
)
FetchContent_MakeAvailable(json)
# Ensure the include directory is available
# Note: FetchContent_MakeAvailable *should* handle this, but being explicit can help.
# The nlohmann_json target created by FetchContent is an INTERFACE library,
# so linking against it (as done below) is the modern CMake way to get includes.
# Explicitly adding include_directories(${json_SOURCE_DIR}/include) might also work if needed.

# Include directory for our project headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

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
    src/ui/ChatPanel.cpp
)

set(MAIN_SOURCES
    src/Main.cpp
)

set(AGENT_SOURCES
    src/agent/SauronAgent.cpp
    src/agent/Main.cpp
    src/agent/AIBackend.cpp
    src/agent/OpenAIBackend.cpp
    src/agent/OllamaBackend.cpp
)

# Create the main executable with all source files
add_executable(sauron
    ${MAIN_SOURCES}
    ${CAPTURE_SOURCES}
    ${MQTT_SOURCES}
    ${INPUT_SOURCES}
    ${UI_SOURCES}
)

# Rename from 'pipewrench' to 'sauron' as mentioned in TODO.txt
set_target_properties(sauron PROPERTIES OUTPUT_NAME "sauron")

# Create the agent executable
add_executable(sauron_agent
    ${AGENT_SOURCES}
    ${MQTT_SOURCES}
)

# Set properties for sauron_agent
set_target_properties(sauron_agent PROPERTIES OUTPUT_NAME "sauron_agent")

target_link_libraries(sauron PUBLIC
    ${GTKMM_LIBRARIES}
    ${XCOMPOSITE_LIBRARIES}
    ${XRANDR_LIBRARIES} # Add Xrandr library
    # ${SQLite3_LIBRARIES} # Use imported target
    SQLite::SQLite3 # Link SQLite3 using imported target
    ${CURL_LIBRARIES}
    ${MOSQUITTO_LIBRARIES}
    # Use modern OpenSSL targets instead of the variable
    OpenSSL::SSL
    OpenSSL::Crypto
    # Add nlohmann_json::nlohmann_json here
    # Linking PUBLICLY ensures include directories are propagated
    nlohmann_json::nlohmann_json
    ${X11_LIBRARIES} # Link X11 last
)

# Add executable for the agent
target_link_libraries(sauron_agent PUBLIC
    ${GTKMM_LIBRARIES} # For UI elements in agent settings
    # ${SQLite3_LIBRARIES} # Use imported target
    SQLite::SQLite3 # Link SQLite3 using imported target
    ${CURL_LIBRARIES}
    ${MOSQUITTO_LIBRARIES}
    # Use modern OpenSSL targets instead of the variable
    OpenSSL::SSL
    OpenSSL::Crypto
    # Add nlohmann_json::nlohmann_json here too
    nlohmann_json::nlohmann_json
)

# Install targets (optional)
install(TARGETS sauron sauron_agent DESTINATION bin)

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

