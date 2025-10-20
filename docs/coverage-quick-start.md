# VS Code Coverage Quick Start

## ✅ Current Status
- Coverage Gutters extension: **INSTALLED**
- Coverage data: **GENERATED** (28 .gcda files, 7 .gcov files)
- Configuration: **COMPLETE**

## 🎯 How to View Coverage RIGHT NOW

### Option 1: Coverage Gutters (Visual - RECOMMENDED)

1. **You already have the file open**: `lib/PicoDCCController/pico_dcccontroller.cpp`

2. **Enable Coverage Display**:
   - Press `Ctrl+Shift+7` (or click "Watch" in status bar)
   
3. **What You'll See**:
   - **Green gutter**: Line covered by tests ✅
   - **Red gutter**: Line NOT covered by tests ❌
   - **No gutter**: Not executable (comment/whitespace)
   - **Status bar**: Overall coverage percentage

4. **Coverage Files Available**:
   ```
   ✅ lib\PicoDCCController\pico_dcccontroller.cpp.gcov
   ✅ lib\PicoConfigStorage\pico_config_storage.cpp.gcov
   ✅ lib\PicoDCCEX\pico_dccex.cpp.gcov
   ✅ lib\PicoDCCLoco\pico_dccloco.cpp.gcov
   ✅ lib\PicoDCCLoco\pico_dcclocos.cpp.gcov
   ✅ lib\PicoDCCTrack\pico_dcctrack.cpp.gcov
   ✅ lib\PicoDiagnostic\pico_diagnostic.cpp.gcov
   ```

5. **Keyboard Shortcuts**:
   - `Ctrl+Shift+7` - Watch/enable coverage
   - `Ctrl+Shift+8` - Display coverage
   - `Ctrl+Shift+9` - Remove watch/disable

### Option 2: Text-Based Coverage Report

```powershell
.\scripts\Generate-Coverage-Report.ps1
```

Shows:
```
PicoConfigStorage    [##############------------------------------------]  27.8%
PicoDCCLocos         [############################----------------------]  57.0%
PicoDCCController    [###################################---------------]  70.3%
...
Overall Coverage: 64.96%
```

## 📋 Running Tests with Coverage

### Quick Workflow (Test Explorer)

1. **Open Test Explorer**: Click beaker icon (or `Ctrl+Shift+T`)
2. **Run tests**: Click ▶ button next to any suite
3. **View coverage**: Press `Ctrl+Shift+7` in source file

### Full Workflow (Tasks)

```
Ctrl+Shift+P → Tasks: Run Task → Test with Coverage (Full)
```

This automatically:
- Builds tests with coverage flags
- Runs all test suites
- Generates coverage report

## ⚠️ About "No coverage info files" Message

**If you see this in CMake Test Explorer**: This is **EXPECTED** and **NOT AN ERROR**.

- CMake Test Explorer's "Run with Coverage" button requires `lcov` tool
- `lcov` is not available on Windows by default
- You don't need it - Coverage Gutters works better!

**What to do**:
- Ignore the message
- Use Coverage Gutters instead (press `Ctrl+Shift+7`)
- Or run: `.\scripts\Convert-Coverage-For-VSCode.ps1`

## 🎨 Coverage Targets

| Component | Current | Target | Status |
|-----------|---------|--------|--------|
| PicoDCCEX | 89% | 70%+ | ✅ Excellent |
| PicoDCCTrack | 82% | 70%+ | ✅ Excellent |
| PicoDCCController | 70% | 70%+ | ✅ Met |
| PicoDCCLoco | 70% | 70%+ | ✅ Met |
| PicoDCCLocos | 57% | 70%+ | ⚠️ Needs work |
| PicoConfigStorage | 28% | 60%+ | ❌ Needs tests |
| **Overall** | **65%** | **65%+** | ✅ Met |

## 🔄 Regenerate Coverage After Code Changes

```powershell
cd build
cmake --build .
ctest --output-on-failure
.\scripts\Convert-Coverage-For-VSCode.ps1
```

This will:
1. Build tests with coverage
2. Run all test suites
3. Generate `.gcov` files in source directories
4. Generate `lcov.info` in workspace root (used by Coverage Gutters)

Then reload in VS Code: `Ctrl+Shift+9` then `Ctrl+Shift+7`

---

**TL;DR**: Press `Ctrl+Shift+7` in `pico_dcccontroller.cpp` RIGHT NOW to see coverage!
