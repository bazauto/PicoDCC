# Coverage Scripts Overview

This document explains the coverage-related scripts and their purposes.

## 📜 Active Scripts

### 1. `Convert-Coverage-For-VSCode.ps1` (Main Script)
**Purpose**: Master script that generates all coverage data for VS Code

**What it does**:
1. Runs `gcov` on all compiled test files
2. Copies `.gcov` files to source directories (for reference)
3. Calls `Generate-Lcov-Info.ps1` to create `lcov.info`

**When to use**:
- After running tests: `.\scripts\Convert-Coverage-For-VSCode.ps1`
- Automatically called by VS Code tasks

**Output**:
- `lib/*/[component].cpp.gcov` - Individual coverage files (reference)
- `lcov.info` - Master coverage file (used by Coverage Gutters)

---

### 2. `Generate-Lcov-Info.ps1` (Helper Script)
**Purpose**: Converts `.gcov` files to `lcov.info` format

**What it does**:
1. Parses raw `.gcov` files from build directories
2. Generates `lcov.info` in LCOV format with forward-slash paths
3. Places `lcov.info` in workspace root

**When to use**:
- Called automatically by `Convert-Coverage-For-VSCode.ps1`
- Can be run standalone: `.\scripts\Generate-Lcov-Info.ps1`

**Output**:
- `lcov.info` - LCOV-format coverage file

**Why this exists**:
- Coverage Gutters extension requires LCOV format
- Raw `.gcov` files have path matching issues on Windows
- LCOV format is more portable and compatible

---

### 3. `Generate-Coverage-Report.ps1` (Reporting)
**Purpose**: Generates human-readable text coverage report

**What it does**:
1. Parses `.gcov` files from build directories
2. Calculates coverage percentages per component
3. Displays ASCII bar charts and statistics

**When to use**:
- Quick terminal coverage check: `.\scripts\Generate-Coverage-Report.ps1`
- CI/CD pipelines for coverage reporting
- When you want overall statistics without opening VS Code

**Output** (example):
```
=== Coverage Summary ===

PicoConfigStorage    [##############------------------------------------]  27.8%
PicoDCCLocos         [############################----------------------]  57.0%
PicoDCCController    [###################################---------------]  70.3%
...

Overall Coverage: 64.96%
```

---

### 4. `Validate-DualMode.ps1` (Build Validation)
**Purpose**: Validates both TEST and HARDWARE build modes

**What it does**:
1. Builds and tests in TEST_BUILD mode (Windows/GCC)
2. Attempts to build in hardware mode (ARM GCC/Pico)
3. Reports compatibility issues

**When to use**:
- Before committing major changes
- When modifying shared headers or build configuration
- As part of CI/CD validation

**Output**:
- Build success/failure for both modes
- Test execution results
- Compatibility assessment

---

## 🎯 Typical Workflow

### For Development (VS Code)
```powershell
# 1. Write/modify code
# 2. Run tests with coverage
cd build
cmake --build .
ctest --output-on-failure

# 3. Generate coverage for VS Code
.\scripts\Convert-Coverage-For-VSCode.ps1

# 4. View in VS Code
# Open source file, press Ctrl+Shift+7
```

### For Quick Coverage Check
```powershell
.\scripts\Generate-Coverage-Report.ps1
```

### For CI/CD or Pre-Commit
```powershell
.\scripts\Validate-DualMode.ps1
.\scripts\Generate-Coverage-Report.ps1
```

---

## 📁 Coverage Files Generated

### In Workspace Root
- `lcov.info` - Master coverage file (used by Coverage Gutters)
  - **Committed**: No (in `.gitignore`)
  - **Used by**: Coverage Gutters extension

### In Source Directories (`lib/*/`)
- `*.gcov` - Individual component coverage files
  - **Committed**: No (in `.gitignore`)
  - **Used by**: Reference/debugging

### In Build Directory (`build/`)
- `*.gcda` - Coverage data (execution counts)
- `*.gcno` - Coverage notes (instrumentation info)
  - **Committed**: No (entire `build/` in `.gitignore`)
  - **Used by**: `gcov` to generate `.gcov` files

---

## 🔧 VS Code Integration

### Settings (`.vscode/settings.json`)
```json
"coverage-gutters.coverageFileNames": ["lcov.info"]
```
- Coverage Gutters looks for `lcov.info` in workspace root

### Tasks (`.vscode/tasks.json`)
- **"Build Tests with Coverage"** - Builds with `--coverage` flags
- **"Run All Tests"** - Executes `ctest`
- **"Generate Coverage Report"** - Runs text report script
- **"Test with Coverage (Full)"** - All three in sequence

### Keyboard Shortcuts
- `Ctrl+Shift+7` - Watch coverage (enable display)
- `Ctrl+Shift+8` - Display coverage (show/update)
- `Ctrl+Shift+9` - Remove watch (disable display)

---

## 🗑️ Previously Removed Scripts

### `Generate-LCov-Coverage.ps1` (Deleted)
**Why removed**: This script tried to use the `lcov` command-line tool (native Linux tool, not available on Windows). It was redundant because:
- `lcov` tool is not available on Windows
- Script just fell back to calling `Convert-Coverage-For-VSCode.ps1`
- `Generate-Lcov-Info.ps1` provides the same functionality using pure PowerShell

---

## 📊 Coverage Targets

| Component | Target | Current |
|-----------|--------|---------|
| Overall | 65%+ | 64.96% |
| PicoDCCEX | 70%+ | 89% ✅ |
| PicoDCCTrack | 70%+ | 82% ✅ |
| PicoDCCController | 70%+ | 70% ✅ |
| PicoDCCLoco | 70%+ | 70% ✅ |
| PicoDCCLocos | 70%+ | 57% ⚠️ |
| PicoConfigStorage | 60%+ | 28% ❌ |

---

*Last updated: October 20, 2025*
