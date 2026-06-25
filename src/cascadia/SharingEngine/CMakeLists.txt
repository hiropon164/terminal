# Copyright (c) Microsoft Corporation.
# Licensed under the MIT license.
#
# Standalone, cross-platform build for the OS-independent pane-sharing core.
#
# This builds WITHOUT the Windows Terminal solution and without MSBuild. The
# library here is pure C++17 (no Windows / WinRT) and compiles on Linux, macOS
# and Windows with any conforming compiler. The Windows-only WinRT transport
# (SharingTransportServer/Client) is intentionally NOT part of this target -- a
# non-Windows host supplies its own socket transport around this core.
#
#   cmake -S . -B build -G Ninja
#   cmake --build build
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.16)

project(panesharing
    VERSION 1.0.0
    DESCRIPTION "OS-independent core for terminal pane sharing (protocol, WebSocket, authorization)"
    LANGUAGES CXX)

# --- the library --------------------------------------------------------------

add_library(panesharing STATIC
    Protocol.cpp
    WebSocket.cpp
    Server.cpp
    Client.cpp
)
add_library(panesharing::panesharing ALIAS panesharing)

target_include_directories(panesharing PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/panesharing>
)

target_compile_features(panesharing PUBLIC cxx_std_17)
set_target_properties(panesharing PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(panesharing PRIVATE /W4 /permissive-)
else()
    target_compile_options(panesharing PRIVATE -Wall -Wextra)
endif()

# --- tests --------------------------------------------------------------------

option(PANESHARING_BUILD_TESTS "Build the SharingEngine unit tests" ON)

if(PANESHARING_BUILD_TESTS)
    enable_testing()
    # Each suite is a self-contained main() that links the core library.
    foreach(suite Protocol WebSocket Orchestration)
        add_executable(${suite}_test test/${suite}.test.cpp)
        target_link_libraries(${suite}_test PRIVATE panesharing)
        add_test(NAME ${suite} COMMAND ${suite}_test)
    endforeach()
endif()

# --- install (so it can be embedded by, e.g., a macOS host) -------------------

include(GNUInstallDirs)
install(TARGETS panesharing
    EXPORT panesharingTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
    Protocol.h
    WebSocket.h
    Server.h
    Client.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/panesharing)
