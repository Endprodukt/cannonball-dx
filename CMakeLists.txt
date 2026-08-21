# ----------------------------------------------------------------------------------
# Consolidated CannonBall-SE CMake File
#
# For CannonBall-SE - https://github.com/J1mbo/cannonball
# Copyright (c) James Pearce, 2025
#
# CannonBall is Copyright (c) Chris White, see: https://github.com/djyt/cannonball
#
# Tested on Linux (Ubuntu on Intel and Raspbian) and Windows 11
# ----------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)
project(cannonball-se)
include(GNUInstallDirs)
include(FetchContent)

# --- Compiler version checks ------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
    message(FATAL_ERROR "C++23 support requires GCC 12+")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR "C++23 support requires Clang 15+")
elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.36)
    message(FATAL_ERROR "C++23 support requires MSVC 19.36+ (Visual Studio 2022 17.6+)")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# --- Build verbosity controls --------------------------------------------------
option(QUIET_BUILD "Reduce build output & warning noise (recommended for users)" ON)
option(SUPER_QUIET "Silence *all* compiler warnings (-w) in Release-like configs" OFF)

# Keep build logs tidy (affects Makefiles/Unix Makefiles; Ninja is already terse)
set(CMAKE_VERBOSE_MAKEFILE OFF CACHE BOOL "Do not echo full compile/link commands")
if(QUIET_BUILD)
  # Hide per-target boilerplate like "Building CXX object ..."
  set(CMAKE_RULE_MESSAGES OFF CACHE BOOL "Suppress CMake rule messages" FORCE)
endif()

# Default to Release if nothing is specified (single-config generators)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type specified; defaulting to Release")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
               "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# For multi-config generators (Visual Studio / Xcode)
if(CMAKE_CONFIGURATION_TYPES)
  # Generate these configs; put Release first
  set(CMAKE_CONFIGURATION_TYPES "Release;RelWithDebInfo;MinSizeRel;Debug"
      CACHE STRING "" FORCE)
  # Make 'Release' the default for `cmake --install` when --config is omitted
  set(CMAKE_INSTALL_CONFIG_NAME Release CACHE STRING "" FORCE)
endif()

# --- Optimisation level --------------------------------------------------------
option(WITH_MARCH_NATIVE "Build with -march=native" ON)

# --- Profiling toggles ---------------------------------------------------------
option(ENABLE_PROFILING        "Turn on common profiling flags" OFF)
option(PROFILE_WITH_PERF       "Tweak flags for Linux perf (symbols + frame pointers)" OFF)
option(PROFILE_WITH_GPROF      "Build with -pg for gprof" OFF)
option(PROFILE_WITH_CALLGRIND  "Reduce inlining for cleaner callgrind graphs" OFF)

# --- Pixel accuracy mode ---
option(PIXEL_ACCURACY "Enable pixel-accurate sprite rendering" ON)

# --- Option for MP3 playback ---
option(WITH_MP3 "Enable MP3 playback via mpg123" ON)

# --- Option to install man page ---
option(INSTALL_MANPAGE "Install cannonball-se man page" ON)

# --- Source directory base ---
set(main_base cannonball-se)
set(main_cpp_base src/main)

