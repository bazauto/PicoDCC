# Build, validate and stage PicoDCC firmware for the bench machine.
#
#   scripts\Deploy-Firmware.ps1              # build + validate + stage to bench
#   scripts\Deploy-Firmware.ps1 -NoStage     # build + validate only, stay local
#   scripts\Deploy-Firmware.ps1 -SkipBuild   # re-validate and re-stage what is already built
#
# This script NEVER touches the board. It stops at the point where firmware is
# sitting on the bench machine's filesystem, verified byte-for-byte, ready to
# flash. Flashing is scripts/bench-flash.sh, which is deliberately a separate
# script so that it can stay behind a per-use approval prompt while everything
# here runs unattended.
#
# The validations are the ones that would otherwise have to be reasoned out by
# hand every time, and the config-sector check is the one that actually matters:
# the last 4KB of flash holds PicoConfigStorage, and an image whose LOAD
# segments reach into it would silently destroy calibration on flash.

param(
    [switch]$SkipBuild,
    [switch]$NoStage,
    [string]$BenchHost = "pbarrett@172.18.10.240",
    [string]$RemoteDir = "picodcc-deploy"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir    = Join-Path $ProjectRoot "build"
$ElfPath     = Join-Path $BuildDir "src\PicoDCC.elf"
$Uf2Path     = Join-Path $BuildDir "src\PicoDCC.uf2"

# Last 4KB sector of the 4MB flash. Mirrors CONFIG_FLASH_OFFSET in
# lib/PicoConfigStorage/pico_config_storage.h -- if that moves, this moves.
$ConfigBase  = 0x103FF000
$FlashBase   = 0x10000000

$script:Failed = @()

function Say  { param([string]$m) Write-Host $m }
function Ok   { param([string]$l, [string]$d) Write-Host ("  ok    {0,-26} {1}" -f $l, $d) -ForegroundColor Green }
function Bad  { param([string]$l, [string]$d) Write-Host ("  FAIL  {0,-26} {1}" -f $l, $d) -ForegroundColor Red; $script:Failed += $l }
function Note { param([string]$l, [string]$d) Write-Host ("  --    {0,-26} {1}" -f $l, $d) -ForegroundColor DarkGray }

# --- Environment discovery -------------------------------------------------
# Same approach as Validate-DualMode.ps1: prefer the exported variables, else
# take the newest install under ~/.pico-sdk. Nothing here pins a version.

function Find-NewestUnder {
    param([string]$Root, [string]$Probe)
    if (-not (Test-Path $Root)) { return $null }
    Get-ChildItem $Root -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Where-Object { Test-Path (Join-Path $_.FullName $Probe) } |
        Select-Object -First 1 -ExpandProperty FullName
}

function Resolve-PicoSdk {
    if ($env:PICO_SDK_PATH -and (Test-Path (Join-Path $env:PICO_SDK_PATH "pico_sdk_init.cmake"))) {
        return $env:PICO_SDK_PATH
    }
    return Find-NewestUnder (Join-Path $HOME ".pico-sdk\sdk") "pico_sdk_init.cmake"
}

function Resolve-ArmToolchain {
    $probe = "bin\arm-none-eabi-gcc.exe"
    if ($env:PICO_TOOLCHAIN_PATH -and (Test-Path (Join-Path $env:PICO_TOOLCHAIN_PATH $probe))) {
        return $env:PICO_TOOLCHAIN_PATH
    }
    return Find-NewestUnder (Join-Path $HOME ".pico-sdk\toolchain") $probe
}

Say ""
Say "=== PicoDCC firmware deploy (build + validate + stage) ==="
Say ""
Say "-- environment"

$Sdk = Resolve-PicoSdk
if (-not $Sdk) {
    Bad "pico sdk" "not found under ~/.pico-sdk/sdk and PICO_SDK_PATH unset"
    exit 1
}
Ok "pico sdk" (Split-Path -Leaf $Sdk)

$Toolchain = Resolve-ArmToolchain
if (-not $Toolchain) {
    Bad "arm toolchain" "not found under ~/.pico-sdk/toolchain and PICO_TOOLCHAIN_PATH unset"
    exit 1
}
Ok "arm toolchain" (Split-Path -Leaf $Toolchain)

# The hardware build fails at add_subdirectory with no useful hint when the
# LVGL submodule is missing, so say so up front rather than after a build.
if (-not (Test-Path (Join-Path $ProjectRoot "lib\external\lvgl\CMakeLists.txt"))) {
    Bad "lvgl submodule" "missing -- run: git submodule update --init --depth 1 lib/external/lvgl"
    exit 1
}
Ok "lvgl submodule" "checked out"

$env:PICO_SDK_PATH = $Sdk
$env:PICO_TOOLCHAIN_PATH = $Toolchain
$env:PATH = (Join-Path $Toolchain "bin") + ";" + $env:PATH

$Size    = Join-Path $Toolchain "bin\arm-none-eabi-size.exe"
$Nm      = Join-Path $Toolchain "bin\arm-none-eabi-nm.exe"
$ReadElf = Join-Path $Toolchain "bin\arm-none-eabi-readelf.exe"

# Provenance. A firmware image nobody can trace back to a commit is not one to
# put on the rails, so the state is reported whether or not it is clean.
Push-Location $ProjectRoot
$Commit = (git rev-parse --short HEAD 2>$null)
$Dirty  = (git status --porcelain 2>$null | Measure-Object -Line).Lines
Pop-Location
if ($Dirty -gt 0) { Note "source" "$Commit + $Dirty uncommitted change(s)" }
else             { Ok   "source" "$Commit (clean)" }

# --- Build -----------------------------------------------------------------

if (-not $SkipBuild) {
    Say ""
    Say "-- build (hardware mode)"

    # Both modes share build/, so the cache must go when switching. This is the
    # single most common way to lose an hour on this project.
    if (Test-Path $BuildDir) {
        Remove-Item (Join-Path $BuildDir "CMakeCache.txt") -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $BuildDir "CMakeFiles") -Recurse -Force -ErrorAction SilentlyContinue
        Ok "cmake cache" "cleared (was test mode?)"
    }

    $cfg = & cmake -B $BuildDir -G Ninja -DTEST_BUILD=OFF 2>&1
    if ($LASTEXITCODE -ne 0) {
        $cfg | Select-Object -Last 20 | ForEach-Object { Write-Host "    $_" }
        Bad "configure" "cmake failed"
        exit 1
    }
    Ok "configure" "TEST_BUILD=OFF, Ninja"

    $bld = & cmake --build $BuildDir 2>&1
    if ($LASTEXITCODE -ne 0) {
        $bld | Select-Object -Last 30 | ForEach-Object { Write-Host "    $_" }
        Bad "compile" "build failed"
        exit 1
    }
    $warn = ($bld | Select-String -Pattern "warning:" | Measure-Object).Count
    Ok "compile" "linked ok$(if ($warn) { ", $warn warning(s)" })"
} else {
    Say ""
    Note "build" "skipped (-SkipBuild)"
}

