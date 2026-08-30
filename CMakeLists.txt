# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

cmake_minimum_required(VERSION 3.20)
project(StillWakesTheDeepHeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
endif()

# MinHook (for the GetPlayerViewPoint trampoline). Fetched + built locally;
# cameraunlock-core's hook manager links against this `minhook` target.
include(FetchContent)
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

# Shared cameraunlock-core (C++ side) with the hook manager and UE runtime
# helpers enabled.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/view_hook.cpp
    src/mod_hotkeys.cpp
    src/camera_boundary.cpp
    src/aim_projection.cpp
    src/reticle_mover.cpp
    src/torch_aim.cpp
    src/torch_flare.cpp
    src/widget_probe.cpp
    src/ue_vm.cpp
    src/config.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(StillWakesTheDeepHeadTracking SHARED ${MOD_SOURCES})

# The log banner names the version, so an attached HeadTracking.log says which
# build produced it. release.ps1 stamps the project version above.
target_compile_definitions(StillWakesTheDeepHeadTracking PRIVATE
    SWTD_HT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(StillWakesTheDeepHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(StillWakesTheDeepHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(StillWakesTheDeepHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Behaviour locks for the parts of the mod that are pure functions - the
# tracker-to-Unreal camera boundary and the GetPlayerViewPoint caller gate.
# A separate executable; it links none of the hook or Windows machinery, so
# `pixi run test` runs anywhere the mod builds, with no game installed.
option(SWTD_BUILD_TESTS "Build the camera-boundary behaviour tests" ON)
if(SWTD_BUILD_TESTS)
    add_executable(swtd_boundary_tests
        tests/boundary_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(swtd_boundary_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
endif()
