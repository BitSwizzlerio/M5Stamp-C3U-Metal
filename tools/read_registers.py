#!/usr/bin/env python3
"""
read_registers.py - look inside the running board over JTAG.

Starts OpenOCD, stops the CPU for a moment, and prints:
- its registers, including the trap registers,
- a few hardware registers,
- any global variables you name.
Then it lets the program carry on.

    python tools/read_registers.py
    python tools/read_registers.py sk6812_last_high sk6812_last_bit

Needs OpenOCD for Espressif chips (openocd-esp32) and the RISC-V toolchain on
the PATH, so run it from an ESP-IDF terminal. JTAG uses the same USB cable as
the console. Only one program can use the JTAG connection at a time, so stop
any debugger first.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ELF = os.path.join(ROOT, "build", "c3u-metal.elf")

CPU_REGISTERS = [
    ("pc", "program counter: the instruction about to run"),
    ("ra", "return address"),
    ("sp", "stack pointer"),
    ("gp", "global pointer"),
    ("mtvec", "trap vector table (+1 means vectored mode)"),
    ("mcause", "reason for the last trap (0 if there has been none)"),
    ("mepc", "address where the last trap happened"),
    ("mtval", "bad address or instruction of the last trap"),
]

HARDWARE_REGISTERS = [
    (0x60008090, "RTC_CNTL_WDTCONFIG0", "RTC watchdog (0 = off)"),
    (0x6001F048, "TIMG0_WDTCONFIG0", "Timer Group 0 watchdog (bit 31 = on)"),
    (0x600C0058, "SYSTEM_SYSCLK_CONF", "CPU clock (low 12 bits 0 = crystal, 40 MHz)"),
    (0x6000403C, "GPIO_IN", "pin levels, one bit per pin (bit 9 is the button: 0 = pressed)"),
]


def find_tool(name, hint):
    path = shutil.which(name)
    if path is None:
        sys.exit(f"{name} was not found. {hint}")
    return path


def symbol_addresses(elf, names):
    """Look up global variables in the ELF file with nm: {name: address}."""
    nm = find_tool("riscv32-esp-elf-nm", "Run this from an ESP-IDF terminal.")
    output = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    missing = [n for n in names if n not in table]
    if missing:
        sys.exit(f"not found in {elf}: {', '.join(missing)}")
    return {n: table[n] for n in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("variables", nargs="*", help="global variables to read (32-bit values)")
    parser.add_argument("--elf", default=DEFAULT_ELF, help="the program's ELF file, for variable addresses")
    args = parser.parse_args()

    variables = symbol_addresses(args.elf, args.variables) if args.variables else {}
    openocd = find_tool("openocd", "Run this from an ESP-IDF terminal, which puts openocd-esp32 on the PATH.")

    # One OpenOCD session: halt, print everything with "echo", then resume.
    # Always resume before shutting down: a CPU left halted stays halted.
    tcl = ["init", "halt"]
    tcl += [f"echo \"CPU {name} [reg {name}]\"" for name, _ in CPU_REGISTERS]
    tcl += [f"echo \"MEM [mdw 0x{addr:08x}]\"" for addr, _, _ in HARDWARE_REGISTERS]
    tcl += [f"echo \"MEM [mdw 0x{addr:08x}]\"" for addr in variables.values()]
    tcl += ["resume", "shutdown"]
    command = [openocd,
               "-c", "set ESP_FLASH_SIZE 0",      # don't look for an ESP-IDF app in flash
               "-c", "set ESP_RTOS none",         # there is no FreeRTOS
               "-f", "board/esp32c3-builtin.cfg",
               "-c", "; ".join(tcl)]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("OpenOCD did not finish within 60 s. Unplug the board and plug it back in, then try again.")
    output = result.stdout + result.stderr

    cpu = dict(re.findall(r"^CPU (\w+) \w+ \(/\d+\): (0x[0-9a-fA-F]+)", output, re.MULTILINE))
    mem = {int(a, 16): int(v, 16) for a, v in re.findall(r"^MEM 0x([0-9a-fA-F]+): ([0-9a-fA-F]+)", output, re.MULTILINE)}
    if not cpu:
        print(output[-2000:])
        sys.exit("Could not read the CPU. Is the board plugged in, and no other debugger running?")

    print("CPU registers (the CPU was stopped for a moment and is running again)")
    for name, meaning in CPU_REGISTERS:
        print(f"  {name:7} {cpu.get(name, '?'):>10}   {meaning}")
    print("\nHardware registers")
    for addr, name, meaning in HARDWARE_REGISTERS:
        value = f"0x{mem[addr]:08x}" if addr in mem else "?"
        print(f"  {name:20} {value}   {meaning}")
    if variables:
        print(f"\nVariables ({os.path.relpath(args.elf)})")
        for name, addr in variables.items():
            if addr in mem:
                print(f"  {name:20} 0x{mem[addr]:08x} = {mem[addr]}")
            else:
                print(f"  {name:20} ?")


if __name__ == "__main__":
    main()
