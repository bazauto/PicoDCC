# PicoDCC Dual-Mode Build Validation Script
# This script validates that changes work in both TEST_BUILD modes

param(
    [switch]$SkipTests,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$CMakeListsPath = Join-Path $ProjectRoot "CMakeLists.txt"

Write-Host "=== PicoDCC Dual-Mode Build Validation ===" -ForegroundColor Cyan
Write-Host "Project Root: $ProjectRoot"
Write-Host "Build Directory: $BuildDir"

function Test-BuildMode {
    param([string]$Mode)
    
    Write-Host "`n--- Switching to $Mode Mode ---" -ForegroundColor Yellow
    
    # Update CMakeLists.txt
    $content = Get-Content $CMakeListsPath
    if ($Mode -eq "TEST") {
        $content = $content -replace 'set\(TEST_BUILD OFF', 'set(TEST_BUILD ON'
    } else {
        $content = $content -replace 'set\(TEST_BUILD ON', 'set(TEST_BUILD OFF'
    }
    Set-Content $CMakeListsPath $content
    
    # Clear cmake cache and reconfigure
    Push-Location $BuildDir
    try {
        if (Test-Path "CMakeCache.txt") {
            Remove-Item "CMakeCache.txt" -Force
            Write-Host "Cleared CMake cache"
        }
        
        Write-Host "Configuring CMake for $Mode mode..."
        if ($Mode -eq "HARDWARE") {
            # Configure for hardware build with proper ARM GCC toolchain
            $toolchainPath = "C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\gcc-arm-none-eabi"
            $armGccPath = "$toolchainPath\bin\arm-none-eabi-gcc.exe"
            $armGxxPath = "$toolchainPath\bin\arm-none-eabi-g++.exe"
            
            if (-not (Test-Path $armGccPath)) {
                Write-Warning "ARM GCC toolchain not found at: $armGccPath"
                throw "Hardware mode requires ARM GCC toolchain installation"
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
                cmake -G "Ninja" .. -DPICO_TOOLCHAIN_PATH="$toolchainPath" | Out-Host
            } else {
                # Force ARM GCC compilers even with Visual Studio generator
                cmake .. -DPICO_TOOLCHAIN_PATH="$toolchainPath" -DCMAKE_C_COMPILER="$armGccPath" -DCMAKE_CXX_COMPILER="$armGxxPath" | Out-Host
            }
        } else {
            cmake .. | Out-Host
        }
        
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed for $Mode mode"
        }
        
        Write-Host "Building in $Mode mode..."
        cmake --build . | Out-Host
        
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Build failed in $Mode mode - this may be expected for hardware mode without proper ARM GCC setup"
            return $false
        }
        
        return $true
    }
    finally {
        Pop-Location
    }
}

function Run-TestSuite {
    Write-Host "`n--- Running Test Suite ---" -ForegroundColor Green
    
    Push-Location $BuildDir
    try {
        $testExecutables = @(
            ".\test\Debug\pico_dcc_dccex_tests.exe",
            ".\test\Debug\pico_dcc_controller_tests.exe", 
            ".\test\Debug\pico_dcc_loco_tests.exe",
            ".\test\Debug\pico_dcc_locos_tests.exe",
            ".\test\Debug\pico_dcc_packet_tests.exe",
            ".\test\Debug\pico_dcc_track_tests.exe"
        )
        
        $totalTests = 0
        $passedTests = 0
        
        foreach ($testExe in $testExecutables) {
            if (Test-Path $testExe) {
                Write-Host "Running $(Split-Path $testExe -Leaf)..." -ForegroundColor Cyan
                & $testExe
                if ($LASTEXITCODE -eq 0) {
                    $passedTests++
                }
                $totalTests++
            } else {
                Write-Warning "Test executable not found: $testExe"
            }
        }
        
        Write-Host "`nTest Results: $passedTests/$totalTests test suites passed" -ForegroundColor $(if ($passedTests -eq $totalTests) { "Green" } else { "Yellow" })
        return $passedTests -eq $totalTests
    }
    finally {
        Pop-Location
    }
}

function Validate-HardwareBuild {
    Write-Host "`n--- Validating Hardware Build Output ---" -ForegroundColor Green
    
    $srcDir = Join-Path $BuildDir "src"
    $expectedFiles = @("PicoDCC.elf", "PicoDCC.uf2")
    
    $foundFiles = @()
    foreach ($file in $expectedFiles) {
        $filePath = Join-Path $srcDir $file
        if (Test-Path $filePath) {
            $fileInfo = Get-Item $filePath
            Write-Host "[OK] Found $file ($($fileInfo.Length) bytes, modified $($fileInfo.LastWriteTime))" -ForegroundColor Green
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
    Write-Host "`n=== Validation Summary ===" -ForegroundColor Cyan
    Write-Host "Test Mode Build: $(if ($testModeSuccess) { '[PASSED]' } else { '[FAILED]' })" -ForegroundColor $(if ($testModeSuccess) { "Green" } else { "Red" })
    if (-not $SkipTests) {
        Write-Host "Test Suite: $(if ($testsSuccess) { '[PASSED]' } else { '[FAILED]' })" -ForegroundColor $(if ($testsSuccess) { "Green" } else { "Red" })
    }
    Write-Host "Hardware Mode: $(if ($hardwareModeSuccess) { '[PASSED]' } else { '[FAILED]' })" -ForegroundColor $(if ($hardwareModeSuccess) { "Green" } else { "Red" })
    
    $overallSuccess = $testModeSuccess -and $testsSuccess -and $hardwareModeSuccess
    Write-Host "`nOverall Result: $(if ($overallSuccess) { '[ALL TESTS PASSED]' } else { '[SOME ISSUES DETECTED]' })" -ForegroundColor $(if ($overallSuccess) { "Green" } else { "Yellow" })
    
    if (-not $overallSuccess) {
        Write-Host "`nNote: Hardware mode requires proper ARM GCC toolchain setup." -ForegroundColor Yellow
        Write-Host "Test mode validation is the primary indicator of code compatibility." -ForegroundColor Yellow
    }
}
catch {
    Write-Error "Validation failed: $_"
    exit 1
}

Write-Host "`n=== Dual-Mode Validation Complete ===" -ForegroundColor Cyan