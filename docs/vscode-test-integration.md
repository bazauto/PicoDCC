# VS Code Test Integration with Coverage

This guide explains how to use the integrated testing and code coverage features in VS Code.

## Prerequisites

### Required VS Code Extensions

The following extensions are recommended (listed in `.vscode/extensions.json`):

1. **CMake Tools** (`ms-vscode.cmake-tools`) - CMake project integration
2. **C++ TestMate** (`matepek.vscode-catch2-test-adapter`) - Test Explorer integration
3. **Coverage Gutters** (`ryanluker.vscode-coverage-gutters`) - Visual coverage display

To install these extensions:
1. Open VS Code
2. Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on macOS)
3. Type "Extensions: Show Recommended Extensions"
4. Click "Install Workspace Recommended Extensions"

## Using the Test Explorer

### Running Tests via Test Explorer

1. **Open Test Explorer**:
   - Click the test beaker icon in the Activity Bar (left side)
   - Or press `Ctrl+Shift+T`

2. **View Available Tests**:
   - Tests are organized by suite:
     - `pico_dcc_controller_tests` (13 tests)
     - `pico_dcc_track_tests` (21 tests)
     - `pico_dcc_loco_tests` (11 tests)
     - `pico_dcc_locos_tests` (11 tests)
     - `pico_dcc_packet_tests` (25 tests)
     - `pico_config_storage_tests` (11 tests)
     - `pico_dcc_display_tests` (9 tests)
     - `pico_diagnostic_tests` (9 tests)
     - `pico_dcc_dccex_tests` (3 tests)
   - **Total: 113 tests** across 9 test suites

3. **Run Tests**:
   - Click the play button (▶) next to any test suite or individual test
   - Or right-click and select "Run Test"
   - Tests will be built automatically if needed

4. **View Results**:
   - Green checkmark (✓) = Test passed
   - Red X (✗) = Test failed
   - Click on a test to see detailed output

### Running Tests via Tasks

Alternative to Test Explorer, you can use VS Code tasks:

1. **Build Tests with Coverage**:
   ```
   Ctrl+Shift+P → Tasks: Run Task → Build Tests with Coverage
   ```

2. **Run All Tests**:
   ```
   Ctrl+Shift+P → Tasks: Run Task → Run All Tests
   ```

3. **Generate Coverage Report**:
   ```
   Ctrl+Shift+P → Tasks: Run Task → Generate Coverage Report
   ```

4. **Test with Coverage (Full)**:
   ```
   Ctrl+Shift+P → Tasks: Run Task → Test with Coverage (Full)
   ```
   This runs all three tasks in sequence.

## Viewing Code Coverage

### Overview: Two Coverage Methods

The PicoDCC project supports two ways to view code coverage:

1. **Coverage Gutters (Recommended)** - Visual in-editor coverage with green/red gutters
   - ✅ Works on Windows without additional tools
   - ✅ Real-time visual feedback in source files
   - ✅ Already configured and working
   - Uses: `.gcov` files (generated automatically)

2. **CMake Test Explorer Coverage** - Coverage in Test Explorer UI
   - ⚠️ Requires `lcov` tool (not typically available on Windows)
   - ⚠️ Additional installation needed: `choco install lcov`
   - Uses: `lcov.info` files (requires conversion from `.gcov`)

**Note**: If you see "No coverage info files for CMake project" when running tests with coverage in Test Explorer, this is expected on Windows without `lcov`. Use Coverage Gutters instead (Method 1).

### Method 1: Coverage Gutters (In-Editor Visualization) ⭐ RECOMMENDED

**Coverage Gutters** displays coverage directly in your source files with colored highlights.

#### Setup
1. Install the "Coverage Gutters" extension
2. Build and run tests with coverage:
   ```powershell
   # Run from the project root -- presets resolve against the source directory.
   Remove-Item build/host -Recurse -Force -ErrorAction SilentlyContinue
   cmake --preset host -DCMAKE_CXX_FLAGS="--coverage"
   cmake --build --preset host
   .\build\host\test\pico_dcc_controller_tests.exe
   ```
3. Convert coverage for VS Code:
   ```bash
   .\scripts\Convert-Coverage-For-VSCode.ps1
   ```

#### Usage
1. Open a source file (e.g., `lib/PicoDCCController/pico_dcccontroller.cpp`)
2. **Watch Coverage**:
   - Press `Ctrl+Shift+7`
   - Or click "Watch" in the status bar
   - Or use Command Palette: `Coverage Gutters: Watch`

3. **View Coverage**:
   - **Green gutter**: Line is covered by tests ✅
   - **Red gutter**: Line is NOT covered by tests ❌
   - **No gutter**: Line is not executable (comments, whitespace)

4. **Coverage Summary**:
   - Status bar shows overall coverage percentage
   - Hover over lines for detailed execution counts

5. **Remove Coverage**:
   - Press `Ctrl+Shift+9`
   - Or click "Remove Watch" in the status bar

