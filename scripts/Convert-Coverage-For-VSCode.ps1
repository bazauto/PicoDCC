#!/usr/bin/env pwsh
# Convert-Coverage-For-VSCode.ps1
# Generates gcov coverage files in a format that Coverage Gutters extension can read

param(
    [string]$BuildDir = "build/host"
)

$ErrorActionPreference = "Stop"
$WorkspaceRoot = Split-Path -Parent $PSScriptRoot

Write-Host "`n=== Converting Coverage for VS Code ===" -ForegroundColor Cyan
Write-Host "Workspace: $WorkspaceRoot"
Write-Host "Build Directory: $BuildDir"
Write-Host ""

$BuildPath = Join-Path $WorkspaceRoot $BuildDir

# Check if build directory exists
if (-not (Test-Path $BuildPath)) {
    Write-Host "[ERROR] Build directory not found: $BuildPath" -ForegroundColor Red
    exit 1
}

# Check if coverage data exists
$GcdaFiles = Get-ChildItem -Path $BuildPath -Recurse -Filter "*.gcda"
if ($GcdaFiles.Count -eq 0) {
    Write-Host "[ERROR] No coverage data files (*.gcda) found" -ForegroundColor Red
    Write-Host "Please run tests first with coverage enabled." -ForegroundColor Yellow
    exit 1
}

Write-Host "Found $($GcdaFiles.Count) coverage data files" -ForegroundColor Green

# Components to process
$Components = @(
    @{Name="PicoDCCController"; Path="lib\PicoDCCController\CMakeFiles\PicoDCCController.dir"; Source="pico_dcccontroller.cpp"; SourceFile="lib\PicoDCCController\pico_dcccontroller.cpp"},
    @{Name="PicoConfigStorage"; Path="lib\PicoConfigStorage\CMakeFiles\PicoConfigStorage.dir"; Source="pico_config_storage.cpp"; SourceFile="lib\PicoConfigStorage\pico_config_storage.cpp"},
    @{Name="PicoDCCEX"; Path="lib\PicoDCCEX\CMakeFiles\PicoDCCEX.dir"; Source="pico_dccex.cpp"; SourceFile="lib\PicoDCCEX\pico_dccex.cpp"},
    @{Name="PicoDCCLoco"; Path="lib\PicoDCCLoco\CMakeFiles\PicoDCCLoco.dir"; Source="pico_dccloco.cpp"; SourceFile="lib\PicoDCCLoco\pico_dccloco.cpp"},
    @{Name="PicoDCCLocos"; Path="lib\PicoDCCLoco\CMakeFiles\PicoDCCLoco.dir"; Source="pico_dcclocos.cpp"; SourceFile="lib\PicoDCCLoco\pico_dcclocos.cpp"},
    @{Name="PicoDCCTrack"; Path="lib\PicoDCCTrack\CMakeFiles\PicoDCCTrack.dir"; Source="pico_dcctrack.cpp"; SourceFile="lib\PicoDCCTrack\pico_dcctrack.cpp"},
    @{Name="PicoDiagnostic"; Path="lib\PicoDiagnostic\CMakeFiles\PicoDiagnostic.dir"; Source="pico_diagnostic.cpp"; SourceFile="lib\PicoDiagnostic\pico_diagnostic.cpp"}
)

$ProcessedCount = 0

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
    
    # Run gcov to generate .gcov files
    $null = gcov $GcnoFile 2>&1
    
    # Find the generated .gcov file for our source
    $GcovFile = "$($Component.Source).gcov"
    if (Test-Path $GcovFile) {
        # Copy to source directory for reference
        $TargetDir = Join-Path $WorkspaceRoot (Split-Path $Component.SourceFile -Parent)
        $TargetFile = Join-Path $TargetDir "$($Component.Source).gcov"
        
        # Ensure target directory exists
        if (-not (Test-Path $TargetDir)) {
            New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
        }
        
        Copy-Item $GcovFile $TargetFile -Force
        
        Write-Host "[OK] $($Component.Name): $TargetFile" -ForegroundColor Green
        $ProcessedCount++
    } else {
        Write-Host "[WARN] $($Component.Name): .gcov file not generated" -ForegroundColor Yellow
    }
    
    Pop-Location
}

Write-Host "`n=== Conversion Complete ===" -ForegroundColor Cyan
Write-Host "Processed $ProcessedCount components" -ForegroundColor Green

# Also generate lcov.info for better Coverage Gutters compatibility
Write-Host "`nGenerating lcov.info for Coverage Gutters..." -ForegroundColor Cyan
& "$PSScriptRoot\Generate-Lcov-Info.ps1" -BuildDir $BuildDir

Write-Host ""
Write-Host "To view coverage in VS Code:" -ForegroundColor Cyan
Write-Host "  1. Install 'Coverage Gutters' extension (ryanluker.vscode-coverage-gutters)" -ForegroundColor White
Write-Host "  2. Open a source file (e.g., lib/PicoDCCController/pico_dcccontroller.cpp)" -ForegroundColor White
Write-Host "  3. Press Ctrl+Shift+7 or click 'Watch' in the status bar" -ForegroundColor White
Write-Host "  4. Coverage will be displayed in the gutter (green=covered, red=not covered)" -ForegroundColor White
Write-Host ""

return $ProcessedCount
