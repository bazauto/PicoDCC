# Generate LCov coverage data for CMake Tools Test Explorer
# This script converts gcov data to lcov format for CMake Test Explorer integration

$ErrorActionPreference = "Stop"
$buildDir = "$PSScriptRoot\..\build"

Write-Host "`n=== Generating LCov Coverage Data ===" -ForegroundColor Cyan

# Check if we have coverage data
$gcdaFiles = Get-ChildItem -Path $buildDir -Recurse -Filter "*.gcda" -ErrorAction SilentlyContinue
if ($gcdaFiles.Count -eq 0) {
    Write-Host "ERROR: No coverage data files found. Run tests first with coverage enabled." -ForegroundColor Red
    exit 1
}

Write-Host "Found $($gcdaFiles.Count) coverage data files" -ForegroundColor Green

# Check for lcov (if installed via chocolatey or manually)
$lcovPath = $null
$possiblePaths = @(
    "C:\ProgramData\chocolatey\bin\lcov.exe",
    "C:\Program Files\lcov\bin\lcov.exe",
    "C:\lcov\bin\lcov.exe",
    "$env:LOCALAPPDATA\Programs\lcov\bin\lcov.exe"
)

foreach ($path in $possiblePaths) {
    if (Test-Path $path) {
        $lcovPath = $path
        break
    }
}

if (-not $lcovPath) {
    # Try to find lcov in PATH
    $lcovCmd = Get-Command lcov -ErrorAction SilentlyContinue
    if ($lcovCmd) {
        $lcovPath = $lcovCmd.Source
    }
}

if (-not $lcovPath) {
    Write-Host "`nWARNING: lcov not found. CMake Test Explorer coverage requires lcov." -ForegroundColor Yellow
    Write-Host "You can:" -ForegroundColor Yellow
    Write-Host "  1. Install via Chocolatey: choco install lcov" -ForegroundColor Yellow
    Write-Host "  2. Use Coverage Gutters instead (already configured)" -ForegroundColor Yellow
    Write-Host "  3. Use the text-based coverage report: .\scripts\Generate-Coverage-Report.ps1" -ForegroundColor Yellow
    Write-Host "`nFor now, generating gcov files for Coverage Gutters..." -ForegroundColor Cyan
    
    # Fall back to just generating .gcov files
    & "$PSScriptRoot\Convert-Coverage-For-VSCode.ps1"
    exit 0
}

Write-Host "Using lcov at: $lcovPath" -ForegroundColor Green

# Generate lcov coverage data
Write-Host "`nGenerating lcov.info..." -ForegroundColor Cyan

$lcovOutputFile = "$buildDir\coverage\lcov.info"
$lcovDir = Split-Path $lcovOutputFile -Parent
if (-not (Test-Path $lcovDir)) {
    New-Item -ItemType Directory -Path $lcovDir -Force | Out-Null
}

# Run lcov to capture coverage
Push-Location $buildDir

try {
    & $lcovPath --capture --directory . --output-file $lcovOutputFile --gcov-tool gcov
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Successfully generated: $lcovOutputFile" -ForegroundColor Green
        
        # Also generate .gcov files for Coverage Gutters
        Write-Host "`nGenerating .gcov files for Coverage Gutters..." -ForegroundColor Cyan
        Pop-Location
        & "$PSScriptRoot\Convert-Coverage-For-VSCode.ps1"
        Push-Location $buildDir
        
        Write-Host "`nCoverage data ready!" -ForegroundColor Green
        Write-Host "  - CMake Test Explorer: Will use $lcovOutputFile" -ForegroundColor White
        Write-Host "  - Coverage Gutters: Use Ctrl+Shift+7 in source files" -ForegroundColor White
    } else {
        Write-Host "ERROR: lcov failed with exit code $LASTEXITCODE" -ForegroundColor Red
        exit 1
    }
} finally {
    Pop-Location
}
