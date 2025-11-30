# PicoDCC Project Documentation Index

## Project Status Overview

**Last Updated**: October 19, 2025

**Current Status**: Hardware testing stable, planning phase for service mode programming (CV operations)

**Immediate Goal**: Implement locomotive address programming on programming track (change addresses from default 3 to custom values)

---

## Outstanding Work Summary

### 🚧 In Progress: Service Mode Programming Implementation

**Overall Timeline**: 6-7 weeks (can be spread over time)  
**Current Phase**: Planning complete, awaiting Phase 1 implementation start  
**User's Primary Goal**: Program locomotive addresses (Phase 4)

#### Phase Breakdown:

| Phase | Description | Status | Duration | Priority |
|-------|-------------|--------|----------|----------|
| **Phase 1** | ACK Detection Infrastructure | 📋 Planned | 1-2 weeks | 🔴 Critical |
| **Phase 2** | Direct Mode Packet Generation | 📋 Planned | 1 week | 🔴 Critical |
| **Phase 3** | DCC-EX Command Integration | 📋 Planned | 1 week | 🔴 Critical |
| **Phase 4** | Address Programming (USER GOAL) | 📋 Planned | 1 week | 🔴 Critical |
| **Phase 5** | Bit-Level CV Operations | 📋 Planned | 1 week | 🟡 Optional |
| **Phase 6** | Legacy Mode Support | 📋 Planned | 1 week | 🟡 Optional |

#### Prerequisites Before Implementation:
- [ ] Verify hardware current sensor circuit on PCB
- [ ] Measure ADC-to-mA calibration factor (connect 100mA load)
- [ ] Confirm known-good decoder available for testing
- [ ] Decide on configuration storage approach

### 🔧 Infrastructure Improvements

#### Non-Volatile Storage System
**Status**: ✅ **Implemented** (2025-10-19)  
**Purpose**: Persistent storage for calibration values and tunable parameters  
**Solution**: Flash memory (last 4KB sector)  
**Implementation Time**: Completed

**Key Features Implemented:**
- [x] `PicoConfigStorage` class for flash operations
- [x] Linker script consideration (reserve last 4KB sector - documented)
- [x] DCC-EX configuration commands (`<D CONFIG ...>`)
- [x] Calibration workflow commands (`<D CAL ...>`)
- [x] CRC32 validation and factory defaults
- [x] Unit tests with flash mocking (11 tests, all passing)

---

## Documentation Guide

### Core Architecture Documents

#### 📘 `docs/architecture.md`
**Purpose**: Complete system architecture documentation  
**Audience**: Developers, AI agents, future maintainers  
**Content**:
- Component descriptions (Controller, Track, Loco, DCCEX, etc.)
- Dual-core architecture (Core 0 vs Core 1 responsibilities)
- Queue management and reminder generation
- DCC protocol implementation details
- Test coverage summary (124 total tests across 9 suites)
- Future enhancements roadmap

**When to Reference**:
- Understanding component interactions
- Checking architectural decisions
- Planning new features
- Reviewing test coverage

**Last Updated**: During locomotive reminder refactoring

---

#### 📗 `.github/copilot-instructions.md`
**Purpose**: AI coding agent guidelines and project conventions  
**Audience**: GitHub Copilot, AI assistants, new developers  
**Content**:
- Project overview and component structure
- Build system (TEST vs HARDWARE modes)
- Testing framework (CMocka, mock infrastructure)
- DCC protocol patterns (emergency stop, CV programming, queue management)
- Diagnostic logging system (LOG_* macros vs DCCEX_RESPONSE)
- Code maintenance best practices
- Dual-mode validation script usage
- Real-world refactoring examples

**When to Reference**:
- Setting up development environment
- Understanding build modes (TEST_BUILD flag)
- Following project conventions
- Learning diagnostic logging patterns
- Running validation scripts

**Last Updated**: Added service mode programming reference and locomotive reminder refactoring example

---

### Planning Documents (Service Mode Programming)

#### 📙 `docs/service-mode-programming-plan.md`
**Purpose**: Comprehensive implementation plan for CV programming  
**Audience**: Developers implementing CV operations  
**Content**:
- NMRA S-9.2.3 service mode programming specification
- ACK detection requirements (60mA for 6ms ±1ms)
- Direct Mode packet formats with examples
- DCC-EX command specifications (`<W>`, `<V>`, `<R>`, `<B>`)
- 6-phase implementation roadmap
- CV definitions (CV1, CV17/18, CV29, etc.)
- Testing strategy and validation procedures
- Programming vs main track differences

**When to Reference**:
- Implementing CV read/write operations
- Understanding ACK detection specifications
- Generating Direct Mode packets
- Planning implementation phases
- Debugging CV programming issues

