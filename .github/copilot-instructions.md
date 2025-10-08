# AI Coding Agent Instructions for PicoDCC

Welcome to the PicoDCC codebase! This document provides essential guidelines for AI coding agents to be productive and aligned with the project's architecture, workflows, and conventions.

## Project Overview
PicoDCC is a project designed for managing and controlling Digital Command Control (DCC) systems, commonly used in model railroads. The codebase is structured to support modularity and scalability, with distinct components for different functionalities:

- **Core Components**:
  - `PicoDCCController`: Handles the main control logic, implements DCC-EX protocol parsing, manages dual-core architecture with Core 0/Core 1 coordination, and maintains main command queue.
  - `PicoDCCEX`: Manages extended DCC functionalities and packet parsing for DCC-EX protocol.
  - `PicoDCCLoco`: Focuses on locomotive-specific operations including throttle commands, function control, and CV programming support.
  - `PicoDCCTrack`: Deals with track-related functionalities, packet transmission via PIO, and hardware queue management (single-buffered design).
  - `PicoDCCLocos`: Collection management for multiple locomotives with semaphore-protected operations.
- **Testing**:
  - Unit tests are located in the `test/` directory, with comprehensive test coverage including `pico_dcc_controller_tests.cpp`, `pico_dcc_loco_tests.cpp`, `pico_dcc_locos_tests.cpp`, and `pico_dcc_packet_tests.cpp`.
- **Circuit Design**:
  - KiCad files for PCB design are in the `CircuitDesign/` directory.

## Developer Workflows

### Build Modes
The project supports two distinct build modes controlled by the `TEST_BUILD` flag in `CMakeLists.txt`:

1. **Test Mode** (`TEST_BUILD=ON`):
   - Uses MSVC compiler for Windows testing
   - Includes mock implementations for hardware functions
   - Links with CMocka testing framework
   - Uses `uart_puts(uart0, ...)` for UART output (mocked)
   - Builds test executables in `build/test/`

2. **Hardware Mode** (`TEST_BUILD=OFF`):
   - Uses ARM GCC compiler (arm-none-eabi) for Raspberry Pi Pico
   - Links with Pico SDK for hardware abstraction
   - Uses `printf(...)` for UART output via Pico SDK
   - Builds firmware files (.elf, .uf2, etc.) in `build/src/`
   - Requires `PICO_SDK_PATH` environment variable

### Building the Project
- The project uses CMake for build configuration. Build artifacts are located in the `build/` directory.
- To build the project, run the following commands in the terminal:
  ```bash
  mkdir -p build
  cd build
  cmake ..
  cmake --build .
  ```
- **Important**: Use `cmake --build .` instead of `make` for better cross-platform compatibility and proper target building.
- **PowerShell Limitation**: When using PowerShell on Windows, avoid using `&&` to chain commands as it's not supported. Use separate commands or `;` for single-line command chaining.

### Running Tests (TEST_BUILD=ON)
- Tests are compiled into executables like `pico_dcc_packet_tests.exe` in the `build/test/` directory.
- Run tests directly from the build directory:
  ```bash
  cd build
  ./test/pico_dcc_packet_tests.exe
  ./test/pico_dcc_controller_tests.exe
  ./test/pico_dcc_loco_tests.exe
  ./test/pico_dcc_locos_tests.exe
  ./test/pico_dcc_track_tests.exe
  ```
- **Test Architecture**: All tests use CMocka and include mock implementations for hardware-specific functions.

### Dual-Mode Validation Script
The project includes a comprehensive PowerShell script (`scripts/Validate-DualMode.ps1`) that automatically validates both build modes to ensure cross-mode compatibility.

#### Usage:
```powershell
# Full validation (includes test execution)
.\scripts\Validate-DualMode.ps1

# Skip test execution (build validation only)
.\scripts\Validate-DualMode.ps1 -SkipTests
```

#### What the Script Does:
1. **Test Mode Validation** (`TEST_BUILD=ON`):
   - Clears CMake cache and reconfigures for MSVC/Windows testing
   - Builds all test executables and libraries
   - Executes all available test suites (currently 43/57 tests passing)
   - Reports individual test suite results

