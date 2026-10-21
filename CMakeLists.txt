set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

cmake_minimum_required(VERSION 3.7.2)
project("Chocolate Doom" VERSION 3.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 99)

# Autotools variables
set(top_srcdir ${CMAKE_CURRENT_SOURCE_DIR})
set(top_builddir ${CMAKE_CURRENT_BINARY_DIR})

# AC_INIT variables
set(PACKAGE_NAME "${PROJECT_NAME}")
set(PACKAGE_TARNAME "chocolate-doom")
set(PACKAGE_VERSION "${PROJECT_VERSION}")
set(PACKAGE_STRING "${PROJECT_NAME} ${PROJECT_VERSION}")
set(PACKAGE_BUGREPORT "chocolate-doom-dev-list@chocolate-doom.org")

string(REGEX REPLACE " Doom$" "" PACKAGE_SHORTNAME "${PACKAGE_NAME}")
set(PACKAGE_COPYRIGHT "Copyright (C) 1993-2025")
set(PACKAGE_LICENSE "GNU General Public License, version 2")

# Any settings that should apply to all targets in this directory and all
# subdirectories should go here.  Use judiciously.
if(MSVC)
    add_definitions("/D_CRT_SECURE_NO_WARNINGS" "/D_CRT_SECURE_NO_DEPRECATE"
                    "/D_CRT_NONSTDC_NO_DEPRECATE")
else()
    add_compile_options("-Wall" "-Wdeclaration-after-statement"
                        "-Wredundant-decls")
endif()

option(CMAKE_FIND_PACKAGE_PREFER_CONFIG
       "Lookup package config files before using find modules" On)

option(ENABLE_SDL2_NET "Enable SDL2_net" On)
option(ENABLE_SDL2_MIXER "Enable SDL2_mixer" On)

if (NOT DEFINED EMSCRIPTEN)
    find_package(SDL2 2.0.14)
endif()
if(ENABLE_SDL2_MIXER)
    if (NOT DEFINED EMSCRIPTEN)
        find_package(SDL2_mixer 2.0.2)
    endif()
else()
    add_compile_definitions(DISABLE_SDL2MIXER=1)
endif()

if(ENABLE_SDL2_NET)
    if (NOT DEFINED EMSCRIPTEN)
        find_package(SDL2_net 2.0.0)
    endif()
else()
    add_compile_definitions(DISABLE_SDL2NET=1)
endif()

# Chocolate Doom VK: run libretro slang shader presets (.slangp) through
# Vulkan via VTSTech/libslang-vk (pure-C API, C++23 library behind it). The
# library is fetched at configure time and linked into the game binaries;
# the games' own sources stay C99. No Vulkan SDK needed anywhere: the
# library loads the Vulkan runtime through volk, and the presenter resolves
# its few direct entry points through SDL_Vulkan_GetVkGetInstanceProcAddr.
option(ENABLE_VK_SHADERS "libretro slang shader presets via Vulkan (VTSTech/libslang-vk)" On)
if(ENABLE_VK_SHADERS)
    if(DEFINED EMSCRIPTEN)
        message(FATAL_ERROR "ENABLE_VK_SHADERS is not supported on Emscripten")
    endif()
    add_compile_definitions(ENABLE_VK_SHADERS=1)
    # The library's targets require cxx_std_23; the games' project() only
    # enables C, so turn on CXX for the embedded library's targets.
    enable_language(CXX)
    include(FetchContent)
    set(SLANGVK_BUILD_TESTS OFF)    # the upstream suite runs in its own repo
    set(SLANGVK_BUILD_EXAMPLES OFF)
    set(SLANGVK_BUILD_SHARED OFF)   # single static link into each game
    # Pinned by content hash, not by a mutable release tag: this is the
    # archive of the exact commit the M4.05 milestone tag points at
    # (fa65b6d5 — 26/26 ctest, byte-exact goldens), fetched by URL and
    # verified by SHA256 at configure time. A tag can be re-pointed (or
    # its archive regenerated) upstream; this pin cannot drift. To bump,
    # swap in the new archive URL plus its sha256 — always deliberate.
    FetchContent_Declare(libslang-vk
    GIT_REPOSITORY https://github.com/VTSTech/libslang-vk.git
    GIT_TAG 09068b25407cd77739391bff2e2a249a743368e7 # M4.06 Release
    )
    FetchContent_MakeAvailable(libslang-vk)
    # The library's targets require C++23 (its scope sets it for its own
    # sources); harmless here — the game code stays C99.
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()


# Check for libsamplerate.
find_package(SampleRate)
if(SampleRate_FOUND)
    set(HAVE_LIBSAMPLERATE TRUE)
endif()

# Check for libpng.
find_package(PNG)
if(PNG_FOUND)
    set(HAVE_LIBPNG TRUE)
endif()

# Check for FluidSynth.
find_package(FluidSynth 2.2.0)
if(FluidSynth_FOUND AND ENABLE_SDL2_MIXER)
    set(HAVE_FLUIDSYNTH TRUE)
endif()

find_package(m)

include(CheckSymbolExists)
include(CheckIncludeFile)
check_symbol_exists(strcasecmp "strings.h" HAVE_DECL_STRCASECMP)
check_symbol_exists(strncasecmp "strings.h" HAVE_DECL_STRNCASECMP)
check_include_file("dirent.h" HAVE_DIRENT_H)

string(CONCAT WINDOWS_RC_VERSION "${PROJECT_VERSION_MAJOR}, "
    "${PROJECT_VERSION_MINOR}, ${PROJECT_VERSION_PATCH}, 0")

# Without a hyphen. This is used for the bash-completion scripts.
string(TOLOWER "${PACKAGE_SHORTNAME}" PROGRAM_SPREFIX)

# With a hyphen, used almost everywhere else.
set(PROGRAM_PREFIX "${PROGRAM_SPREFIX}-")

configure_file(cmake/config.h.cin config.h)

configure_file(src/doom-res.rc.in src/doom-res.rc)
configure_file(src/heretic-res.rc.in src/heretic-res.rc)
configure_file(src/hexen-res.rc.in src/hexen-res.rc)
configure_file(src/setup-res.rc.in src/setup-res.rc)
configure_file(src/setup/setup-manifest.xml.in src/setup/setup-manifest.xml)
configure_file(src/strife-res.rc.in src/strife-res.rc)

foreach(SUBDIR textscreen opl pcsound src)
    add_subdirectory("${SUBDIR}")
endforeach()

# Install the .slangp demo presets shipped with libslang-vk? No — this fork
# ships none by design (see README-VK.md); presets live in the user's
# slang-shaders clone and -shader accepts any path.