**Key Sections**:
- **Phase 4**: Address programming (user's primary goal)
- **ACK Detection**: Critical timing requirements
- **Packet Examples**: Byte-by-byte packet construction
- **Testing Strategy**: Bench test procedures

**Last Updated**: October 18, 2025 (added accurate ACK specs from DCC Wiki)

---

#### 📙 `docs/ack-detection-analysis.md`
**Purpose**: Technical deep-dive into ACK detection implementation  
**Audience**: Developers implementing Phase 1 (ACK detection)  
**Content**:
- Raspberry Pi Pico ADC hardware capabilities (500 kSPS max)
- Current `adc_read()` implementation analysis
- Recommended solution: Free-running ADC at 25 kHz
- Complete working code examples (`detectACK()`, `analyzeACKSamples()`)
- Performance analysis (19ms Core 0 blocking time)
- Hardware calibration procedure
- Current sensor requirements and circuit design
- DMA-based alternative approach
- Risk mitigation strategies

**When to Reference**:
- Implementing ACK detection logic
- Understanding ADC sample rate requirements
- Configuring free-running ADC mode
- Debugging ACK detection failures
- Hardware circuit verification

**Key Code Examples**:
- `detectACK()`: High-speed sampling during 8ms window
- `measureBaselineCurrent()`: Baseline establishment
- `analyzeACKSamples()`: Pulse detection algorithm
- ADC configuration: `adc_fifo_setup()`, `adc_set_clkdiv()`

**Critical Specifications**:
- 25 kHz sampling rate (150 samples per 6ms ACK pulse)
- 8ms detection window after packet transmission
- 60mA threshold above baseline (±15mA tolerance)
- 5-7ms pulse duration validation

**Last Updated**: October 18, 2025

---

### Planning Documents (Service Mode Programming)

#### 📙 `docs/service-mode-programming-plan.md`
**Purpose**: Analysis of persistent storage options for configuration  
**Audience**: Developers implementing calibration storage  
**Content**:
- Storage requirements (calibration values, tunable parameters)
- Comparison of 5 storage options (Flash, EEPROM, SPI Flash, SD Card, LittleFS)
- **Recommended solution**: Flash memory last sector (4KB)
- Complete `PicoConfigStorage` class implementation
- Linker script modifications to reserve flash space
- DCC-EX configuration commands (`<D CONFIG ...>`)
- Calibration workflow (`<D CAL ...>`)
- Write endurance analysis (10,000 cycles)
- Surviving firmware updates strategy

**When to Reference**:
- Implementing persistent configuration storage
- Storing calibration values
- Adding tunable parameters
- Understanding flash write performance
- Planning configuration commands

**Key Implementation**:
- `PicoConfigStorage` class (header and .cpp provided)
- CRC32 validation for data integrity
- Factory default values
- Flash write timing (410ms blocking)
- Configuration command examples

**Decision Summary**:
- ✅ Flash memory (last sector) - **Recommended**
- ❌ I2C EEPROM - Requires hardware changes
- ❌ SPI Flash - Overkill for small config
- ❌ SD Card - Too complex
- ⚠️ LittleFS - Pico W only

**Last Updated**: October 19, 2025

---

### Compliance & Safety Documents

#### 📕 `docs/dccex-compliance-analysis.md`
**Purpose**: Track DCC-EX protocol specification compliance  
**Audience**: Developers, testers, protocol compliance verification  
**Content**:
- Power control commands (implemented)
- Throttle commands (implemented)
- Function commands (implemented)
- Accessory commands (implemented)
- **CV programming commands (⚠️ STUBS ONLY)**
- Emergency stop (implemented as broadcast)
- Status and diagnostic commands
- Compliance gaps and future work

**When to Reference**:
- Verifying protocol compliance
- Checking command implementation status
- Planning DCC-EX feature additions
- Understanding command format requirements

**Key Status Items**:
- ✅ Basic operations: Power, throttle, functions, accessories
- ⚠️ CV Programming: Method stubs exist, ACK detection not implemented
- 📋 Future: Track current reporting, decoder programming

**Last Updated**: Pre-service mode planning (needs update after CV implementation)

---

#### 📕 `docs/safety-recommendations.md`
**Purpose**: Hardware safety and operational guidelines  
**Audience**: Users, hardware designers, safety compliance  
**Content**:
- Overcurrent protection requirements
- Emergency stop procedures
- Programming track safety (250mA limit)
- Thermal management
- Electrical isolation requirements
- User safety warnings

**When to Reference**:
- Hardware design review
- Safety feature implementation
- User manual creation
- Troubleshooting overcurrent issues

**Last Updated**: Initial safety analysis (may need CV programming safety additions)

---

### Reference Documents

#### 📄 `docs/DCC Service Mode Programming.html`
**Purpose**: Offline copy of DCC Wiki authoritative specification  
**Audience**: Reference for NMRA S-9.2.3 standards  
**Content**:
- Complete DCC Wiki Service Mode Programming article
- NMRA S-9.2.3 Direct Mode specification
- ACK detection official requirements (60mA for 6ms ±1ms)
- CV addressing and instruction formats
- Legacy Register and Paged mode specifications
- Decoder behavior requirements

**When to Reference**:
- Verifying packet format correctness
- Checking ACK timing specifications
- Understanding NMRA standards compliance
- Resolving specification ambiguities

**Source**: Offline copy provided by user (web version blocked by Cloudflare)

---

## Development Workflow Documents

### Build & Test Scripts

#### ⚙️ `scripts/Validate-DualMode.ps1`
**Purpose**: Automated validation of TEST and HARDWARE build modes  
**Usage**: `.\scripts\Validate-DualMode.ps1` or `.\scripts\Validate-DualMode.ps1 -SkipTests`  
**What It Does**:
1. Validates TEST_BUILD=ON mode (MSVC, CMocka tests)
2. Runs all 64 test cases (59 passing, 5 pre-existing failures)
3. Validates TEST_BUILD=OFF mode (ARM GCC, Pico hardware)
4. Reports comprehensive build compatibility status

**When to Use**:
- Before committing major changes
- After modifying shared headers or core components
- When debugging build issues
- Validating cross-mode compatibility

**Expected Output**:
```
Test Mode Validation: SUCCESS
  Controller Tests: 9/9 passed
  DCCEX Tests: 3/3 passed
  Locos Tests: 11/11 passed
  Loco Tests: 11/11 passed
  Packet Tests: 14/14 passed
  Track Tests: 11/16 passed (5 pre-existing failures)

Hardware Mode Validation: SUCCESS
  ARM GCC build: OK
  Firmware files generated: PicoDCC.elf, PicoDCC.uf2
```

---

### Build Configuration

#### 📋 `CMakeLists.txt` (Root)
**Purpose**: Master build configuration  
**Key Features**:
- `TEST_BUILD` flag (ON = Windows/MSVC tests, OFF = ARM GCC hardware)
- Conditional compiler selection
- CMocka integration for tests
- Pico SDK integration for hardware

**When to Modify**:
- Adding new library components
- Changing build modes
- Adding dependencies
- Configuring flash memory reservation (for config storage)

---

## Implementation Checklist

### Immediate Next Steps (Phase 1: ACK Detection)

**Prerequisites** (Week 0):
- [ ] Analyze PCB schematic for current sensor circuit
- [ ] Measure ADC reading with 100mA calibration load
- [ ] Calculate `adc_to_ma_conversion` factor
- [ ] Confirm known-good decoder available (ESU, TCS, or Digitrax)
- [ ] Review `docs/ack-detection-analysis.md` for implementation details

**Implementation** (Week 1-2):
- [ ] Create `lib/PicoDCCProgrammer/` directory structure
- [ ] Create `pico_dccprogrammer.h` class skeleton
- [ ] Implement `measureBaselineCurrent()` method
- [ ] Implement `detectACK()` with free-running ADC
- [ ] Implement `analyzeACKSamples()` pulse detection
- [ ] Add unit tests with mock ADC operations
- [ ] Bench test with real decoder

**Parallel Work** (Week 1-2):
- [ ] Create `lib/PicoConfigStorage/` (optional, recommended)
- [ ] Implement flash storage class from `docs/non-volatile-storage-options.md`
- [ ] Modify linker script to reserve last 4KB sector
- [ ] Add DCC-EX configuration commands
- [ ] Test calibration workflow

---

## Quick Reference

### Finding Information

| Question | Document to Check |
|----------|-------------------|
| How does the dual-core architecture work? | `docs/architecture.md` |
| How do I build in TEST vs HARDWARE mode? | `.github/copilot-instructions.md` |
| What are the ACK detection specifications? | `docs/service-mode-programming-plan.md` |
| How do I implement ACK detection in code? | `docs/ack-detection-analysis.md` |
| How do I store calibration values? | `docs/non-volatile-storage-options.md` |
| What DCC-EX commands are implemented? | `docs/dccex-compliance-analysis.md` |
| What are the official NMRA specifications? | `docs/DCC Service Mode Programming.html` |
| How do I validate both build modes? | `scripts/Validate-DualMode.ps1` |
| What are the safety requirements? | `docs/safety-recommendations.md` |

### Key Contacts & Resources

- **NMRA DCC Standards**: [dccwiki.com](https://dccwiki.com) (offline copy in docs/)
- **DCC-EX Protocol**: [DCC-EX Command Reference](https://dcc-ex.com/reference/software/command-reference.html)
- **Pico SDK Documentation**: [Raspberry Pi Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/)

---

## Document Maintenance

### When to Update This Index

- ✅ When new planning documents are created
- ✅ When implementation phases change status
- ✅ When new features are completed
- ✅ When architectural documents are updated
- ✅ Weekly during active development

### Document Status Symbols

- ✅ **Implemented/Complete**
- 🚧 **In Progress**
- 📋 **Planned**
- ⚠️ **Partially Implemented**
- ❌ **Not Recommended/Deprecated**
- 🔴 **Critical Priority**
- 🟡 **Optional/Nice-to-Have**

---

## Version History

| Date | Change | Author |
|------|--------|--------|
| 2025-10-19 | Initial index created | AI Agent |
| TBD | Phase 1 implementation started | - |
| TBD | Configuration storage implemented | - |
| TBD | Address programming completed (Phase 4) | - |

---

## Contact & Support

For questions about implementation details, refer to the specific documents above. Each document includes:
- **Purpose**: What problem it solves
- **Audience**: Who should read it
- **When to Reference**: Specific use cases
- **Key Sections**: Important areas to focus on

**Happy coding! 🚂**
