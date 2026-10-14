# Native Windows build of the Astonia client with Visual Studio (clang-cl) and vcpkg.
# No MSYS2 needed. See WINDOWS-BUILD.md, or just run build-msvc.bat.
#
# The code uses GCC/Clang extensions (__atomic builtins, __attribute__), so it is
# compiled with clang-cl, the Clang that ships with Visual Studio
# ("C++ Clang tools for Windows" component). It is MSVC-ABI and links with the
# normal Microsoft linker and libraries.
#
# The MSYS2 / Linux / macOS Makefiles are unchanged and still work.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(astonia_client LANGUAGES C RC)

if(NOT WIN32)
    message(FATAL_ERROR "CMakeLists.txt is for the Windows build. On Linux/macOS use make.")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "This client needs clang-cl (it uses __atomic builtins that cl.exe does not have).\n"
        "Install the Visual Studio component \"C++ Clang tools for Windows\" and use a preset "
        "from CMakePresets.json (they select clang-cl), or run build-msvc.bat.")
endif()

option(USE_MIMALLOC "Use mimalloc as the allocator" ON)
option(DEVELOPER "Build with -DDEVELOPER (developer commands and checks)" OFF)
set(DEFAULT_SERVER "" CACHE STRING "Override the default server address (empty = keep 10.1.0.36 from main.c)")

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_mixer CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(libzip CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
if(USE_MIMALLOC)
    find_package(mimalloc CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# moac.exe
# ---------------------------------------------------------------------------
# Same object list as build/make/Makefile.windows
set(MOAC_SOURCES
    src/gui/gui_core.c src/gui/gui_input.c src/gui/gui_display.c src/gui/gui_inventory.c
    src/gui/gui_buttons.c src/gui/gui_map.c
    src/client/client.c src/client/astonia_net.c src/client/protocol.c src/client/skill.c
    src/game/game_core.c src/game/game_effects.c src/game/game_lighting.c src/game/game_display.c
    src/game/render.c src/game/font.c src/game/main.c src/game/sprite.c src/game/sprite_config.c
    src/game/memory.c
    src/modder/modder.c
    src/sdl/sdl_core.c src/sdl/sdl_texture.c src/sdl/sdl_image.c src/sdl/sdl_effects.c
    src/sdl/sdl_draw.c src/sdl/sound.c
    src/game/resource.rc src/helper/helper.c
    src/gui/dots.c src/gui/display.c src/gui/teleport.c src/gui/color.c src/gui/cmd.c
    src/gui/questlog.c src/gui/craftwin.c src/gui/stylewin.c src/gui/context.c src/gui/hover.c src/gui/minimap.c src/gui/options.c
    src/gui/panels.c
    src/modder/sharedmem_windows.c src/game/crash_handler_windows.c
    src/game/memory_windows.c src/gui/draghack_windows.c src/client/unique_windows.c
    src/game/version.c
    src/lib/cjson/cJSON.c
)

add_executable(moac WIN32 ${MOAC_SOURCES})

# Put moac.exe (and, through vcpkg, the DLLs it needs) in bin\ like the Makefile does,
# for every configuration.
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(moac PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY${cfg} "${CMAKE_SOURCE_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY${cfg} "${CMAKE_SOURCE_DIR}/lib"
        PDB_OUTPUT_DIRECTORY${cfg} "${CMAKE_SOURCE_DIR}/bin")
endforeach()

# moac.exe exports functions for mods (amod.dll); this also writes lib\moac.lib.
set_target_properties(moac PROPERTIES
    ENABLE_EXPORTS ON
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The source root is on the path so resource.rc finds res/moa3.ico.
target_include_directories(moac PRIVATE include src "${CMAKE_SOURCE_DIR}")

target_compile_definitions(moac PRIVATE
    _WIN32_WINNT=0x0601
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    STORE_UNIQUE
    ENABLE_CRASH_HANDLER
    ENABLE_SHAREDMEM
    ENABLE_DRAGHACK
    USE_MIMALLOC=$<BOOL:${USE_MIMALLOC}>
    SDL_FUNCTION_POINTER_IS_VOID_POINTER
    $<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${DEVELOPER}>>,DEVELOPER,NDEBUG>)
if(DEFAULT_SERVER)
    target_compile_definitions(moac PRIVATE DEFAULT_SERVER="${DEFAULT_SERVER}")
endif()

# clang-cl: keep -Wall -Wextra like the Makefile, but no -Werror, and no warnings
# for the bundled cJSON.
target_compile_options(moac PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W3 -Wextra -Wno-unused-parameter -Wno-sign-compare
        -Wno-missing-field-initializers -Wno-deprecated-declarations
        -Wno-unknown-warning-option -Wno-unused-but-set-global
        /Zi /Oy->)
set_source_files_properties(src/lib/cjson/cJSON.c PROPERTIES COMPILE_OPTIONS "-w")

# Always write moac.pdb so the crash handler can print function names and lines.
target_link_options(moac PRIVATE /DEBUG /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)

target_link_libraries(moac PRIVATE
    SDL3::SDL3
    SDL3_mixer::SDL3_mixer
    PNG::PNG
    libzip::zip
    ZLIB::ZLIB
    $<$<BOOL:${USE_MIMALLOC}>:$<IF:$<TARGET_EXISTS:mimalloc>,mimalloc,mimalloc-static>>
    ws2_32 wsock32 psapi dbghelp)