# --- Validate --------------------------------------------------------------

Say ""
Say "-- validate"

if (-not (Test-Path $ElfPath)) { Bad "artifacts" "PicoDCC.elf not found -- build first"; exit 1 }
if (-not (Test-Path $Uf2Path)) { Bad "artifacts" "PicoDCC.uf2 not found -- build first"; exit 1 }
Ok "artifacts" "PicoDCC.elf, PicoDCC.uf2"

# Size, purely informational but the number worth watching across a toolchain
# or SDK bump -- a large unexplained change is a reason to stop and look.
$sizeOut = & $Size $ElfPath 2>&1
$sizeRow = ($sizeOut | Select-Object -Last 1) -split '\s+' | Where-Object { $_ -ne "" }
$text = [int]$sizeRow[0]; $data = [int]$sizeRow[1]; $bss = [int]$sizeRow[2]
Ok "size" ("text {0:N0}  data {1:N0}  bss {2:N0}" -f $text, $data, $bss)

# The check that matters. Every LOAD segment carrying file content into flash
# must end before the config sector. The linker script caps FLASH at 4092k so
# this should never fire -- but "should never fire" is exactly the class of
# assumption worth making a machine check, because the cost of being wrong is
# silently erased calibration on a board that then misreads track current.
$segs = & $ReadElf -l $ElfPath 2>&1 | Select-String -Pattern '^\s+LOAD\s'
$maxEnd = 0
$overlap = @()
foreach ($s in $segs) {
    $f = ($s.ToString().Trim() -split '\s+')
    $phys    = [Convert]::ToUInt32($f[3], 16)
    $fileSiz = [Convert]::ToUInt32($f[4], 16)
    if ($fileSiz -eq 0) { continue }               # BSS-style, occupies no flash
    if ($phys -lt $FlashBase) { continue }         # RAM-resident
    $end = $phys + $fileSiz
    if ($end -gt $maxEnd) { $maxEnd = $end }
    if ($end -gt $ConfigBase) { $overlap += ("0x{0:X8}..0x{1:X8}" -f $phys, $end) }
}

