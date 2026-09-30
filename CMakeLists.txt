# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

cmake_minimum_required(VERSION 3.20)
project(StillWakesTheDeepHeadTracking VERSION 0.2.0 LANGUAGES C CXX)

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
    src/legacy_config/legacy_config.cpp
    src/builds/build_registry.cpp
    src/builds/runtime_discovery.cpp
    src/builds/native_thunk.cpp
    src/builds/runtime_layout.cpp
    src/builds/steam_offsets.cpp
)

add_library(StillWakesTheDeepHeadTracking SHARED ${MOD_SOURCES})

# The log banner names the version, so an attached HeadTracking.log says which
# build produced it. release.ps1 stamps the project version above.
target_compile_definitions(StillWakesTheDeepHeadTracking PRIVATE
    SWTD_HT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(StillWakesTheDeepHeadTracking PRIVATE
    ${minhook_src_SOURCE_DIR}/src/hde
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
    enable_testing()
    add_executable(swtd_discovery_bounds_tests tests/discovery_bounds_tests.cpp src/builds/runtime_discovery.cpp src/builds/native_thunk.cpp)
    target_include_directories(swtd_discovery_bounds_tests PRIVATE src cameraunlock-core/cpp/include ${minhook_src_SOURCE_DIR}/src/hde)
    target_link_libraries(swtd_discovery_bounds_tests PRIVATE minhook)
    add_test(NAME swtd_discovery_bounds_tests COMMAND swtd_discovery_bounds_tests)
    add_executable(swtd_native_thunk_tests tests/native_thunk_tests.cpp src/builds/native_thunk.cpp)
    target_include_directories(swtd_native_thunk_tests PRIVATE src ${minhook_src_SOURCE_DIR}/src/hde)
    target_link_libraries(swtd_native_thunk_tests PRIVATE minhook)
    add_test(NAME swtd_native_thunk_tests COMMAND swtd_native_thunk_tests)

    add_executable(swtd_boundary_tests
        tests/boundary_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(swtd_boundary_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
    add_test(NAME swtd_boundary_tests COMMAND swtd_boundary_tests)

    # The canonical config, the committed file and the toggles' saves. Also the
    # renderer behind `pixi run render-config` (--render-config <path>). Links
    # cameraunlock for the config owner and the file log; still no hooks and no
    # game.
    add_executable(swtd_config_tests
        tests/config_tests.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(swtd_config_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
    target_link_libraries(swtd_config_tests PRIVATE cameraunlock)
    target_compile_definitions(swtd_config_tests PRIVATE SWTD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME swtd_config_tests COMMAND swtd_config_tests)

    # The canonical config conversion's differential test: v0.2.0's reader
    # (byte copies in tests/config_differential/oracle/src) and startup code
    # against the frozen reader in src/legacy_config/, and that reader against
    # the config owner's migration into CameraUnlock.ini.
    set(SWTD_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_executable(swtd_config_differential
        ${SWTD_DIFFERENTIAL_DIR}/differential_tests.cpp
        ${SWTD_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(swtd_config_differential PRIVATE
        src
        ${SWTD_DIFFERENTIAL_DIR}
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(swtd_config_differential PRIVATE
        SWTD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(swtd_config_differential PRIVATE cameraunlock)
    if(MSVC)
        target_compile_options(swtd_config_differential PRIVATE /EHsc)
    endif()
    add_test(NAME swtd_config_differential COMMAND swtd_config_differential)

    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated\.
    add_test(NAME swtd_config_lint COMMAND node
        ${SWTD_DIFFERENTIAL_DIR}/lint-migrated.mjs
        $<TARGET_FILE_DIR:swtd_config_differential>/migrated)
    set_tests_properties(swtd_config_lint PROPERTIES DEPENDS swtd_config_differential)
    set_tests_properties(swtd_config_differential swtd_config_lint PROPERTIES LABELS differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay v0.2.0's (git show v0.2.0:src/<file>),
    # the core sources both readers compile must stay the ones v0.2.0 was built
    # with (core 0f7a634), and the frozen reader is never edited.
    function(swtd_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(SWTD_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${SWTD_DIFFERENTIAL_DIR}/oracle/src/config.cpp|6af8240a144b4cce2600114cfcb56e6fbc635529cd98254f4d04a4f5b17538f5"
            "${SWTD_DIFFERENTIAL_DIR}/oracle/src/config.h|a9d031404653d5b0626537a6822b2da76f760bc6ae7aa997d0a3f22e0ce3871e"
            "${SWTD_DIFFERENTIAL_DIR}/oracle/src/logging.h|9a547e7980620b35f634cfa7b446630ca65811dd00b9c2e2fc34daf8d5dbb1f4"
            "${SWTD_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${SWTD_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${SWTD_CORE_DIR}/include/cameraunlock/protocol/port_utils.h|91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"
            "${SWTD_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${SWTD_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|cc74b719255ccfee7e398913730b9f23b3548bcbd94871ffad0e15705a78c28c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|02e580a63152e6c03d0eed95796acbdbb32c342aaecf65f65d2247bd026a5c20")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        swtd_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()
endif()
