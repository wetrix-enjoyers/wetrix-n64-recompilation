cmake_minimum_required(VERSION 3.20)
project(WetrixRecompilation C CXX)

# Wetrix as recompiled code: the N64Recomp output for the game (see wetrix.toml),
# the RSPRecomp output for its audio microcode (see wetrix_rsp.toml), the
# game-side shims those call, and the N64ModernRuntime that runs them.
#
# A port adds it with add_subdirectory and links `wetrix_game`; it brings its
# own window, input and renderer. Nothing here knows about any platform or
# device beyond the CPU architecture the microcode's SIMD is compiled for.
#
# Also sets, as cache variables for the port: WETRIX_RUNTIME_DIR (the runtime
# checkout) and WETRIX_IMAGE (the game image, wetrix.z64).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# The recompiled C produced by N64Recomp. It lands in build/, generated from the
# ROM; it is not committed, because it is derived from the cartridge.
set(RECOMP_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/recomp"
    CACHE PATH "Directory holding N64Recomp output (funcs_*.c, funcs.h)")

# The RSPRecomp output for the ROM's audio microcode. This one has to exist:
# M_AUDTASK is the first thing the ROM submits that the runtime cannot answer
# itself, and librecomp aborts the process without it.
set(RSP_OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/build/rsp/aspMain.cpp"
    CACHE FILEPATH "RSPRecomp output for the audio microcode")

# The N64ModernRuntime checkout.
set(WETRIX_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/n64modernruntime"
    CACHE PATH "Directory holding the N64ModernRuntime checkout")

# The game image: the 8MB ROM image N64Recomp and RSPRecomp read, and the image
# the runtime loads. tools/ (wetrix_provision) adopts a user's dump into it; the
# ports' exe does the same on first launch in a folder that has none.
set(WETRIX_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/wetrix.z64"
    CACHE FILEPATH "The game image, adopted from a ROM")

foreach(need IN ITEMS "${RECOMP_OUTPUT_DIR}/funcs.h" "${RSP_OUTPUT_PATH}")
    if(NOT EXISTS "${need}")
        message(FATAL_ERROR "Missing ${need}.\nGenerate the recompiled sources first; see README.md.")
    endif()
endforeach()
# Generated sources from old inputs compile the wrong functions without
# complaint. tools/regen.py records its inputs' hashes in each output folder's
# .inputs; re-hash them here, and re-configure whenever one changes.
foreach(out IN ITEMS "${RECOMP_OUTPUT_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/build/rsp")
    if(NOT EXISTS "${out}/.inputs")
        message(FATAL_ERROR "${out} has no .inputs record.\nRun: python tools/regen.py (in ${CMAKE_CURRENT_SOURCE_DIR})")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${out}/.inputs")
    file(STRINGS "${out}/.inputs" lines)
    foreach(line IN LISTS lines)
        string(REGEX MATCH "^([0-9a-f]+) (.+)$" _ "${line}")
        set(path "${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_MATCH_2}")
        set(want "${CMAKE_MATCH_1}")
        set(have "")
        if(EXISTS "${path}")
            file(SHA256 "${path}" have)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
        endif()
        if(NOT have STREQUAL want)
            message(FATAL_ERROR "${out} is out of date: ${CMAKE_MATCH_2} changed.\n"
                "Run: python tools/regen.py (in ${CMAKE_CURRENT_SOURCE_DIR})")
        endif()
    endforeach()
endforeach()
if(NOT EXISTS "${WETRIX_RUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "No N64ModernRuntime checkout in ${WETRIX_RUNTIME_DIR}.\n"
        "git clone --recurse-submodules https://github.com/N64Recomp/N64ModernRuntime build/n64modernruntime")
endif()
if(NOT EXISTS "${WETRIX_IMAGE}")
    message(FATAL_ERROR
        "No game image at ${WETRIX_IMAGE}.\n"
        "Drop your Wetrix ROM (.z64, .n64 or .v64) here and run tools/ (wetrix_provision); see README.md.")
endif()

# Provides the `ultramodern` and `librecomp` static libraries.
add_subdirectory("${WETRIX_RUNTIME_DIR}" runtime)

file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS "${RECOMP_OUTPUT_DIR}/funcs_*.c")

add_library(wetrix_game STATIC
    ${RECOMP_SOURCES}
    src/recomp_overlays.cpp
    src/replaced_funcs.cpp
    src/ultra_shims.cpp
    src/trace.cpp
    src/rom_adopt.c
    "${RSP_OUTPUT_PATH}")
target_include_directories(wetrix_game PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${RECOMP_OUTPUT_DIR}"
    "${WETRIX_RUNTIME_DIR}/N64Recomp/include"
    # rom_adopt.c checks the ROM against the hash librecomp itself compares
    # with, so it inlines the runtime's xxHash header, as librecomp does.
    "${WETRIX_RUNTIME_DIR}/thirdparty/xxHash")

# RECOMP_FUNC exists to stop a compiler inlining or interposing recompiled
# functions, because the ROM assumes distinct callable code at each address.
# LTO would defeat that, so it stays off.
set_target_properties(wetrix_game PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF
    POSITION_INDEPENDENT_CODE ON)

target_link_libraries(wetrix_game PUBLIC librecomp ultramodern)

# The generated microcode pulls in librecomp's RSP vector unit, which is written
# against SSE4.1 on x86-64 (gcc's SSE2 baseline refuses to inline its
# intrinsics). On aarch64 rsp_vu.hpp maps the same intrinsics onto NEON through
# the runtime's sse2neon.h, which needs only the include path.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    target_include_directories(wetrix_game PUBLIC "${WETRIX_RUNTIME_DIR}/thirdparty/sse2neon")
elseif(MSVC)
    set_source_files_properties("${RSP_OUTPUT_PATH}" PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
else()
    set_source_files_properties("${RSP_OUTPUT_PATH}" PROPERTIES COMPILE_OPTIONS "-msse4.1")
endif()
