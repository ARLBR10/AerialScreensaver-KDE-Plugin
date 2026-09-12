cmake_minimum_required(VERSION 3.20)

project(plasma-aerial-wallpaper VERSION 0.1.0 LANGUAGES CXX)

option(AERIAL_ALLOW_INSECURE_TLS "Allow Apple HTTPS requests without certificate verification (unsafe)" OFF)
option(AERIAL_USE_SCCACHE "Use sccache when available" ON)

if(AERIAL_ALLOW_INSECURE_TLS)
    message(WARNING "AERIAL_ALLOW_INSECURE_TLS is enabled. Network content can be intercepted or replaced.")
endif()

if(AERIAL_USE_SCCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(SCCACHE_EXECUTABLE sccache)
    if(SCCACHE_EXECUTABLE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_EXECUTABLE}")
        message(STATUS "Using sccache: ${SCCACHE_EXECUTABLE}")
    else()
        message(STATUS "sccache was not found; building without a compiler cache")
    endif()
endif()

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(CTest)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Concurrent Core Network Qml)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Archive)
find_package(Plasma ${KF_MIN_VERSION} REQUIRED)

plasma_install_package(package org.kde.plasma.aerial wallpapers wallpaper)
add_subdirectory(plugin)

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
