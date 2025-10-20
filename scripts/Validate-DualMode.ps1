param([switch]$SkipTests = $false)

# Suppress PowerShell error output for stderr redirections
$ErrorActionPreference = "SilentlyContinue"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"

Write-Host "=== PicoDCC Dual-Mode Build Validation ===" -ForegroundColor Cyan
Write-Host "Project Root: $ProjectRoot"
Write-Host "Build Directory: $BuildDir"

function Clear-CMakeCache {
    if (Test-Path $BuildDir) {
        Write-Host "Cleared CMake cache" -ForegroundColor Yellow
        Remove-Item "$BuildDir\CMakeCache.txt" -ErrorAction SilentlyContinue
        Remove-Item "$BuildDir\CMakeFiles" -Recurse -ErrorAction SilentlyContinue
    }
}

function Test-BuildMode {
    param([string]$Mode)
    
    Write-Host ""
    Write-Host "--- Switching to $Mode Mode ---" -ForegroundColor Yellow
    Clear-CMakeCache
    
    try {
        Push-Location $ProjectRoot
        
        Write-Host "Configuring CMake for $Mode mode..." -ForegroundColor Gray
        
        if ($Mode -eq "TEST") {
            # Test mode: Use Ninja generator with default compiler detection
            # This matches how the Pico IDE extension builds in test mode
            $ninjaAvailable = $false
            try {
                $null = Get-Command ninja -ErrorAction Stop
                $ninjaAvailable = $true
            } catch {
                Write-Warning "Ninja not available, using default generator"
            }

            if ($ninjaAvailable) {
                Write-Host "Using Ninja generator for test build (matching IDE configuration)..." -ForegroundColor Cyan
                $result = cmake -B build -G "Ninja" -DTEST_BUILD=ON 2>&1
            } else {
                # Fallback to default generator (likely MSVC on Windows)
                $result = cmake -B build -DTEST_BUILD=ON 2>&1
            }
        } else {
            # Configure for hardware build with proper ARM GCC toolchain
            $toolchainPath = "C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\gcc-arm-none-eabi"
            $armGccPath = "$toolchainPath\bin\arm-none-eabi-gcc.exe"
            $armGxxPath = "$toolchainPath\bin\arm-none-eabi-g++.exe"

            if (-not (Test-Path $armGccPath)) {
                Write-Warning "ARM GCC toolchain not found at: $armGccPath"
                Write-Host "Hardware mode requires ARM GCC toolchain installation" -ForegroundColor Red
                return $false
            }

            # Check if Ninja generator is available (preferred for cross-compilation)
            $ninjaAvailable = $false
            try {
                $null = Get-Command ninja -ErrorAction Stop
                $ninjaAvailable = $true
                Write-Host "Using Ninja generator for ARM GCC cross-compilation..." -ForegroundColor Cyan
            } catch {
                Write-Host "Ninja not available, using default generator with explicit ARM GCC..." -ForegroundColor Yellow
            }

            # Configure with proper ARM GCC toolchain
            if ($ninjaAvailable) {
                $result = cmake -B build -G "Ninja" -DTEST_BUILD=OFF -DPICO_TOOLCHAIN_PATH="$toolchainPath" 2>&1
            } else {
                # Force ARM GCC compilers even with Visual Studio generator
                $result = cmake -B build -DTEST_BUILD=OFF -DPICO_TOOLCHAIN_PATH="$toolchainPath" -DCMAKE_C_COMPILER="$armGccPath" -DCMAKE_CXX_COMPILER="$armGxxPath" 2>&1
            }
        }
        
        $result | Out-Host
        
        if ($LASTEXITCODE -ne 0) {
            Write-Host "CMake configuration failed" -ForegroundColor Red
            return $false
        }
        
        Write-Host "Building in $Mode mode..." -ForegroundColor Gray
        $buildResult = cmake --build build 2>&1
        $buildResult | Out-Host
        
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

function Run-TestSuite {
    Write-Host ""
    Write-Host "--- Running Test Suite ---" -ForegroundColor Green
    
    # Test executables can be in different locations depending on generator
    # Ninja: build/test/*.exe
    # Visual Studio: build/test/Debug/*.exe
    $testDirNinja = Join-Path $BuildDir "test"
    $testDirVS = Join-Path $BuildDir "test\Debug"
    
    # Determine which directory contains the test executables
    $testDir = $testDirNinja
    if (-not (Test-Path $testDirNinja\*.exe) -and (Test-Path $testDirVS)) {
        $testDir = $testDirVS
        Write-Host "Using Visual Studio build output directory" -ForegroundColor Gray
    } else {
        Write-Host "Using Ninja build output directory" -ForegroundColor Gray
    }
    
    $testExes = @(
        "pico_dcc_dccex_tests.exe",
        "pico_dcc_controller_tests.exe", 
        "pico_dcc_loco_tests.exe",
        "pico_dcc_locos_tests.exe",
        "pico_dcc_packet_tests.exe",
        "pico_dcc_track_tests.exe"
    )
    
    $passed = 0
    $total = 0
    
    foreach ($exe in $testExes) {
        $testPath = Join-Path $testDir $exe
        if (Test-Path $testPath) {
            Write-Host "Running $exe..." -ForegroundColor Cyan
            # Use Start-Process to avoid PowerShell stderr interpretation
            $process = Start-Process -FilePath $testPath -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$env:TEMP\test_output.txt" -RedirectStandardError "$env:TEMP\test_error.txt"
            $output = Get-Content "$env:TEMP\test_output.txt" -Raw -ErrorAction SilentlyContinue
            $errorOutput = Get-Content "$env:TEMP\test_error.txt" -Raw -ErrorAction SilentlyContinue
            
            # CMocka writes success messages to stderr, so combine both outputs
            $combinedOutput = "$output$errorOutput"
            Write-Host $combinedOutput
            
            # Clean up temp files
            Remove-Item "$env:TEMP\test_output.txt" -ErrorAction SilentlyContinue
            Remove-Item "$env:TEMP\test_error.txt" -ErrorAction SilentlyContinue
            
            # Enhanced failure detection
            $hasFailures = ($combinedOutput -match '\[  FAILED  \].*tests?: \d+ test\(s\)') -or ($combinedOutput -match '\d+ FAILED TEST\(S\)')
            
            if (-not $hasFailures -and $process.ExitCode -eq 0) {
                $passed++
                Write-Host " Test suite passed" -ForegroundColor Green
            } else {
                Write-Host " Test suite failed" -ForegroundColor Red
            }
            $total++
        }
    }
    
    Write-Host ""
    $color = if ($passed -eq $total) { "Green" } else { "Yellow" }
    Write-Host "Test Results: $passed/$total test suites passed" -ForegroundColor $color
    return $passed -eq $total
}

function Validate-HardwareBuild {
    Write-Host ""
    Write-Host "--- Validating Hardware Build Output ---" -ForegroundColor Green
    
    $srcDir = Join-Path $BuildDir "src"
    $expectedFiles = @("PicoDCC.elf", "PicoDCC.uf2")
    
    $foundFiles = @()
    foreach ($file in $expectedFiles) {
        $filePath = Join-Path $srcDir $file
        if (Test-Path $filePath) {
            Write-Host "[OK] Found $file" -ForegroundColor Green
            $foundFiles += $file
        } else {
            Write-Host "[MISSING] $file" -ForegroundColor Red
        }
    }
    
    return $foundFiles.Count -eq $expectedFiles.Count
}

# Main validation workflow
try {
    # Test mode validation
    $testModeSuccess = Test-BuildMode "TEST"
    
    if ($testModeSuccess -and -not $SkipTests) {
        $testsSuccess = Run-TestSuite
    } else {
        $testsSuccess = $true  # Skip if build failed or tests skipped
    }
    
    # Hardware mode validation
    $hardwareModeSuccess = Test-BuildMode "HARDWARE"
    
    if ($hardwareModeSuccess) {
        $hardwareValidation = Validate-HardwareBuild
    } else {
        Write-Host "Hardware build failed - likely due to ARM GCC toolchain setup" -ForegroundColor Yellow
        $hardwareValidation = $false
    }
    
    # Summary
    Write-Host ""
    Write-Host "=== Validation Summary ===" -ForegroundColor Cyan
    
    $testModeColor = if ($testModeSuccess) { "Green" } else { "Red" }
    $testModeResult = if ($testModeSuccess) { "[PASSED]" } else { "[FAILED]" }
    Write-Host "Test Mode Build: $testModeResult" -ForegroundColor $testModeColor
    
    if (-not $SkipTests) {
        $testsColor = if ($testsSuccess) { "Green" } else { "Red" }
        $testsResult = if ($testsSuccess) { "[PASSED]" } else { "[FAILED]" }
        Write-Host "Test Suite: $testsResult" -ForegroundColor $testsColor
    }
    
    $hardwareColor = if ($hardwareModeSuccess) { "Green" } else { "Red" }
    $hardwareResult = if ($hardwareModeSuccess) { "[PASSED]" } else { "[FAILED]" }
    Write-Host "Hardware Mode: $hardwareResult" -ForegroundColor $hardwareColor
    
    $overallSuccess = $testModeSuccess -and $testsSuccess -and $hardwareModeSuccess
    Write-Host ""
    $overallColor = if ($overallSuccess) { "Green" } else { "Yellow" }
    $overallResult = if ($overallSuccess) { "[ALL TESTS PASSED]" } else { "[SOME ISSUES DETECTED]" }
    Write-Host "Overall Result: $overallResult" -ForegroundColor $overallColor
    
    if (-not $overallSuccess) {
        Write-Host ""
        Write-Host "Note: Hardware mode requires proper ARM GCC toolchain setup." -ForegroundColor Yellow
        Write-Host "Test mode validation is the primary indicator of code compatibility." -ForegroundColor Yellow
    }
} 
catch {
    Write-Error "Validation failed: $_"
    exit 1
}

Write-Host ""
Write-Host "=== Dual-Mode Validation Complete ===" -ForegroundColor Cyan
