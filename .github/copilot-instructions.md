# AI Coding Agent Instructions for PicoDCC

Welcome to the PicoDCC codebase! This document provides essential guidelines for AI coding agents to be productive and aligned with the project's architecture, workflows, and conventions.

## Project Overview
PicoDCC is a project designed for managing and controlling Digital Command Control (DCC) systems, commonly used in model railroads. The codebase is structured to support modularity and scalability, with distinct components for different functionalities:

- **Core Components**:
  - `PicoDCCController`: Handles the main control logic, implements DCC-EX protocol parsing, manages dual-core architecture with Core 0/Core 1 coordination, and maintains main command queue for explicit commands.
  - `PicoDCCEX`: Manages extended DCC functionalities and packet parsing for DCC-EX protocol.
  - `PicoDCCLoco`: Focuses on locomotive-specific operations including throttle commands, function control, and CV programming support.
  - `PicoDCCTrack`: Deals with track-related functionalities, packet transmission via PIO, hardware queue management (single-buffered design), and **locomotive reminder generation on Core 1**.
  - `PicoDCCLocos`: Collection management for multiple locomotives with semaphore-protected operations for thread-safe access from both cores.
  - `PicoDCCDisplay`: LCD display management (Waveshare WAV-27579, ST7789T3 controller, LVGL graphics). Self-contained component handles all display logic including boot sequence, periodic updates, and data gathering. **See LCD Code Organization section below.**
  - `PicoConfigStorage`: Non-volatile configuration storage in flash memory for calibration values and tunable parameters (last 4KB sector).
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
   - Executes all available test suites (currently 59/64 tests passing, 5 pre-existing failures)
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
  - **CRITICAL**: Conditional compilation should ONLY be used for hardware abstraction (mocks vs. real hardware)
  - **NEVER** use `#ifdef TEST_BUILD` in business logic, error handling, or diagnostic messages

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

## Main Application Entry Point (`src/pico_dcc.cpp`)
- **CRITICAL RULE**: Keep `main()` function minimal and clean.
  - `main()` should ONLY contain high-level initialization and the main loop.
  - **NEVER** add complex logic, data gathering, or update loops directly in `main()`.
  - **ALWAYS** delegate functionality to component classes (e.g., `PicoDCCDisplay::loop()`).
  
- **Current Structure** (DO NOT violate):
  ```cpp
  int main() {
      stdio_init_all();
      
      // Initialize components (keep to 5-10 lines max)
      #ifndef TEST_BUILD
      PicoDCCDisplay display;
      display.init();
      display.runBootSequence();
      #endif
      
      // Start multi-core
      multicore_launch_core1(main_core1);
      
      // Main loop (keep to 3-5 lines max)
      while (true) {
          pico_controller.dccexLoop();
          #ifndef TEST_BUILD
          display.loop(&pico_controller);  // Component handles its own logic
          #endif
      }
  }
  ```

- **Pattern to Follow**:
  - Component initialization: `component.init()`
  - Component boot/setup: `component.runBootSequence()`
  - Component periodic updates: `component.loop(controller)` (NOT in main!)
  - Each component manages its own timing, data gathering, and update logic internally

- **What NOT to Do**:
  - ❌ Adding timer variables in `main()` (e.g., `last_update_time`)
  - ❌ Gathering data from other components in `main()` (e.g., `getTrack()`, `getCurrent()`)
  - ❌ Complex conditional logic or calculations in `main()`
  - ❌ Long blocks of update code (>5 lines per component)

## LCD Code Organization
- **PicoDCCDisplay** is a self-contained component (lib/PicoDCCDisplay/)
  - Handles ALL display logic internally: boot sequence, updates, data gathering
  - Main application ONLY calls: `init()`, `runBootSequence()`, `loop(controller)`
  - Component manages: timing, track data queries, LVGL updates, screen rendering
- **Integration Pattern**: See main() structure above - 3 simple calls, no embedded logic
- **Documentation**: `docs/lcd-*.md` for LCD-specific design and implementation details

## Diagnostic Logging System
- **CRITICAL RULE**: Never pollute the DCC-EX command UART with diagnostic messages. 
  - **NEVER** use `DCCEX_RESPONSE()` for error messages, warnings, or debug information.
  - **ALWAYS** use the diagnostic logging system (`LOG_*` macros) for all internal diagnostics.
  - **ONLY** use `DCCEX_RESPONSE()` for genuine DCC-EX protocol responses to client commands.

- **Usage** (`lib/pico_diagnostic.h`):
  - `LOG_CRITICAL()`: System failures, safety violations  
  - `LOG_ERROR()`: Component errors, failed operations
  - `LOG_WARNING()`: Non-critical issues  
  - `LOG_INFO()`: Status information
  - Include `#include "pico_diagnostic.h"` in components that need logging
  - Current implementation is silent to avoid UART pollution, ready for future LCD integration