# -----------------------------------------------------------------------------
# Optional pixel scaler source used by the Enhancements menu.
# Pull the HqMAME-derived implementations from a pinned Mesen2 snapshot and
# strip Mesen's project-specific precompiled-header include before compiling.
# -----------------------------------------------------------------------------
FetchContent_Declare(
    cannonball_scalers
    GIT_REPOSITORY https://github.com/SourMesen/Mesen2.git
    GIT_TAG b9fa69ddc6d0a331fb103fdb5eef6904305703c2
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(cannonball_scalers)
if(NOT cannonball_scalers_POPULATED)
    FetchContent_Populate(cannonball_scalers)
endif()

set(CB_SCALER_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_scalers")
file(MAKE_DIRECTORY "${CB_SCALER_GEN_DIR}/xbrz")
file(MAKE_DIRECTORY "${CB_SCALER_GEN_DIR}/hqx")

function(cb_prepare_scaler_file source_subdir filename output_subdir)
    file(READ
        "${cannonball_scalers_SOURCE_DIR}/${source_subdir}/${filename}"
        _cb_scaler_content)
    string(REPLACE "#include \"../pch.h\"\r\n" "" _cb_scaler_content "${_cb_scaler_content}")
    string(REPLACE "#include \"../pch.h\"\n" "" _cb_scaler_content "${_cb_scaler_content}")
    file(WRITE
        "${CB_SCALER_GEN_DIR}/${output_subdir}/${filename}"
        "${_cb_scaler_content}")
endfunction()

cb_prepare_scaler_file("Utilities/xBRZ" "config.h" "xbrz")
cb_prepare_scaler_file("Utilities/xBRZ" "xbrz.h" "xbrz")
cb_prepare_scaler_file("Utilities/xBRZ" "xbrz.cpp" "xbrz")
cb_prepare_scaler_file("Utilities/HQX" "common.h" "hqx")
cb_prepare_scaler_file("Utilities/HQX" "hqx.h" "hqx")
cb_prepare_scaler_file("Utilities/HQX" "init.cpp" "hqx")
cb_prepare_scaler_file("Utilities/HQX" "hq2x.cpp" "hqx")
cb_prepare_scaler_file("Utilities/HQX" "hq3x.cpp" "hqx")
cb_prepare_scaler_file("Utilities/HQX" "hq4x.cpp" "hqx")

set(src_scalers
    "${CB_SCALER_GEN_DIR}/xbrz/xbrz.cpp"
    "${CB_SCALER_GEN_DIR}/hqx/init.cpp"
    "${CB_SCALER_GEN_DIR}/hqx/hq2x.cpp"
    "${CB_SCALER_GEN_DIR}/hqx/hq3x.cpp"
    "${CB_SCALER_GEN_DIR}/hqx/hq4x.cpp"
)

# -----------------------------------------------------------------------------
# Project Source Files
# -----------------------------------------------------------------------------

set(src_main
    "${main_cpp_base}/globals.hpp"
    "${main_cpp_base}/romloader.hpp"
    "${main_cpp_base}/roms.hpp"
    "${main_cpp_base}/trackloader.hpp"
    "${main_cpp_base}/stdint.hpp"
    "${main_cpp_base}/main.hpp"
    "${main_cpp_base}/video.hpp"
    "${main_cpp_base}/utils.hpp"
    "${main_cpp_base}/windirent.h"

    "${main_cpp_base}/main.cpp"
    "${main_cpp_base}/romloader.cpp"
    "${main_cpp_base}/trackloader.cpp"
    "${main_cpp_base}/roms.cpp"
    "${main_cpp_base}/video.cpp"
    "${main_cpp_base}/utils.cpp"
    )

set(src_frontend
    "${main_cpp_base}/frontend/cabdiag.hpp"
    "${main_cpp_base}/frontend/config.hpp"
    "${main_cpp_base}/frontend/menu.hpp"
    "${main_cpp_base}/frontend/menulabels.hpp"
    "${main_cpp_base}/frontend/ttrial.hpp"

    "${main_cpp_base}/frontend/cabdiag.cpp"
    "${main_cpp_base}/frontend/config.cpp"
    "${main_cpp_base}/frontend/menu.cpp"
    "${main_cpp_base}/frontend/ttrial.cpp"
    )

set(src_hwvideo
    "${main_cpp_base}/hwvideo/hwroad.hpp"
    "${main_cpp_base}/hwvideo/hwsprites.hpp"
    "${main_cpp_base}/hwvideo/hwtiles.hpp"

    "${main_cpp_base}/hwvideo/hwroad.cpp"
    "${main_cpp_base}/hwvideo/hwsprites.cpp"
    "${main_cpp_base}/hwvideo/hwtiles.cpp"
    )

set(src_hwaudio
    "${main_cpp_base}/hwaudio/segapcm.hpp"
    "${main_cpp_base}/hwaudio/soundchip.hpp"
    "${main_cpp_base}/hwaudio/ym2151.hpp"

    "${main_cpp_base}/hwaudio/segapcm.cpp"
    "${main_cpp_base}/hwaudio/soundchip.cpp"
    "${main_cpp_base}/hwaudio/ym2151.cpp"
    )

set(src_sdl
    "${main_cpp_base}/sdl2/audio.hpp"
    "${main_cpp_base}/sdl2/wav123.hpp"
    "${main_cpp_base}/sdl2/timer.hpp"
    "${main_cpp_base}/sdl2/input.hpp"
    "${main_cpp_base}/sdl2/renderbase.hpp"
    "${main_cpp_base}/sdl2/snes_ntsc.h"
    "${main_cpp_base}/sdl2/pixel_scaler_state.hpp"

    "${main_cpp_base}/sdl2/audio.cpp"
    "${main_cpp_base}/sdl2/wav123.cpp"
    "${main_cpp_base}/sdl2/timer.cpp"
    "${main_cpp_base}/sdl2/input.cpp"
    "${main_cpp_base}/sdl2/renderbase.cpp"
    "${main_cpp_base}/sdl2/snes_ntsc.cpp"
    )

set(src_render
    "${main_cpp_base}/sdl2/rendersurface.hpp"
    "${main_cpp_base}/sdl2/rendersurface.cpp"
    )

set(src_engine
    "${main_cpp_base}/engine/oaddresses.hpp"
    "${main_cpp_base}/engine/oanimseq.hpp"
    "${main_cpp_base}/engine/oanimsprite.hpp"
    "${main_cpp_base}/engine/oattractai.hpp"
    "${main_cpp_base}/engine/obonus.hpp"
    "${main_cpp_base}/engine/ocrash.hpp"
    "${main_cpp_base}/engine/oentry.hpp"
    "${main_cpp_base}/engine/oferrari.hpp"
    "${main_cpp_base}/engine/ohiscore.hpp"
    "${main_cpp_base}/engine/ohud.hpp"
    "${main_cpp_base}/engine/oinitengine.hpp"
    "${main_cpp_base}/engine/oinputs.hpp"
    "${main_cpp_base}/engine/olevelobjs.hpp"
    "${main_cpp_base}/engine/ologo.hpp"
    "${main_cpp_base}/engine/omap.hpp"
    "${main_cpp_base}/engine/omusic.hpp"
    "${main_cpp_base}/engine/ooutputs.hpp"
    "${main_cpp_base}/engine/opalette.hpp"
    "${main_cpp_base}/engine/oroad.hpp"
    "${main_cpp_base}/engine/osmoke.hpp"
    "${main_cpp_base}/engine/osprite.hpp"
    "${main_cpp_base}/engine/osprites.hpp"
    "${main_cpp_base}/engine/ostats.hpp"
    "${main_cpp_base}/engine/otiles.hpp"
    "${main_cpp_base}/engine/otraffic.hpp"
    "${main_cpp_base}/engine/outils.hpp"
    "${main_cpp_base}/engine/outrun.hpp"
    "${main_cpp_base}/engine/oanimseq.cpp"
    "${main_cpp_base}/engine/oattractai.cpp"
    "${main_cpp_base}/engine/obonus.cpp"
    "${main_cpp_base}/engine/ocrash.cpp"
    "${main_cpp_base}/engine/oferrari.cpp"
    "${main_cpp_base}/engine/ohiscore.cpp"
    "${main_cpp_base}/engine/ohud.cpp"
    "${main_cpp_base}/engine/oinitengine.cpp"
    "${main_cpp_base}/engine/oinputs.cpp"
    "${main_cpp_base}/engine/olevelobjs.cpp"
    "${main_cpp_base}/engine/ologo.cpp"
    "${main_cpp_base}/engine/omap.cpp"
    "${main_cpp_base}/engine/omusic.cpp"
    "${main_cpp_base}/engine/ooutputs.cpp"
    "${main_cpp_base}/engine/opalette.cpp"
    "${main_cpp_base}/engine/oroad.cpp"
    "${main_cpp_base}/engine/osmoke.cpp"
    "${main_cpp_base}/engine/osprite.cpp"
    "${main_cpp_base}/engine/osprites.cpp"
    "${main_cpp_base}/engine/ostats.cpp"
    "${main_cpp_base}/engine/otiles.cpp"
    "${main_cpp_base}/engine/otraffic.cpp"
    "${main_cpp_base}/engine/outils.cpp"
    "${main_cpp_base}/engine/outrun.cpp"
    )

set(src_engine_data
    "${main_cpp_base}/engine/data/ozoom_lookup.hpp"
    "${main_cpp_base}/engine/data/sprite_pals.hpp"
    )

set(src_engine_audio
    "${main_cpp_base}/engine/audio/commands.hpp"
    "${main_cpp_base}/engine/audio/osound.hpp"
    "${main_cpp_base}/engine/audio/osoundint.hpp"
    "${main_cpp_base}/engine/audio/osoundadr.hpp"

    "${main_cpp_base}/engine/audio/osound.cpp"
    "${main_cpp_base}/engine/audio/osoundint.cpp"
    )

set(src_directx
    "${main_cpp_base}/directx/ffeedback.hpp"
    "${main_cpp_base}/directx/ffeedback.cpp"
    )

if(WIN32)
    list(APPEND src_directx
        "${CMAKE_SOURCE_DIR}/res/cannonball.rc"
    )
endif()

set(SRCS
    ${src_main}
    ${src_frontend}
    ${src_hwvideo}
    ${src_hwaudio}
    ${src_sdl}
    ${src_render}
    ${src_scalers}
    ${src_directx}
    ${src_engine}
    ${src_engine_data}
    ${src_engine_audio}
)

# Group files into relevant filters for Visual Studio build
SOURCE_GROUP(src FILES ${src_main})
SOURCE_GROUP(src\\frontend FILES ${src_frontend})
SOURCE_GROUP(src\\hwvideo FILES ${src_hwvideo})
SOURCE_GROUP(src\\hwaudio FILES ${src_hwaudio})
SOURCE_GROUP(src\\sdl FILES ${src_sdl})
SOURCE_GROUP(src\\sdl FILES ${src_render})
SOURCE_GROUP(src\\sdl\\scalers FILES ${src_scalers})
SOURCE_GROUP(src\\directx FILES ${src_directx})
SOURCE_GROUP(src\\engine FILES ${src_engine})
SOURCE_GROUP(src\\engine\\data FILES ${src_engine_data})
SOURCE_GROUP(src\\engine\\audio FILES ${src_engine_audio})

add_executable(cannonball-se ${SRCS})

if(WIN32)
    target_compile_features(cannonball-se PRIVATE cxx_std_23)
    target_compile_definitions(cannonball-se PRIVATE
        MPG123_NO_LARGENAME      # stop header from mapping foo -> foo_64
        MPG123_NO_64BIT_API
    )
endif()

if(MSVC)
    set_property(TARGET cannonball-se
        PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

if(PIXEL_ACCURACY)
    target_compile_definitions(cannonball-se PRIVATE PIXEL_ACCURACY=1)
else()
    target_compile_definitions(cannonball-se PRIVATE PIXEL_ACCURACY=0)
endif()

# --- Dependencies: SDL2 (robust handling), TinyXML2, EGL/GLESv2 -------------------

# SDL2: Prefer modern imported targets; fall back to MODULE or pkg-config.
set(_SDL2_TARGETS "")
set(_SDL2_INCLUDE_DIRS "")
set(_SDL2_LINK_TARGET "")

find_package(SDL2 QUIET CONFIG)
if(TARGET SDL2::SDL2)
    list(APPEND _SDL2_TARGETS SDL2::SDL2)
    if(WIN32 AND TARGET SDL2::SDL2main)
        list(APPEND _SDL2_TARGETS SDL2::SDL2main)
    endif()
else()
    # Try FindSDL2.cmake (MODULE mode) which defines SDL2_INCLUDE_DIRS/SDL2_LIBRARIES
    find_package(SDL2 QUIET MODULE)
    if(SDL2_FOUND)
        set(_SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
        set(_SDL2_LINK_TARGET ${SDL2_LIBRARIES})
    else()
        # Fallback: pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(_SDL2_LINK_TARGET PkgConfig::SDL2)
    endif()
endif()

# --- TinyXML2 for XML config I/O (replaces Boost.PropertyTree in config.cpp) ---
find_package(TinyXML2 QUIET)
if(NOT TARGET TinyXML2::TinyXML2)
    find_package(tinyxml2 QUIET) # alternate export name on some distros
endif()

if(TARGET TinyXML2::TinyXML2)
    set(_TINYXML2_TARGET TinyXML2::TinyXML2)
elseif(TARGET tinyxml2::tinyxml2)
    set(_TINYXML2_TARGET tinyxml2::tinyxml2)
else()
    # Fall back to pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TINYXML2 REQUIRED IMPORTED_TARGET tinyxml2)
    set(_TINYXML2_TARGET PkgConfig::TINYXML2)
endif()

# ——————————————————————————————————————
# EGL / OpenGLES2 via pkg-config (Linux, Android, etc)
# ——————————————————————————————————————
# headers used like #include "sdl2/foo.hpp" live under src/main
target_include_directories(cannonball-se PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/main"
  "${CB_SCALER_GEN_DIR}/xbrz"
  "${CB_SCALER_GEN_DIR}/hqx"
  ${_SDL2_INCLUDE_DIRS}               # present on all platforms
)

if(NOT WIN32)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(EGL    REQUIRED IMPORTED_TARGET egl)
  pkg_check_modules(GLESv2 REQUIRED IMPORTED_TARGET glesv2)
  # If you link with pkg-config libs, do it here:
  target_link_libraries(cannonball-se PRIVATE
    PkgConfig::EGL
    PkgConfig::GLESv2
  )
else()
  # Windows: prefer ANGLE from vcpkg
  find_package(unofficial-angle CONFIG QUIET)
  if(TARGET unofficial::angle::libEGL AND TARGET unofficial::angle::libGLESv2)
    target_link_libraries(cannonball-se PRIVATE
      unofficial::angle::libEGL
      unofficial::angle::libGLESv2
    )
  endif()
endif()


# Links
if(_SDL2_TARGETS)
    target_link_libraries(cannonball-se PRIVATE ${_SDL2_TARGETS})
elseif(_SDL2_LINK_TARGET)
    target_link_libraries(cannonball-se PRIVATE ${_SDL2_LINK_TARGET})
endif()

target_link_libraries(cannonball-se PRIVATE
  ${_TINYXML2_TARGET}
  $<$<PLATFORM_ID:Windows>:unofficial::angle::libEGL>
  $<$<PLATFORM_ID:Windows>:unofficial::angle::libGLESv2>
  $<$<NOT:$<PLATFORM_ID:Windows>>:PkgConfig::EGL>
  $<$<NOT:$<PLATFORM_ID:Windows>>:PkgConfig::GLESv2>
)

# --- Linux: udev for joystick hotplugging ---
if(UNIX AND NOT APPLE)
    find_library(UDEV_LIBRARY udev)
    if(UDEV_LIBRARY)
        target_link_libraries(cannonball-se PRIVATE ${UDEV_LIBRARY})
    else()
        message(WARNING "libudev not found. Joystick hotplugging may not work.")
    endif()
    # --- Also add debug trace info
    target_link_options(cannonball-se PRIVATE -rdynamic)
endif()

# --- Optional MPG123/MP3 support ---
if(WITH_MP3)
    find_path(MPG123_INCLUDE_DIR mpg123.h)
    find_library(MPG123_LIBRARY NAMES mpg123 libmpg123)
    if(MPG123_INCLUDE_DIR AND MPG123_LIBRARY)
        message(STATUS "mpg123 found, enabling MP3 playback.")
        target_compile_definitions(cannonball-se PRIVATE WITH_MP3)
        target_include_directories(cannonball-se PRIVATE ${MPG123_INCLUDE_DIR})
        target_link_libraries(cannonball-se PRIVATE ${MPG123_LIBRARY})
    else()
        message(WARNING "mpg123 not found, building without MP3 playback. Set WITH_MP3=OFF to suppress this warning.")
    endif()
else()
    message(STATUS "Building without MP3 playback support (WITH_MP3=OFF).")
endif()

# Windows-specific: DirectX and Windows multimedia (if needed)
if(WIN32)
    target_link_libraries(cannonball-se PRIVATE dinput8 dxguid winmm)
endif()

# Recommended warnings for GCC/Clang
if(MSVC)
    target_compile_options(cannonball-se PRIVATE /W4)
else()
    target_compile_options(cannonball-se PRIVATE -Wall -Wextra -Wpedantic)

    # Optimize Release builds aggressively (-Ofast) on GCC/Clang
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cannonball-se PRIVATE $<$<CONFIG:Release>:-Ofast>)
    endif()
endif()

# Debug builds
# --- Debug hardening & sanitizers (GCC/Clang only) ------------------------------
# No explicit -msse4.1 here: the SIMD code detects SSE4.1 at compile time via
# __SSE4_1__ and falls back to an SSE2 path automatically.  Release builds with
# WITH_MARCH_NATIVE=ON enable SSE4.1 on capable hosts through -march=native.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Compile-time flags (Debug only)
  target_compile_options(cannonball-se PRIVATE
    $<$<CONFIG:Debug>:-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer -ftrapv>
  )

  # Link-time flags (Debug only) — needed for sanitizers
  target_link_options(cannonball-se PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
  )

  # Preprocessor defines (Debug only)
  target_compile_definitions(cannonball-se PRIVATE
    $<$<CONFIG:Debug>:_FORTIFY_SOURCE=2>
  )
endif()


# --- Quiet warnings on noisy toolchains ---------------------------------------
# These are conservative suppressions aimed at legacy/portability code paths.
if(QUIET_BUILD)
    if(MSVC)
        # Drop to /W3 and silence a few common noise warnings (narrowing, signed/unsigned, deprecations)
        target_compile_options(cannonball-se PRIVATE /W3 /wd4244 /wd4267 /wd4018 /wd4996)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cannonball-se PRIVATE
            -Wno-unused-parameter
            -Wno-unused-variable
            -Wno-missing-field-initializers
            -Wno-sign-compare
            -Wno-reorder
            -Wno-implicit-fallthrough
            -Wno-format-security)
        if(SUPER_QUIET)
            # Nuke all warnings for user-friendly Release-like configs
            target_compile_options(cannonball-se PRIVATE
            $<$<CONFIG:Release>:-w>
            $<$<CONFIG:RelWithDebInfo>:-w>
            $<$<CONFIG:MinSizeRel>:-w>)
        endif()
    endif()
endif()


# Add -march=native for GCC/Clang, only in Release builds
if(WITH_MARCH_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    target_compile_options(cannonball-se PRIVATE
      $<$<COMPILE_LANGUAGE:C>:-march=native>
      $<$<COMPILE_LANGUAGE:CXX>:-march=native>)
  endif()
endif()

option(WITH_RVV "Enable RISC-V Vector (RVV) SIMD (requires -march=..._v)" OFF)
if(WITH_RVV AND CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    target_compile_options(cannonball-se PRIVATE -march=rv64gcv)
endif()


# --- PROFILING FLAGS -----------------------------------------------------------
if(ENABLE_PROFILING)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Symbols + frame pointers for accurate stacks in perf/flamegraphs
        target_compile_options(cannonball-se PRIVATE -g -fno-omit-frame-pointer)
        # Avoid LTO which can hide symbols / confuse profilers
        set_property(TARGET cannonball-se PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
    endif()

    if(PROFILE_WITH_PERF)
        message(STATUS "Profiling: perf enabled (symbols + frame pointers)")
        # Nothing extra required beyond -g and -fno-omit-frame-pointer
    endif()

    if(PROFILE_WITH_GPROF)
        message(STATUS "Profiling: gprof via -pg enabled")
        target_compile_options(cannonball-se PRIVATE -pg)
        target_link_options(cannonball-se PRIVATE -pg)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            include(CheckPIESupported)
            check_pie_supported(LANGUAGES C CXX)
            if(NOT CMAKE_CXX_LINK_PIE_SUPPORTED)
                target_link_options(cannonball-se PRIVATE -no-pie)
            endif()
        endif()
    endif()

    if(PROFILE_WITH_CALLGRIND)
        message(STATUS "Profiling: callgrind-friendly inlining reduction enabled")
        target_compile_options(cannonball-se PRIVATE -fno-inline-functions -fno-inline-small-functions)
    endif()

    if(PROFILE_WITH_GPERFTOOLS)
        # To enable this, run:
        # $ cmake -DENABLE_PROFILING=ON -DPROFILE_WITH_GPERFTOOLS=ON -S ../ -B build
        # $ cmake --build build -j
        #
        message(STATUS "Profiling: Google perftools (CPU) enabled")
        # Prefer pkg-config if available
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBPROFILER QUIET libprofiler)
        endif()

        # Include dir is optional (header usually in system include path),
        # but probe to give nicer errors if not installed.
        find_path(GPERFTOOLS_INCLUDE_DIR
                  NAMES gperftools/profiler.h
                  HINTS ${LIBPROFILER_INCLUDE_DIRS})

        # Find the library (shared preferred)
        find_library(GPERFTOOLS_PROFILER_LIB
                     NAMES profiler libprofiler
                     HINTS ${LIBPROFILER_LIBRARY_DIRS})

        if(NOT GPERFTOOLS_INCLUDE_DIR OR NOT GPERFTOOLS_PROFILER_LIB)
            message(FATAL_ERROR "gperftools not found. Install libgoogle-perftools-dev (Ubuntu/WSL) then reconfigure.")
        endif()

        target_include_directories(cannonball-se PRIVATE ${GPERFTOOLS_INCLUDE_DIR})
        target_link_libraries(cannonball-se PRIVATE ${GPERFTOOLS_PROFILER_LIB} pthread)

        # Helpful define (optional): lets you #ifdef around ProfilerStart/Stop
        target_compile_definitions(cannonball-se PRIVATE HAS_GPERFTOOLS=1)

        # Make -DPROFILE_WITH_GPERFTOOLS available to your code
        target_compile_definitions(cannonball-se PRIVATE PROFILE_WITH_GPERFTOOLS=1)
        # Notes:
        # - Do NOT combine with -pg (gprof).
        # - You start/stop profiling in code with ProfilerStart/Stop.
        # - At runtime you can tune sampling with CPUPROFILE_FREQUENCY, e.g.:
        #     CPUPROFILE_FREQUENCY=1000 ./cannonball-se
    endif()

endif()

# OpenMP: Try to enable, but don't require everywhere
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(cannonball-se PRIVATE OpenMP::OpenMP_CXX)
endif()

# Copy out DLLs on Windows
if(WIN32)
    add_custom_command(TARGET cannonball-se POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:cannonball-se> $<TARGET_FILE_DIR:cannonball-se>
        COMMAND_EXPAND_LISTS
    )
endif()

# Copy out config files
configure_file(res/tilemap.bin ${CMAKE_CURRENT_BINARY_DIR}/tilemap.bin COPYONLY)
configure_file(res/tilepatch.bin ${CMAKE_CURRENT_BINARY_DIR}/tilepatch.bin COPYONLY)
configure_file(res/gamecontrollerdb.txt ${CMAKE_CURRENT_BINARY_DIR}/gamecontrollerdb.txt COPYONLY)

# -----------------------------
# Install: man page & licenses
# -----------------------------

# Install the cannonball-se(6) man page if present
set(_MAN_CANDIDATES
  "${CMAKE_SOURCE_DIR}/docs/cannonball-se.6"
  "${CMAKE_SOURCE_DIR}/cannonball-se.6")
foreach(_cand IN LISTS _MAN_CANDIDATES)
  if(EXISTS "${_cand}")
    set(MANPAGE_SOURCE "${_cand}")
    break()
  endif()
endforeach()
if(INSTALL_MANPAGE AND UNIX AND MANPAGE_SOURCE)
  get_filename_component(_ext "${MANPAGE_SOURCE}" EXT)
  install(FILES "${MANPAGE_SOURCE}" DESTINATION "${CMAKE_INSTALL_MANDIR}/man6")
endif()

# Install upstream license and third-party notices
install(FILES "${CMAKE_SOURCE_DIR}/license.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
if(EXISTS "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md")
  install(FILES "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/licenses")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/licenses")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/licenses/"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/licenses")
endif()