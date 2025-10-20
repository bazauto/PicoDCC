# Generate-Lcov-Info.ps1
# Converts gcov output to lcov.info format for Coverage Gutters
# This is more compatible than raw .gcov files

param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$WorkspaceRoot = Split-Path -Parent $PSScriptRoot

Write-Host "`n=== Generating lcov.info for Coverage Gutters ===" -ForegroundColor Cyan

$BuildPath = Join-Path $WorkspaceRoot $BuildDir

# Check if coverage data exists
$GcdaFiles = Get-ChildItem -Path $BuildPath -Recurse -Filter "*.gcda"
if ($GcdaFiles.Count -eq 0) {
    Write-Host "[ERROR] No coverage data files found. Run tests first." -ForegroundColor Red
    exit 1
}

Write-Host "Found $($GcdaFiles.Count) coverage data files" -ForegroundColor Green

# Components to process
$Components = @(
    @{Name="PicoDCCController"; Path="lib\PicoDCCController\CMakeFiles\PicoDCCController.dir"; Source="pico_dcccontroller.cpp"},
    @{Name="PicoConfigStorage"; Path="lib\PicoConfigStorage\CMakeFiles\PicoConfigStorage.dir"; Source="pico_config_storage.cpp"},
    @{Name="PicoDCCEX"; Path="lib\PicoDCCEX\CMakeFiles\PicoDCCEX.dir"; Source="pico_dccex.cpp"},
    @{Name="PicoDCCLoco"; Path="lib\PicoDCCLoco\CMakeFiles\PicoDCCLoco.dir"; Source="pico_dccloco.cpp"},
    @{Name="PicoDCCLocos"; Path="lib\PicoDCCLoco\CMakeFiles\PicoDCCLoco.dir"; Source="pico_dcclocos.cpp"},
    @{Name="PicoDCCTrack"; Path="lib\PicoDCCTrack\CMakeFiles\PicoDCCTrack.dir"; Source="pico_dcctrack.cpp"},
    @{Name="PicoDiagnostic"; Path="lib\PicoDiagnostic\CMakeFiles\PicoDiagnostic.dir"; Source="pico_diagnostic.cpp"}
)

$LcovOutput = @()
$LcovOutput += "TN:"  # Test name (empty)

foreach ($Component in $Components) {
    $ComponentPath = Join-Path $BuildPath $Component.Path
    
    if (-not (Test-Path $ComponentPath)) {
        continue
    }
    
    Push-Location $ComponentPath
    
    $GcnoFile = "$($Component.Source).gcno"
    $GcdaFile = "$($Component.Source).gcda"
    
    if (-not (Test-Path $GcnoFile) -or -not (Test-Path $GcdaFile)) {
        Pop-Location
        continue
    }
    
    # Run gcov
    $null = gcov $GcnoFile 2>&1
    
    $GcovFile = "$($Component.Source).gcov"
    if (Test-Path $GcovFile) {
        Write-Host "[Processing] $($Component.Name)" -ForegroundColor Green
        
        # Parse the .gcov file
        $GcovContent = Get-Content $GcovFile
        
        # Extract source file path from header
        $SourceLine = $GcovContent | Where-Object { $_ -match "Source:(.+)$" } | Select-Object -First 1
        if ($SourceLine -match "Source:(.+)$") {
            $SourcePath = $Matches[1].Trim()
            
            # Normalize path to forward slashes for lcov format
            $SourcePath = $SourcePath -replace "\\", "/"
            
            # Convert to relative path from workspace
            $WorkspaceNormalized = $WorkspaceRoot -replace "\\", "/"
            $RelativePath = $SourcePath -replace [regex]::Escape($WorkspaceNormalized), ""
            $RelativePath = $RelativePath.TrimStart("/")
            
            # Start SF (source file) entry
            $LcovOutput += "SF:$RelativePath"
            
            # Parse line execution counts
            foreach ($Line in $GcovContent) {
                # Match lines like "        5:   42:    code here"
                # Where 5 = execution count, 42 = line number
                if ($Line -match "^\s*(\d+|\#\#\#\#\#):\s*(\d+):") {
                    $ExecutionCount = $Matches[1]
                    $LineNumber = $Matches[2]
                    
                    # Convert ##### (not executed) to 0
                    if ($ExecutionCount -eq "#####") {
                        $ExecutionCount = "0"
                    }
                    
                    $LcovOutput += "DA:$LineNumber,$ExecutionCount"
                }
            }
            
            $LcovOutput += "end_of_record"
        }
    }
    
    Pop-Location
}

# Write lcov.info to workspace root
$LcovPath = Join-Path $WorkspaceRoot "lcov.info"
$LcovOutput | Out-File -FilePath $LcovPath -Encoding ASCII

Write-Host "`n[SUCCESS] Generated: $LcovPath" -ForegroundColor Green
Write-Host "`nTo view coverage in VS Code:" -ForegroundColor Cyan
Write-Host "  1. Open a source file" -ForegroundColor White
Write-Host "  2. Press Ctrl+Shift+7 (or click 'Watch' in status bar)" -ForegroundColor White
Write-Host "  3. Coverage will be displayed in the gutter" -ForegroundColor White
Write-Host ""