- **Protocol Compliance**:
  - No conditional compilation (`#ifdef TEST_BUILD`) in diagnostic messages
  - Clean separation between protocol communication and internal diagnostics

## Test Investigation Best Practices
- **Always compile and run the tests when investigating test issues.**
  - After making changes to test code or related logic, rebuild the project and execute the relevant test suite to observe output and debug failures.
  - This ensures that any code or test changes are validated in the actual build and runtime environment.
- **Dual-Mode Validation**:
  - Use `.\scripts\Validate-DualMode.ps1` to validate changes work in both TEST and HARDWARE modes
  - Critical when modifying shared headers, core components, or build configuration
  - Prevents mode-specific compilation errors and ensures architectural consistency
- **Understanding Hardware Queue Architecture**:
  - The hardware queue (`PicoDCCTrack`) is single-buffered by design, meaning only one command is visible at a time.
  - When debugging queue issues, check the "sent" packets rather than the current queue state.
  - Use debug output to trace packet flow between Core 0 (main queue) and Core 1 (hardware queue).
- **Test Coverage Status**:
  - **64 total tests**: Controller (9), DCCEX (3), Locos (11), Loco (11), Packet (14), Track (16)
  - **Mock infrastructure**: Comprehensive hardware abstraction for GPIO, ADC, PIO, UART, and timing functions
  - **Thread-Safety Testing**: Multi-core race condition validation and locomotive collection management

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
  - Main command queue operates on Core 0 for explicit commands with repeat logic.
  - Hardware queue operates on Core 1 with single-buffered design.
  - **Reminder generation moved to Core 1**: `PicoDccTrack::loop()` generates locomotive reminders when hardware queue is empty.
  - Priority system: Explicit commands > locomotive reminders > idle packets.
  - Semaphore protection is used for multi-core synchronization of locomotive collection.
  - **Self-regulating design**: Core 1 only generates reminders when hardware has capacity, preventing queue overflow.

## Integration Points
- **External Dependencies**:
  - **Test Mode**: CMocka testing framework, MSVC compiler
  - **Hardware Mode**: Pico SDK (requires `PICO_SDK_PATH`), ARM GCC toolchain
- **Cross-Component Communication**:
  - Components interact through well-defined interfaces using shared `dcc_types.h`
  - `PicoDCCController` communicates with `PicoDCCTrack` for track updates
  - UART communication uses conditional compilation: `uart_puts()` for tests, `printf()` for hardware
- **Multi-Core Architecture** (Hardware Mode):
  - Core 0 handles command processing and main queue management for explicit commands
  - Core 1 manages hardware-level packet transmission via PIO and generates locomotive reminders
  - Proper synchronization is critical when modifying queue operations or locomotive collection
  - `PicoDccLocos` collection is updated by Core 0, read by Core 1 for reminder generation
- **Thread-Safety Patterns**:
  - **CRITICAL**: All shared data structure access MUST be protected by semaphores
  - **Vector Operations**: Always acquire semaphore BEFORE any `std::vector` operation (size(), empty(), etc.)
  - **Race Condition Prevention**: Never check container state outside of semaphore protection
  - **Iterator Safety**: Use proper iterator increment patterns to avoid infinite loops during removal
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
- **Documentation Maintenance**:
  - **ALWAYS** update relevant documentation when making code changes or architectural modifications.
  - **Architecture Document** (`docs/architecture.md`): Update when changing component responsibilities, adding new systems, or modifying core architecture.
  - **Instructions File** (`.github/copilot-instructions.md`): Update when establishing new patterns, adding development guidelines, or changing build/test workflows.
  - **Test Counts**: Update architecture document test counts when adding or removing test cases.
  - **Future vs. Current Features**: Move implemented features from "Future Enhancements" to current architecture sections.
  - **Examples Section**: Add new examples when implementing significant architectural changes or establishing new patterns.

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

- **Locomotive Reminder Refactoring (Queue Overflow Prevention)**:
  - **Problem**: Core 0 generated reminders faster than Core 1 could transmit, causing queue overflow.
  - **Solution**: Moved reminder generation from Core 0 to Core 1 (`PicoDccTrack::loop()`).
  - **Architecture Change**: Core 1 now generates reminders only when hardware queue is empty (self-regulating).
  - **Priority System**: Explicit commands > locomotive reminders > idle packets.
  - **Thread Safety**: `PicoDccLocos` collection is updated by Core 0, read by Core 1 with semaphore protection.
  - **Benefits**: Eliminates queue overflow, simpler flow, hardware-paced reminder generation.
  - **Implementation**: `PicoDccTrack` constructor takes optional `PicoDccLocos*` parameter (main track only).

## Key Files and Directories
- `CMakeLists.txt`: Build configuration.
- `src/`: Main source code.
- `lib/`: Component libraries.
- `test/`: Unit tests.
- `CircuitDesign/`: PCB design files.

For any questions or clarifications, refer to the existing code patterns or consult the project maintainers.