#### Keyboard Shortcuts
- `Ctrl+Shift+7` - Watch coverage
- `Ctrl+Shift+8` - Display coverage
- `Ctrl+Shift+9` - Remove watch

### Method 2: Coverage Report Script

For a comprehensive text-based coverage report:

```bash
.\scripts\Generate-Coverage-Report.ps1
```

Output example:
```
=== Coverage Summary ===

PicoConfigStorage    [##############------------------------------------]  27.8% (32/115 lines)
PicoDCCLocos         [############################----------------------]  57.0% (49/86 lines)
PicoDCCController    [###################################---------------]  70.3% (137/195 lines)
...

--- Overall Statistics ---
Total Lines Covered: 445 / 685
Overall Coverage: 64.96%
```

### Method 3: Detailed .gcov Files

Gcov generates line-by-line coverage files with execution counts:

```bash
# Generate coverage
cd build/host/lib/PicoDCCController/CMakeFiles/PicoDCCController.dir
gcov pico_dcccontroller.cpp.gcno

# View detailed coverage
cat pico_dcccontroller.cpp.gcov
```

Format:
- `#####`: Line not executed (0 times)
- `1:`: Line executed once
- `42:`: Line executed 42 times
- `-:`: Line not executable (comment/whitespace)

## Test Development Workflow

### 1. Write Tests
```cpp
// test/pico_dcc_controller_tests.cpp
static void test_new_feature(void **state) {
    PicoDccController* controller = (PicoDccController*)*state;
    
    // Arrange
    // ... setup code
    
    // Act
    // ... execute code
    
    // Assert
    assert_int_equal(expected, actual);
}
```

### 2. Register Test
```cpp
const struct CMUnitTest controller_tests[] = {
    // ... existing tests
    cmocka_unit_test(test_new_feature),  // Add here
};
```

### 3. Build and Run
```bash
# Via task (recommended)
Ctrl+Shift+P → Tasks: Run Task → Test with Coverage (Full)

# Or manually, from the project root
cmake --build --preset host
.\build\host\test\pico_dcc_controller_tests.exe
```

### 4. Check Coverage
```bash
.\scripts\Convert-Coverage-For-VSCode.ps1
```
Then open the source file and press `Ctrl+Shift+7`.

### 5. Iterate
- Lines in **red**: Not covered - add tests
- Lines in **green**: Covered - you're good!
- Aim for 70%+ coverage on critical components

## Troubleshooting

### Tests Don't Appear in Test Explorer

1. Ensure the host tree is configured (run from the project root):
   ```bash
   cmake --preset host
   ```

2. Reload CMake project:
   ```
   Ctrl+Shift+P → CMake: Configure
   ```

3. Restart Test Explorer:
   ```
   Ctrl+Shift+P → Test: Refresh Tests
   ```

### Coverage Not Showing

1. Verify coverage data exists:
   ```bash
   Get-ChildItem build -Recurse -Filter "*.gcda"
   ```

2. Regenerate coverage files:
   ```bash
   .\scripts\Convert-Coverage-For-VSCode.ps1
   ```

3. Reload coverage:
   - Press `Ctrl+Shift+9` (remove watch)
   - Press `Ctrl+Shift+7` (watch again)

### CMake Test Explorer Shows "No coverage info files"

This message appears when running "Run with Coverage" in Test Explorer because:
- CMake Test Explorer requires `lcov.info` files
- `lcov` tool is not typically available on Windows
- This is **expected behavior** and **not an error**

**Solution**: Use Coverage Gutters instead (recommended):
1. Run tests normally (without coverage button)
2. Use `.\scripts\Convert-Coverage-For-VSCode.ps1` to generate `.gcov` files
3. Open source file and press `Ctrl+Shift+7` to view coverage

**Optional**: Install `lcov` for CMake Test Explorer coverage:
```bash
# Using Chocolatey (requires admin)
choco install lcov

# Then run:
.\scripts\Generate-LCov-Coverage.ps1
```
However, Coverage Gutters provides better visual feedback and is already configured.

### Build Fails with Coverage Flags

Ensure you're using the GNU compiler (not MSVC):
```bash
cmake --preset host -DCMAKE_CXX_FLAGS="--coverage"
```

Check compiler:
```bash
gcc --version  # Should be GCC 4.8.3
```

## Coverage Targets

Current coverage goals:
- **Overall**: 65%+ (currently 64.96%)
- **Core Components**: 70%+
  - PicoDCCEX: ✅ 89%
  - PicoDCCTrack: ✅ 82%
  - PicoDCCController: ✅ 70%
  - PicoDCCLoco: ✅ 70%

## Additional Resources

- [CMocka Documentation](https://api.cmocka.org/)
- [gcov Documentation](https://gcc.gnu.org/onlinedocs/gcc/Gcov.html)
- [Coverage Gutters Extension](https://marketplace.visualstudio.com/items?itemName=ryanluker.vscode-coverage-gutters)
- [Test Coverage Report](../docs/test-coverage-report.md)

---

*Last updated: October 20, 2025*
