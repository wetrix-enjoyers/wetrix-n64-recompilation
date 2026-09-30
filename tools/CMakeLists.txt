cmake_minimum_required(VERSION 3.20)
project(WetrixTools C)

# The recompilation's toolchain: the one-time step that turns a ROM into the
# image every other part reads. Kept as a separate project from the
# recompilation itself, and deliberately so -- provisioning has to be possible before
# build/recomp and build/rsp exist, so it cannot be gated behind the recompilation's own
# configure, which requires them.
#
#   cmake -S tools -B build/tools
#   cmake --build build/tools
#
# Produces build/tools/wetrix_provision.exe.

# Same default as the recompilation's CMakeLists, and for the same reason: this is
# where a checkout that followed README.md has the runtime. The tool needs only
# the xxHash header out of it, because the hash it checks the ROM against is the
# hash librecomp checks the loaded image with -- validating with anything else
# would be answering a different question.
set(RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/n64modernruntime"
    CACHE PATH "Directory holding the N64ModernRuntime checkout")

if(NOT EXISTS "${RUNTIME_DIR}/thirdparty/xxHash/xxhash.c")
    message(FATAL_ERROR
        "No N64ModernRuntime checkout in ${RUNTIME_DIR}.\n"
        "Run this from the recompilation's root, or point RUNTIME_DIR at it:\n"
        "    git clone --recurse-submodules https://github.com/N64Recomp/N64ModernRuntime build/n64modernruntime")
endif()

# The searching and checking live in src/rom_adopt.c, not here: the ports' exe
# does exactly the same thing at startup, and one implementation is what stops
# the two from disagreeing about what counts as the ROM. So this target compiles
# that same file rather than a second one -- and, like the ports, it needs
# no xxHash library, because rom_adopt.c inlines the header the same way
# librecomp does.
add_executable(wetrix_provision
    provision_rom.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/rom_adopt.c")
target_include_directories(wetrix_provision PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${RUNTIME_DIR}/thirdparty/xxHash")
