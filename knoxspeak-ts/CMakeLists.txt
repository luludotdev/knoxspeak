cmake_minimum_required(VERSION 3.28)

project(knoxspeak-ts
    VERSION 0.1.1
    DESCRIPTION "make the zomboid voip not suck with the power of TeamSpeak"
    LANGUAGES CXX)

set(KNOXSPEAK_AUTHOR "lulu.dev")

if(NOT WIN32)
    message(FATAL_ERROR "knoxspeak-ts currently targets Windows only (the IPC server uses Winsock).")
endif()

option(KNOXSPEAK_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static CRT so users don't need the VC++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(KNOXSPEAK_GIT_HASH "" CACHE STRING "Commit hash to embed in the version (default: git rev-parse HEAD)")
set(_knox_hash "${KNOXSPEAK_GIT_HASH}")
if(NOT _knox_hash)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _knox_hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _knox_git_result)
        if(NOT _knox_git_result EQUAL 0)
            set(_knox_hash "")
        endif()
    endif()
    # Re-run configure when the checked-out commit changes so the hash stays current.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.git/HEAD")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../.git/HEAD")
    endif()
endif()
if(_knox_hash)
    string(SUBSTRING "${_knox_hash}" 0 12 _knox_hash)
    set(KNOXSPEAK_VERSION_FULL "${PROJECT_VERSION}+g${_knox_hash}")
else()
    set(KNOXSPEAK_VERSION_FULL "${PROJECT_VERSION}+unknown")
endif()
message(STATUS "knoxspeak-ts version: ${KNOXSPEAK_VERSION_FULL}")

include(FetchContent)

# TeamSpeak 3 client plugin SDK
FetchContent_Declare(ts3sdk
    GIT_REPOSITORY https://github.com/TeamSpeak-Systems/ts3client-pluginsdk.git
    GIT_TAG        b2f4b8e574d13c584ba8cae0c5ab6e1a0c3048e6 # tag 26
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(ts3sdk)

add_library(ts3sdk INTERFACE)
target_include_directories(ts3sdk SYSTEM INTERFACE "${ts3sdk_SOURCE_DIR}/include")

add_library(knoxspeak_warnings INTERFACE)
target_compile_options(knoxspeak_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)
target_compile_definitions(knoxspeak_warnings INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(knoxspeak_core STATIC
    src/core/protocol.cpp
    src/core/protocol.hpp
    src/core/mesh.cpp
    src/core/mesh.hpp
    src/core/info_text.cpp
    src/core/info_text.hpp
    src/core/dsp.hpp)
target_include_directories(knoxspeak_core PUBLIC src)
target_link_libraries(knoxspeak_core PRIVATE knoxspeak_warnings)

add_library(knoxspeak_ts SHARED
    src/plugin/exports.cpp
    src/plugin/plugin.cpp
    src/plugin/plugin.hpp
    src/plugin/ipc_server.cpp
    src/plugin/ipc_server.hpp
    src/plugin/ui_dispatcher.cpp
    src/plugin/ui_dispatcher.hpp
    src/plugin/client_registry.hpp
    src/plugin/voice_snapshot.hpp)
target_link_libraries(knoxspeak_ts PRIVATE knoxspeak_core ts3sdk knoxspeak_warnings ws2_32)
target_compile_definitions(knoxspeak_ts PRIVATE
    KNOXSPEAK_VERSION="${KNOXSPEAK_VERSION_FULL}"
    KNOXSPEAK_AUTHOR="${KNOXSPEAK_AUTHOR}"
    KNOXSPEAK_DESCRIPTION="${PROJECT_DESCRIPTION}")
set_target_properties(knoxspeak_ts PROPERTIES
    OUTPUT_NAME "knoxspeak_ts_win64"
    PREFIX "")
target_compile_options(knoxspeak_ts PRIVATE $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/Z7>)
target_link_options(knoxspeak_ts PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/DEBUG /OPT:REF /OPT:ICF>)

set(KNOXSPEAK_PKG_DIR "${CMAKE_BINARY_DIR}/package")
set(KNOXSPEAK_PKG_FILE "${CMAKE_BINARY_DIR}/knoxspeak.ts3_plugin")
configure_file(package/package.ini.in "${KNOXSPEAK_PKG_DIR}/package.ini" @ONLY)
add_custom_target(package_ts3
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KNOXSPEAK_PKG_DIR}/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:knoxspeak_ts>" "${KNOXSPEAK_PKG_DIR}/plugins/"
    COMMAND ${CMAKE_COMMAND} -E chdir "${KNOXSPEAK_PKG_DIR}"
            ${CMAKE_COMMAND} -E tar cf "${KNOXSPEAK_PKG_FILE}" --format=zip package.ini plugins
    DEPENDS knoxspeak_ts
    COMMENT "Packaging knoxspeak.ts3_plugin")

if(KNOXSPEAK_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(Catch2)

    add_executable(knoxspeak_tests
        tests/test_protocol.cpp
        tests/test_mesh.cpp
        tests/test_info_text.cpp
        tests/test_dsp.cpp
        tests/test_ui_dispatcher.cpp
        src/plugin/ui_dispatcher.cpp)
    target_link_libraries(knoxspeak_tests PRIVATE knoxspeak_core knoxspeak_warnings Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(knoxspeak_tests)
endif()
