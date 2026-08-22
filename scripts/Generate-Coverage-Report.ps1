#!/usr/bin/env pwsh
# Generate-Coverage-Report.ps1
# Generates code coverage reports for PicoDCC project using gcov

param(
    [string]$BuildDir = "build/host",
    [switch]$Detailed = $false
)

$ErrorActionPreference = "Stop"
$WorkspaceRoot = Split-Path -Parent $PSScriptRoot

Write-Host "`n=== PicoDCC Code Coverage Report Generation ===" -ForegroundColor Cyan
Write-Host "Workspace: $WorkspaceRoot"
Write-Host "Build Directory: $BuildDir"
Write-Host ""

# Check if build directory exists
$BuildPath = Join-Path $WorkspaceRoot $BuildDir
if (-not (Test-Path $BuildPath)) {
    Write-Host "[ERROR] Build directory not found: $BuildPath" -ForegroundColor Red
    exit 1
}

# Check if coverage data exists
$GcdaFiles = Get-ChildItem -Path $BuildPath -Recurse -Filter "*.gcda"
if ($GcdaFiles.Count -eq 0) {
    Write-Host "[ERROR] No coverage data files (*.gcda) found" -ForegroundColor Red
    Write-Host "Please build with coverage flags and run tests first:" -ForegroundColor Yellow
    Write-Host "  cmake -G 'Ninja' -DTEST_BUILD=ON -DCMAKE_CXX_FLAGS='--coverage' .." -ForegroundColor Yellow
    Write-Host "  cmake --build ." -ForegroundColor Yellow
    Write-Host "  .\test\pico_dcc_controller_tests.exe" -ForegroundColor Yellow
    exit 1
}

Write-Host "Found $($GcdaFiles.Count) coverage data files" -ForegroundColor Green

# Components to analyze
$Components = @(
    @{Name="PicoDCCController"; Path="lib\PicoDCCController\CMakeFiles\PicoDCCController.dir"; Source="pico_dcccontroller.cpp"},
    @{Name="PicoConfigStorage"; Path="lib\PicoConfigStorage\CMakeFiles\PicoConfigStorage.dir"; Source="pico_config_storage.cpp"},
    @{Name="PicoDCCEX"; Path="lib\PicoDCCEX\CMakeFiles\PicoDCCEX.dir"; Source="pico_dccex.cpp"},
    @{Name="PicoDCCLoco"; Path="lib\PicoDCCLoco\CMakeFiles\PicoDCCLoco.dir"; Source="pico_dccloco.cpp"},
    @{Name="PicoDCCLocos"; Path="lib\PicoDCCLoco\CMakeFiles\PicoDCCLoco.dir"; Source="pico_dcclocos.cpp"},
    @{Name="PicoDCCTrack"; Path="lib\PicoDCCTrack\CMakeFiles\PicoDCCTrack.dir"; Source="pico_dcctrack.cpp"},
    @{Name="PicoDiagnostic"; Path="lib\PicoDiagnostic\CMakeFiles\PicoDiagnostic.dir"; Source="pico_diagnostic.cpp"}
)

$CoverageResults = @()

foreach ($Component in $Components) {
    $ComponentPath = Join-Path $BuildPath $Component.Path
    
    if (-not (Test-Path $ComponentPath)) {
        Write-Host "[SKIP] $($Component.Name): Directory not found" -ForegroundColor Yellow
        continue
    }
    
    Push-Location $ComponentPath
    
    # Check for gcno/gcda files
    $GcnoFile = "$($Component.Source).gcno"
    $GcdaFile = "$($Component.Source).gcda"
    
    if (-not (Test-Path $GcnoFile) -or -not (Test-Path $GcdaFile)) {
        Write-Host "[SKIP] $($Component.Name): Coverage files not found" -ForegroundColor Yellow
        Pop-Location
        continue
    }
    
    # Run gcov
    $GcovOutput = gcov $GcnoFile 2>&1 | Out-String
    
    # Parse coverage percentage
    if ($GcovOutput -match "File '.*$($Component.Source)'[\r\n]+Lines executed:(\d+\.\d+)% of (\d+)") {
        $Coverage = [double]$matches[1]
        $TotalLines = [int]$matches[2]
        $ExecutedLines = [int]($TotalLines * $Coverage / 100)
        
        $CoverageResults += [PSCustomObject]@{
            Component = $Component.Name
            Coverage = $Coverage
            ExecutedLines = $ExecutedLines
            TotalLines = $TotalLines
            GcovFile = "$($Component.Source).gcov"
        }
        
        Write-Host "[OK] $($Component.Name): $Coverage% ($ExecutedLines/$TotalLines lines)" -ForegroundColor Green
    } else {
        Write-Host "[WARN] $($Component.Name): Could not parse coverage data" -ForegroundColor Yellow
    }
    
    Pop-Location
}

# Generate summary report
Write-Host "`n=== Coverage Summary ===" -ForegroundColor Cyan
Write-Host ""

if ($CoverageResults.Count -eq 0) {
    Write-Host "[ERROR] No coverage data collected" -ForegroundColor Red
    exit 1
}

# Sort by coverage percentage (ascending to show lowest coverage first)
$CoverageResults | Sort-Object Coverage | ForEach-Object {
    $BarLength = [int]($_.Coverage / 2)  # Scale to 50 chars max
    $Bar = "#" * $BarLength + "-" * (50 - $BarLength)
    
    $Color = if ($_.Coverage -ge 80) { "Green" } 
             elseif ($_.Coverage -ge 60) { "Yellow" } 
             else { "Red" }
    
    Write-Host ("{0,-20} [{1}] {2,5:N1}% ({3}/{4} lines)" -f `
        $_.Component, $Bar, $_.Coverage, $_.ExecutedLines, $_.TotalLines) -ForegroundColor $Color
}

# Calculate overall statistics
$TotalExecuted = ($CoverageResults | Measure-Object -Property ExecutedLines -Sum).Sum
$TotalLines = ($CoverageResults | Measure-Object -Property TotalLines -Sum).Sum
$OverallCoverage = if ($TotalLines -gt 0) { ($TotalExecuted / $TotalLines) * 100 } else { 0 }

Write-Host "`n--- Overall Statistics ---" -ForegroundColor Cyan
Write-Host "Total Lines Covered: $TotalExecuted / $TotalLines"
Write-Host "Overall Coverage: $($OverallCoverage.ToString('N2'))%" -ForegroundColor $(
    if ($OverallCoverage -ge 80) { "Green" } 
    elseif ($OverallCoverage -ge 60) { "Yellow" } 
    else { "Red" }
)

# Detailed report locations
if ($Detailed) {
    Write-Host "`n--- Detailed Coverage Reports ---" -ForegroundColor Cyan
    foreach ($Result in $CoverageResults) {
        $Component = $Components | Where-Object { $_.Name -eq $Result.Component }
        $GcovPath = Join-Path $BuildPath $Component.Path $Result.GcovFile
        if (Test-Path $GcovPath) {
            Write-Host "$($Result.Component): $GcovPath"
        }
    }
}

Write-Host "`n=== Coverage Report Generation Complete ===" -ForegroundColor Cyan
Write-Host ""

# Return results for potential further processing
return $CoverageResults