2. **Hardware Mode Validation** (`TEST_BUILD=OFF`):
   - Clears CMake cache and reconfigures for ARM GCC/Pico hardware
   - Attempts to build firmware files (.elf, .uf2, etc.)
   - Validates ARM GCC toolchain setup
   - Checks for proper Pico SDK configuration

3. **Comprehensive Reporting**:
   - Build success/failure status for both modes
   - Test execution results (when not skipped)
   - Hardware build validation results
   - Overall compatibility assessment

#### Expected Output:
```
=== PicoDCC Dual-Mode Build Validation ===
Project Root: E:\Development\PicoDCC
Build Directory: E:\Development\PicoDCC\build

--- Switching to TEST Mode ---
Cleared CMake cache
Configuring CMake for TEST mode...
Building in TEST mode...
[OK] Found pico_dcc_controller_tests.exe (245760 bytes, modified 10/8/2025 2:30:45 PM)
[OK] Found pico_dcc_dccex_tests.exe (192512 bytes, modified 10/8/2025 2:30:44 PM)
...

--- Running Test Suites ---
Running pico_dcc_controller_tests.exe...
[==========] Running 6 test(s).
[  PASSED  ] 6 test(s).
...

--- Switching to HARDWARE Mode ---
...

=== Validation Summary ===
Test Mode Build: [PASSED]
Test Suite: [PASSED]
Hardware Mode: [PASSED]

Overall Result: [ALL TESTS PASSED]
```

#### Troubleshooting:
- **Packet Tests Failure**: Known issue with `inline` keyword macro in MSVC compiler
- **Hardware Mode Issues**: Requires proper ARM GCC toolchain and `PICO_SDK_PATH` environment variable
- **Build Failures**: Script automatically clears CMake cache between mode switches to prevent configuration conflicts
- **PowerShell Compatibility**: Script handles PowerShell-specific syntax limitations (avoids `&&` operators)

#### When to Use:
- Before committing major changes to ensure cross-mode compatibility
- After modifying shared headers or core components
- When debugging build issues in either mode
- As part of CI/CD validation workflow

This script directly addresses the need to ensure that changes in one build mode don't break the other, providing automated verification of the dual-build architecture.

### Hardware Build (TEST_BUILD=OFF)  
- Builds firmware for Raspberry Pi Pico using ARM GCC toolchain
- Generates `.elf`, `.uf2`, `.hex`, and `.bin` files in `build/src/`
- Requires Pico SDK installation and `PICO_SDK_PATH` environment variable
- Uses conditional compilation (`#ifdef TEST_BUILD`) to switch between mock and hardware functions

### Debugging
- Debug symbols are included in the build by default (`-g` flag in GCC).
- Use tools like `gdb` for debugging:
  ```bash
  gdb ./build/PicoDCC.elf
  ```

## Project-Specific Conventions
- **File Organization**:
  - Source files are in `src/`.
  - Libraries are in `lib/`.
  - Tests are in `test/`.
- **Naming Conventions**:
  - Follow `snake_case` for file and function names.
  - Use `CamelCase` for class names.
- **Testing Framework**:
  - The project uses `cmocka` for unit testing.

## Test Investigation Best Practices
- **Always compile and run the tests when investigating test issues.**
  - After making changes to test code or related logic, rebuild the project and execute the relevant test suite to observe output and debug failures.
  - This ensures that any code or test changes are validated in the actual build and runtime environment.
- **Understanding Hardware Queue Architecture**:
  - The hardware queue (`PicoDCCTrack`) is single-buffered by design, meaning only one command is visible at a time.
  - When debugging queue issues, check the "sent" packets rather than the current queue state.
  - Use debug output to trace packet flow between Core 0 (main queue) and Core 1 (hardware queue).
- **Test Coverage Status**:
  - **Complete coverage**: PicoDCCEX (3 tests), PicoDccExPacket (14 tests), PicoDccLoco (11 tests), PicoDccLocos (7 tests), PicoDccController (6 tests), PicoDccTrack (16 tests)
  - **Total test count**: 57 tests across all components
  - **Mock infrastructure**: Comprehensive hardware abstraction for GPIO, ADC, PIO, UART, and timing functions

