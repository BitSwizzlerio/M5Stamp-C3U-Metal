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

$root   = $PSScriptRoot
$out    = Join-Path $root 'build'
$luaOut = Join-Path $out 'lua'
New-Item -ItemType Directory -Force $out, $luaOut | Out-Null
$elf    = Join-Path $out 'c3u-metal.elf'
$bin    = Join-Path $out 'c3u-metal.bin'
$liblua = Join-Path $out 'liblua.a'

# The ESP32-C3's instruction set; matches the toolchain's libraries
$arch = @('-march=rv32imc_zicsr_zifencei', '-mabi=ilp32')

# Lua's configuration. Lua and our C code must agree on it, or they disagree about what a Lua number is.
$luaSrc    = Join-Path $root 'third_party\lua\src'
$luaConfig = @('-DLUA_32BITS', "-I$luaSrc")                    # 32-bit integers and floats

# --- 1. Lua library: third_party\lua\src, unmodified. Rebuilt only when its source or this script changes. ---
$luaCore  = 'lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio'
$luaLibs  = 'lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib'     # no io, os, package, debug or linit
$luaFiles = "$luaCore $luaLibs".Split(' ') | ForEach-Object { Join-Path $luaSrc "$_.c" }

$newestInput = (Get-ChildItem "$luaSrc\*.[ch]", $PSCommandPath | Measure-Object LastWriteTime -Maximum).Maximum
if (-not (Test-Path $liblua) -or (Get-Item $liblua).LastWriteTime -lt $newestInput) {
    Write-Host 'Building liblua.a (Lua 5.5.1) ...'
    Remove-Item -Path "$luaOut\*.o", $liblua -ErrorAction SilentlyContinue
    Push-Location $luaOut                                          # gcc -c writes each .o into the current folder
    riscv32-esp-elf-gcc @($arch + @('-O2', '-g', '-Wall', '-Wextra') + $luaConfig + @('-c') + $luaFiles)
    $code = $LASTEXITCODE
    Pop-Location
    if ($code) { exit $code }
    riscv32-esp-elf-ar rcs $liblua (Get-ChildItem "$luaOut\*.o").FullName
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

# --- 2. The program: our linker script and C runtime, Lua, and newlib as the C library. No ESP-IDF. ---
$includeDirs = 'boot', 'chip', 'board', 'drivers', 'libc', 'app' | ForEach-Object { "-I$root\$_" }
$sources = @(
    'boot\crt0.S', 'boot\stack_check.c',            # the C runtime: the chip starts running here
    'chip\system_esp32c3.c', 'chip\cpu.S', 'chip\trap.S',   # chip setup, CPU helpers, traps
    'drivers\gpio.c', 'drivers\sk6812.c', 'drivers\sk6812.S', 'drivers\usb_serial.c', 'drivers\uptime.c',
    'libc\syscalls.c',                              # what newlib needs from an "operating system"
    'app\main.c', 'app\repl.c', 'app\lua_hw.c', 'app\lua_sys.c', 'app\trap_report.c'
) | ForEach-Object { Join-Path $root $_ }

$flags = $arch + @(
    '-g', '-Og', '-Wall', '-Wextra',
    '-fno-asynchronous-unwind-tables', '-fno-unwind-tables'
) + $luaConfig + $includeDirs + @(
    '-nostdlib', '-nostartfiles',                        # our own crt0.S; libraries are listed explicitly below
    '-T', "$root\boot\c3u-metal.ld",
    "-Wl,-Map=$out\c3u-metal.map"
) + $sources + @(
    $liblua,
    '-Wl,--start-group', '-lc', '-lm', '-lgcc', '-Wl,--end-group',   # newlib, its maths library, compiler helpers
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