if ($overlap.Count -gt 0) {
    Bad "config sector" ("image reaches into 0x{0:X8}: {1}" -f $ConfigBase, ($overlap -join ", "))
} else {
    $freeKb = [math]::Round(($ConfigBase - $maxEnd) / 1KB, 1)
    Ok "config sector" ("safe -- image ends 0x{0:X8}, {1} KB clear of 0x{2:X8}" -f $maxEnd, $freeKb, $ConfigBase)
}

# Cross-check against the linker's own symbol. If these two disagree the ELF is
# not what the linker script thinks it is, and nothing downstream is trustworthy.
$fbe = & $Nm $ElfPath 2>&1 | Select-String -Pattern '__flash_binary_end'
if ($fbe) {
    $fbeAddr = [Convert]::ToUInt32((($fbe.ToString().Trim() -split '\s+')[0]), 16)
    if ([math]::Abs([int64]$fbeAddr - [int64]$maxEnd) -gt 16) {
        Bad "__flash_binary_end" ("0x{0:X8} disagrees with segment end 0x{1:X8}" -f $fbeAddr, $maxEnd)
    } else {
        Ok "__flash_binary_end" ("0x{0:X8}" -f $fbeAddr)
    }
} else {
    Note "__flash_binary_end" "symbol absent -- custom linker script in use?"
}

$LocalHash = (Get-FileHash $ElfPath -Algorithm SHA256).Hash.ToLower()
Ok "sha256" ($LocalHash.Substring(0, 16) + "...")

if ($script:Failed.Count -gt 0) {
    Say ""
    Write-Host "FAILED: $($script:Failed -join ', ')" -ForegroundColor Red
    Say "Nothing staged."
    exit 1
}

# --- Stage -----------------------------------------------------------------

if ($NoStage) {
    Say ""
    Note "stage" "skipped (-NoStage)"
    Say ""
    Write-Host "VALIDATED (local only)" -ForegroundColor Green
    exit 0
}

Say ""
Say "-- stage to $BenchHost"

& ssh -o BatchMode=yes -o ConnectTimeout=10 $BenchHost "mkdir -p $RemoteDir" 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Bad "ssh" "cannot reach $BenchHost"; exit 1 }
Ok "ssh" "$BenchHost reachable"

& scp -o BatchMode=yes -q $ElfPath $Uf2Path "${BenchHost}:${RemoteDir}/" 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Bad "scp" "transfer failed"; exit 1 }

# Verify what landed rather than trusting scp's exit code. A truncated or
# corrupted image that still flashes cleanly is the failure mode worth ruling
# out, and it costs one round trip.
$RemoteHash = (& ssh -o BatchMode=yes $BenchHost "sha256sum $RemoteDir/PicoDCC.elf" 2>&1) -split '\s+' | Select-Object -First 1
if ($RemoteHash -ne $LocalHash) {
    Bad "transfer" "sha256 mismatch -- local $($LocalHash.Substring(0,16)) vs remote $($RemoteHash.Substring(0,16))"
    exit 1
}
Ok "transfer" "sha256 identical both ends"

# Record what was staged, so the flash script can refuse an image that is not
# the one this run validated.
& ssh -o BatchMode=yes $BenchHost "printf '%s  %s  %s\n' '$LocalHash' '$Commit' '$(Get-Date -Format s)' > $RemoteDir/STAGED" 2>&1 | Out-Null

Say ""
Write-Host "STAGED OK" -ForegroundColor Green
Say "  $BenchHost : ~/$RemoteDir/PicoDCC.elf  ($Commit)"
Say ""
Say "  Nothing has touched the board. To flash (needs approval, track power off):"
Say "    bash scripts/bench.sh flash --expect $LocalHash"
Say ""
# build/ is shared between modes and is now configured for hardware. Say how to
# get back, because the test-mode configure has to run from PowerShell: under
# Git Bash CMake picks up a broken msys64 cc.exe and fails at project().
Say "  build/ is now in hardware mode. To return to tests (from PowerShell, not bash):"
Say "    cmake -B build -G Ninja -DTEST_BUILD=ON; cmake --build build; cd build; ctest"
Say ""