## DCC Protocol Implementation
- **Emergency Stop Handling**:
  - Emergency stop is implemented as a DCC broadcast command (address 0x00, instruction 0x41).
  - The system uses a single broadcast packet rather than per-locomotive emergency stop commands.
  - Emergency stop clears the main queue, hardware queue, and all locomotive states.
- **CV Programming Support**:
  - The `PicoDccLoco` class includes CV (Configuration Variable) methods for future decoder programming.
  - Methods include `verifyCV()`, `readCVByte()`, `readCVBit()`, `writeCVBytes()`, and `writeCVBit()`.
  - These are preserved for planned programming track functionality.
- **Queue Management**:
  - Main command queue operates on Core 0, hardware queue on Core 1.
  - Hardware queue is single-buffered - only one command visible at any time.
  - Semaphore protection is used for multi-core synchronization.

## Integration Points
- **External Dependencies**:
  - **Test Mode**: CMocka testing framework, MSVC compiler
  - **Hardware Mode**: Pico SDK (requires `PICO_SDK_PATH`), ARM GCC toolchain
- **Cross-Component Communication**:
  - Components interact through well-defined interfaces using shared `dcc_types.h`
  - `PicoDCCController` communicates with `PicoDCCTrack` for track updates
  - UART communication uses conditional compilation: `uart_puts()` for tests, `printf()` for hardware
- **Multi-Core Architecture** (Hardware Mode):
  - Core 0 handles command processing and main queue management
  - Core 1 manages hardware-level packet transmission via PIO
  - Proper synchronization is critical when modifying queue operations
- **Conditional Compilation**:
  - `#ifdef TEST_BUILD` switches between mock and hardware implementations
  - Shared `raw_dcc_cmd_t` type defined in `lib/dcc_types.h`

## Code Maintenance Best Practices
- **Unused Code Management**:
  - Regularly review and remove truly unused methods, variables, and includes.
  - Preserve methods that have clear future purpose (e.g., CV programming methods).
  - When refactoring, consider the broader architectural implications across all components.
- **Test-Driven Changes**:
  - Always update tests when changing component behavior.
  - Use mock implementations to test hardware-dependent functionality.
  - Verify that tests pass after architectural changes.

## Examples
- **Adding a New Test**:
  1. Create a new file in `test/`, e.g., `new_feature_tests.cpp`.
  2. Use the `cmocka` framework to define test cases.
  3. Add the test executable to `test/CMakeLists.txt`.

- **Modifying a Component**:
  1. Locate the relevant component in `lib/`.
  2. Follow the existing patterns for class and function definitions.
  3. Update the corresponding tests in `test/`.

- **Emergency Stop Implementation Example**:
  - Emergency stop was refactored from per-locomotive commands to a single DCC broadcast.
  - The change involved updating `PicoDCCController`, removing methods from `PicoDCCLoco` and `PicoDCCLocos`.
  - Tests were updated to verify the broadcast packet and queue clearing behavior.

- **Current Monitoring Improvement Example**:
  - Current monitoring was improved to only perform overcurrent protection when ADC is actually configured.
  - The change involved wrapping current monitoring logic in `canReadCurrent()` check in `PicoDCCTrack::loop()`.
  - Tests were added to verify that tracks without ADC configuration skip current monitoring entirely.

- **DCC-EX Acknowledgment Restoration**:
  - DCC-EX protocol acknowledgments were restored for all command types during refactoring.
  - Power commands send `<p0>` or `<p1>` responses, throttle/function commands send `<l cab 0 speed 0>` status.
  - Emergency stop and accessory commands send `<O>` acknowledgments.
  - UART output tracking was added to test infrastructure for acknowledgment validation.

## Key Files and Directories
- `CMakeLists.txt`: Build configuration.
- `src/`: Main source code.
- `lib/`: Component libraries.
- `test/`: Unit tests.
- `CircuitDesign/`: PCB design files.

For any questions or clarifications, refer to the existing code patterns or consult the project maintainers.