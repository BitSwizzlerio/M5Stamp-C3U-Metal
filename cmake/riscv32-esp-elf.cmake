# riscv32-esp-elf.cmake - a CMake toolchain file: build for the ESP32-C3, not for this PC.
#
# It uses Espressif's RISC-V GCC, which is installed with ESP-IDF. Run CMake
# from an ESP-IDF terminal so that riscv32-esp-elf-gcc is on the PATH.

set(CMAKE_SYSTEM_NAME Generic)                  # "Generic": no operating system on the target
set(CMAKE_SYSTEM_PROCESSOR riscv32)

set(CMAKE_C_COMPILER   riscv32-esp-elf-gcc)
set(CMAKE_ASM_COMPILER riscv32-esp-elf-gcc)     # gcc runs the C preprocessor on .S files, then assembles them

# CMake checks the compiler by building a small test program. A normal program
# can't link without our linker script and crt0.S, so build a library instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Programs (such as Python) come from this PC; libraries and headers only from the toolchain.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
