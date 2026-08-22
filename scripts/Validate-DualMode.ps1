param([switch]$SkipTests = $false)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot

# One tree per mode, as defined by CMakePresets.json. They are independent, so
# validating both no longer means rebuilding either from scratch.
$HostBuildDir = Join-Path $ProjectRoot "build\host"
$PicoBuildDir = Join-Path $ProjectRoot "build\pico"

Write-Host "=== PicoDCC Dual-Mode Build Validation ===" -ForegroundColor Cyan
Write-Host "Project Root: $ProjectRoot"
Write-Host "Host tests:   $HostBuildDir"
Write-Host "Firmware:     $PicoBuildDir"

# --- Environment discovery -------------------------------------------------
#
# The Pico VS Code extension installs everything under ~/.pico-sdk and exports
# PICO_SDK_PATH / PICO_TOOLCHAIN_PATH into the integrated terminal (see
# .vscode/settings.json). Outside that terminal the variables may be unset, so
# fall back to discovering the newest install under ~/.pico-sdk rather than
# assuming a fixed location.

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

function Resolve-Ninja {
    $onPath = Get-Command ninja -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $ninjaDir = Find-NewestUnder (Join-Path $HOME ".pico-sdk\ninja") "ninja.exe"
    if ($ninjaDir) { return (Join-Path $ninjaDir "ninja.exe") }
    return $null
}

function Test-LvglSubmodule {
    # The hardware build fails at add_subdirectory with no useful hint when the
    # submodule is missing, so check for it up front and say what to run.
    $lvglCMake = Join-Path $ProjectRoot "lib\external\lvgl\CMakeLists.txt"
    if (Test-Path $lvglCMake) { return $true }

    Write-Host "LVGL submodule is not checked out." -ForegroundColor Red
    Write-Host "  Run: git submodule update --init --depth 1 lib/external/lvgl" -ForegroundColor Yellow
    return $false
}

function Invoke-Build {
    param([string]$Preset, [string]$Mode, [string[]]$ConfigureArgs = @())

    Write-Host ""
    Write-Host "--- Building $Mode Mode (preset: $Preset) ---" -ForegroundColor Yellow

    try {
        Push-Location $ProjectRoot

        # No cache clearing: each preset owns its own binaryDir, so the two modes
        # cannot overwrite one another and neither needs wiping to switch.
        Write-Host "Configuring CMake for $Mode mode..." -ForegroundColor Gray
        cmake --preset $Preset @ConfigureArgs 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Host "CMake configuration failed" -ForegroundColor Red
            return $false
        }

        Write-Host "Building in $Mode mode..." -ForegroundColor Gray
        cmake --build --preset $Preset 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Build failed" -ForegroundColor Red
            return $false
        }

        Write-Host "Build successful" -ForegroundColor Green
        return $true
    }
    finally {
        Pop-Location
    }
}

function Invoke-TestSuite {
    # ctest is the source of truth for which suites exist; enumerating the
    # executables here just goes stale every time test/CMakeLists.txt changes.
    Write-Host ""
    Write-Host "--- Running Test Suite ---" -ForegroundColor Green

    try {
        Push-Location $ProjectRoot
        ctest --preset host 2>&1 | Out-Host
        return $LASTEXITCODE -eq 0
    }
    finally {
        Pop-Location
    }
}

function Test-HardwareArtifacts {
    Write-Host ""
    Write-Host "--- Validating Hardware Build Output ---" -ForegroundColor Green

    $srcDir = Join-Path $PicoBuildDir "src"
    $expectedFiles = @("PicoDCC.elf", "PicoDCC.uf2")

    $found = 0
    foreach ($file in $expectedFiles) {
        if (Test-Path (Join-Path $srcDir $file)) {
            Write-Host "[OK] Found $file" -ForegroundColor Green
            $found++
        } else {
            Write-Host "[MISSING] $file" -ForegroundColor Red
        }
    }

    return $found -eq $expectedFiles.Count
}

