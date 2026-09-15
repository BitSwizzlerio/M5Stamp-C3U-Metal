# build.ps1 - build C3U-Metal and flash it to the M5Stamp C3U.
# Usage: .\build.ps1            build, then flash
#        .\build.ps1 -NoFlash   build only
param([switch]$NoFlash)

$idfProfile = 'C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1'

# The RISC-V toolchain and esptool come with ESP-IDF; load them if they aren't available yet
if (-not (Get-Command riscv32-esp-elf-gcc -ErrorAction SilentlyContinue) -or
    -not (Get-Command esptool -ErrorAction SilentlyContinue)) {
    . $idfProfile *> $null
}

$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $out | Out-Null
$elf = Join-Path $out 'c3u-metal.elf'
$bin = Join-Path $out 'c3u-metal.bin'

# Compile, assemble and link with our own linker script and C runtime (start.S):
# no C library, no compiler startup files, no ESP-IDF
$flags = @(
    '-march=rv32imc_zicsr_zifencei', '-mabi=ilp32',     # the ESP32-C3's instruction set; matches the toolchain's libgcc
    '-g', '-Og', '-Wall', '-Wextra',
    '-ffreestanding',                                    # no hosted C library
    '-fno-tree-loop-distribute-patterns',                # don't turn loops into memset/memcpy calls (see string.c)
    '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
    '-nostdlib', '-nostartfiles',
    '-T', "$PSScriptRoot\c3u-metal.ld",
    "-Wl,-Map=$out\c3u-metal.map",
    "$PSScriptRoot\start.S", "$PSScriptRoot\cpu.S", "$PSScriptRoot\led.S",
    "$PSScriptRoot\main.c", "$PSScriptRoot\string.c",
    '-lgcc',                                             # compiler helper routines, e.g. 64-bit division
    '-o', $elf
)
riscv32-esp-elf-gcc @flags
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# Raw flash image: starts with the magic number from the linker script
riscv32-esp-elf-objcopy -O binary $elf $bin
if ($LASTEXITCODE) { exit $LASTEXITCODE }
riscv32-esp-elf-size $elf

if ($NoFlash) { exit 0 }

# Find the board: the C3U's USB port has Espressif's IDs (VID 303A, PID 1001)
$board = Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.PNPDeviceID -like 'USB\VID_303A&PID_1001*' -and $_.Name -match 'COM\d+' } |
    Select-Object -First 1
if (-not $board) {
    Write-Host 'M5Stamp C3U not found. Plug it in (hold its button while plugging in if it still is not found).' -ForegroundColor Red
    exit 1
}
$port = [regex]::Match($board.Name, 'COM\d+').Value
Write-Host "Found M5Stamp C3U on $port"

# Write the image at flash offset 0x0, replacing any ESP-IDF bootloader
esptool --chip esp32c3 -p $port write-flash 0x0 $bin
exit $LASTEXITCODE
