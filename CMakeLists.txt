# CMakeLists.txt - how to build C3U-Metal.
#
# From an ESP-IDF terminal (it puts the RISC-V toolchain, CMake, Ninja and
# Python on the PATH), in this folder:
#
#   cmake --preset default              once: set up the build folder
#   cmake --build build                 compile and link build/c3u-metal.elf and .bin
#   cmake --build build -t flash        build, then write the program to the board
#   cmake --build build -t monitor      open the Lua console (Ctrl-] quits)
#   cmake --build build -t selftest     flash, then check that the board answers correctly
#
# Nothing else from ESP-IDF is used: no bootloader, no FreeRTOS, no components.

cmake_minimum_required(VERSION 3.21)

# Cross-compile for the ESP32-C3. CMakePresets.json sets this too.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/riscv32-esp-elf.cmake)
endif()

project(c3u-metal LANGUAGES C ASM)

# The ESP32-C3's instruction set: RV32IMC plus the CSR and FENCE.I instructions.
# It must match a variant of the toolchain's newlib and libgcc, and is used for
# compiling and for linking.
set(ARCH_FLAGS -march=rv32imc_zicsr_zifencei -mabi=ilp32)


# ---- Lua 5.5.1, unmodified, as a static library -------------------------------

set(LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/src)
set(LUA_FILES
    # the core of Lua
    lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes
    lparser lstate lstring ltable ltm lundump lvm lzio
    # the standard libraries that make sense without files or an operating
    # system (so no io, os, package or debug)
    lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib)
list(TRANSFORM LUA_FILES PREPEND ${LUA_DIR}/)
list(TRANSFORM LUA_FILES APPEND .c)

add_library(lua STATIC ${LUA_FILES})
target_compile_options(lua PRIVATE ${ARCH_FLAGS} -O2 -g -Wall -Wextra)
target_include_directories(lua PUBLIC ${LUA_DIR})
# 32-bit integers and floats. PUBLIC, because our C code must use the same
# setting, or it and Lua would disagree about what a Lua number is.
target_compile_definitions(lua PUBLIC LUA_32BITS)


# ---- The program --------------------------------------------------------------

set(SOURCES
    boot/crt0.S                 # the C runtime: the chip starts running here
    boot/stack_check.c
    chip/system_esp32c3.c       # chip setup
    chip/cpu.S
    chip/trap.S
    drivers/gpio.c
    drivers/sk6812.c
    drivers/sk6812.S
    drivers/usb_serial.c
    drivers/uptime.c
    libc/syscalls.c             # what newlib needs from an "operating system"
    app/main.c
    app/repl.c
    app/lua_hw.c
    app/lua_sys.c
    app/trap_report.c)
set(INCLUDE_DIRS boot chip board drivers libc app)

# A worked solution (solutions/<name>, see solutions/README.md) holds copies of
# the files it changes, and any new ones. Its copies are used instead of the
# project's files, its new .c and .S files are added, and its folders come
# first when looking for headers.
set(SOLUTION "" CACHE STRING "Build the worked solution in solutions/<name> instead of the plain project")
if(SOLUTION)
    set(SOLUTION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/solutions/${SOLUTION})
    if(NOT IS_DIRECTORY ${SOLUTION_DIR})
        message(FATAL_ERROR "There is no solutions/${SOLUTION} folder")
    endif()
    message(STATUS "Building the worked solution in solutions/${SOLUTION}")
    file(GLOB_RECURSE SOLUTION_SOURCES RELATIVE ${SOLUTION_DIR} CONFIGURE_DEPENDS
        ${SOLUTION_DIR}/*.c ${SOLUTION_DIR}/*.S)
    list(APPEND SOURCES ${SOLUTION_SOURCES})
    list(REMOVE_DUPLICATES SOURCES)
    if(EXISTS ${SOLUTION_DIR}/solution.cmake)
        include(${SOLUTION_DIR}/solution.cmake)     # lets a solution drop files from SOURCES
    endif()
endif()

# The path of a file: from the solution if it has a copy, otherwise from the project.
function(pick_path file out)
    if(SOLUTION AND EXISTS ${SOLUTION_DIR}/${file})
        set(${out} ${SOLUTION_DIR}/${file} PARENT_SCOPE)
    else()
        set(${out} ${CMAKE_CURRENT_SOURCE_DIR}/${file} PARENT_SCOPE)
    endif()
endfunction()

set(SOURCE_PATHS)
foreach(file ${SOURCES})
    pick_path(${file} path)
    list(APPEND SOURCE_PATHS ${path})
endforeach()
set(INCLUDE_PATHS)
foreach(dir ${INCLUDE_DIRS})
    if(SOLUTION AND IS_DIRECTORY ${SOLUTION_DIR}/${dir})
        list(APPEND INCLUDE_PATHS ${SOLUTION_DIR}/${dir})
    endif()
endforeach()
foreach(dir ${INCLUDE_DIRS})
    list(APPEND INCLUDE_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
endforeach()

add_executable(c3u-metal ${SOURCE_PATHS})
set_target_properties(c3u-metal PROPERTIES SUFFIX .elf)

target_include_directories(c3u-metal PRIVATE ${INCLUDE_PATHS})
target_compile_options(c3u-metal PRIVATE
    ${ARCH_FLAGS} -g -Og -Wall -Wextra
    -fno-asynchronous-unwind-tables -fno-unwind-tables)    # no C++ exceptions, so no unwind tables

pick_path(boot/c3u-metal.ld LINKER_SCRIPT)
target_link_options(c3u-metal PRIVATE
    ${ARCH_FLAGS}
    -nostdlib -nostartfiles                 # our own crt0.S; the libraries are listed below
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/c3u-metal.map)
set_target_properties(c3u-metal PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})    # relink when it changes
target_link_libraries(c3u-metal PRIVATE
    lua
    -Wl,--start-group c m gcc -Wl,--end-group)   # newlib, its maths library, and compiler helpers

# The raw flash image. It starts with the direct-boot magic number from the linker script.
get_filename_component(TOOLCHAIN_BIN ${CMAKE_C_COMPILER} DIRECTORY)
find_program(RISCV_OBJCOPY riscv32-esp-elf-objcopy HINTS ${TOOLCHAIN_BIN} REQUIRED)
find_program(RISCV_SIZE riscv32-esp-elf-size HINTS ${TOOLCHAIN_BIN} REQUIRED)
set(IMAGE ${CMAKE_CURRENT_BINARY_DIR}/c3u-metal.bin)
add_custom_command(TARGET c3u-metal POST_BUILD
    COMMAND ${RISCV_OBJCOPY} -O binary $<TARGET_FILE:c3u-metal> ${IMAGE}
    COMMAND ${RISCV_SIZE} $<TARGET_FILE:c3u-metal>
    BYPRODUCTS ${IMAGE}
    VERBATIM)


# ---- Board tools (Python scripts in tools/) ----------------------------------------

# ESP-IDF's Python comes first on the PATH, and it has pyserial and esptool installed.
find_program(PYTHON NAMES python python3 REQUIRED)
set(TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/tools)

add_custom_target(flash
    COMMAND ${PYTHON} ${TOOLS}/flash.py ${IMAGE}
    USES_TERMINAL VERBATIM)
add_dependencies(flash c3u-metal)

add_custom_target(monitor
    COMMAND ${PYTHON} ${TOOLS}/monitor.py
    USES_TERMINAL VERBATIM)

add_custom_target(selftest
    COMMAND ${PYTHON} ${TOOLS}/selftest.py
    USES_TERMINAL VERBATIM)
add_dependencies(selftest flash)