# --- Main validation workflow ----------------------------------------------

$ninja = Resolve-Ninja
if (-not $ninja) {
    Write-Host "Ninja not found on PATH or under ~/.pico-sdk/ninja." -ForegroundColor Red
    Write-Host "Both build modes require the Ninja generator." -ForegroundColor Yellow
    exit 1
}
Write-Host "Ninja: $ninja" -ForegroundColor Gray

# Test mode: host compiler, no Pico SDK involvement.
$testModeSuccess = Invoke-Build "host" "TEST"

if ($testModeSuccess -and -not $SkipTests) {
    $testsSuccess = Invoke-TestSuite
} else {
    $testsSuccess = $true
}

# Hardware mode: ARM GCC cross-build.
$sdkPath = Resolve-PicoSdk
$toolchainPath = Resolve-ArmToolchain

$hardwareModeSuccess = $false
$hardwareArtifacts = $false
$hardwareSkipped = $false

if (-not $sdkPath) {
    Write-Host ""
    Write-Host "Pico SDK not found. Set PICO_SDK_PATH or install the SDK under ~/.pico-sdk/sdk." -ForegroundColor Red
    $hardwareSkipped = $true
} elseif (-not $toolchainPath) {
    Write-Host ""
    Write-Host "ARM GCC toolchain not found." -ForegroundColor Red
    Write-Host "Set PICO_TOOLCHAIN_PATH or install it under ~/.pico-sdk/toolchain." -ForegroundColor Yellow
    $hardwareSkipped = $true
} elseif (-not (Test-LvglSubmodule)) {
    $hardwareSkipped = $true
} else {
    Write-Host ""
    Write-Host "Pico SDK:      $sdkPath" -ForegroundColor Gray
    Write-Host "ARM toolchain: $toolchainPath" -ForegroundColor Gray

    $env:PICO_SDK_PATH = $sdkPath
    $env:PICO_TOOLCHAIN_PATH = $toolchainPath

    $hardwareModeSuccess = Invoke-Build "pico" "HARDWARE" @(
        "-DPICO_SDK_PATH=$sdkPath",
        "-DPICO_TOOLCHAIN_PATH=$toolchainPath"
    )

    if ($hardwareModeSuccess) {
        $hardwareArtifacts = Test-HardwareArtifacts
    }
}

# --- Summary ---------------------------------------------------------------

Write-Host ""
Write-Host "=== Validation Summary ===" -ForegroundColor Cyan

function Write-Result {
    param([string]$Label, [bool]$Ok)
    $color = if ($Ok) { "Green" } else { "Red" }
    $text = if ($Ok) { "[PASSED]" } else { "[FAILED]" }
    Write-Host "${Label}: $text" -ForegroundColor $color
}

Write-Result "Test Mode Build" $testModeSuccess
if (-not $SkipTests) { Write-Result "Test Suite" $testsSuccess }

if ($hardwareSkipped) {
    Write-Host "Hardware Mode: [SKIPPED - toolchain unavailable]" -ForegroundColor Yellow
} else {
    Write-Result "Hardware Mode" $hardwareModeSuccess
    if ($hardwareModeSuccess) { Write-Result "Hardware Artifacts" $hardwareArtifacts }
}

$overallSuccess = $testModeSuccess -and $testsSuccess -and $hardwareModeSuccess -and $hardwareArtifacts

Write-Host ""
if ($overallSuccess) {
    Write-Host "Overall Result: [ALL CHECKS PASSED]" -ForegroundColor Green
} else {
    Write-Host "Overall Result: [SOME ISSUES DETECTED]" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Dual-Mode Validation Complete ===" -ForegroundColor Cyan

# Both trees are left configured and warm. There is no mode to restore and no
# cache to clear: build/host and build/pico are independent.
if (-not $overallSuccess) { exit 1 }
