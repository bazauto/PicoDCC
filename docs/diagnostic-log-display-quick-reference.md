# Diagnostic Log Display - Quick Reference

## User Guide

### Accessing Logs

1. **Main Screen**: Bottom of screen shows `Logs: N` count
2. **Tap "View Logs" Button**: Opens log viewer screen
3. **Scroll Through Logs**: Touch and drag to scroll
4. **Return**: Tap "Back" button to return to main screen
5. **Remote Dump**: Send `<D LOG GET>` over DCC-EX to stream the same formatted log lines to your console (capped at 20 newest entries, ends with `<D LOG END>`)

### Log Entry Format

```
[TIME] LEVEL COMPONENT: message
```

**Example**:
```
[1.234] ERROR TRACK: Overcurrent detected
[2.456] WARN CTRL: DCC timing violation
[3.789] INFO DISP: Screen initialized
```

### Severity Levels

| Level    | Color  | Meaning                                    |
|----------|--------|--------------------------------------------|
| CRITICAL | Red    | System failure, safety violation          |
| ERROR    | Orange | Component error, operation failed         |
| WARNING  | Yellow | Non-critical issue, degraded operation    |
| INFO     | White  | Normal status information                 |

### Log Management

- **Auto-Wraparound**: Oldest logs are overwritten after 30 entries
- **Clear Logs**: Tap "Clear" button to erase all entries
- **Live Count**: Main screen shows current log count (yellow when > 0)

### Components

| Code  | Full Name     | Description                    |
|-------|---------------|--------------------------------|
| CTRL  | Controller    | Main DCC controller operations |
| TRACK | Track         | Track power and current        |
| LOCO  | Locomotive    | Individual loco management     |
| DISP  | Display       | LCD and UI operations          |
| DCCEX | DCC-EX        | DCC-EX protocol handling       |
| CFG   | Config        | Configuration storage          |

---

## Developer Reference

### Adding Log Entries

#### C++ Code
```cpp
#include "../lib/pico_diagnostic.h"

// Error example
LOG_ERROR(COMPONENT_TRACK, "Overcurrent detected");

// Warning example
LOG_WARNING(COMPONENT_CONTROLLER, "DCC timing violation");

// Info example
LOG_INFO(COMPONENT_DISPLAY, "Screen initialized");

// Critical example
LOG_CRITICAL(COMPONENT_CONTROLLER, "Emergency stop triggered");
```

### Macros Available

```cpp
LOG_CRITICAL(component, message)  // Red, system failure
LOG_ERROR(component, message)     // Orange, operation failed
LOG_WARNING(component, message)   // Yellow, non-critical issue
LOG_INFO(component, message)      // White, status information
```

### Component Identifiers

```cpp
COMPONENT_CONTROLLER  // "CONTROLLER"
COMPONENT_TRACK       // "TRACK"
COMPONENT_LOCO        // "LOCO"
COMPONENT_DISPLAY     // "DISPLAY"
COMPONENT_DCCEX       // "DCCEX"
COMPONENT_CONFIG      // "CONFIG"
```

### Buffer Management API

```cpp
// Initialize buffer (call once at startup)
void diag_log_init();

// Add message to buffer (called by LOG_* macros)
void diag_log_add(const diagnostic_msg_t* msg);

// Get number of valid entries (0-30)
uint32_t diag_log_get_count();

// Get entry by index (0=oldest, count-1=newest)
const diagnostic_msg_t* diag_log_get_entry(uint32_t index);

// Clear all entries
void diag_log_clear();
```

### Display Integration

```cpp
// IDisplayRenderer interface methods
virtual void showLogScreen() = 0;      // Navigate to log screen
virtual void updateLogScreen() = 0;    // Refresh log display

// LvglRenderer implementation
void createLogScreen();                 // Create LVGL UI
void showLogScreen() override;          // Switch to log screen
void updateLogScreen() override;        // Populate log table
```

---

## Troubleshooting

### Logs Not Appearing

1. **Check Initialization**: Ensure `diag_log_init()` called in `main()`
2. **Check Macro Usage**: Verify using `LOG_*()` macros correctly
3. **Check Component**: Valid component identifier used?
4. **Check Buffer**: Did logs wrap (>30 entries)?

### Display Issues

1. **Blank Screen**: Check `createLogScreen()` called
2. **No Scroll**: Check log count > visible area
3. **Wrong Colors**: Verify severity color mapping
4. **Freeze**: Check for LVGL task overflow

### Performance Issues

1. **Slow Updates**: Reduce log count or update frequency
2. **Memory Warnings**: Check buffer size (2KB fixed)
3. **Screen Lag**: Optimize LVGL refresh rate

---

## Best Practices

### When to Log

✅ **DO LOG**:
- Critical errors (safety violations)
- Hardware failures
- Protocol errors
- Unexpected conditions
- Important state changes

❌ **DON'T LOG**:
- Every packet transmission
- Normal operations (too verbose)
- High-frequency events (>100 Hz)
- Debug spam in production

### Message Guidelines

✅ **Good Messages**:
- "Overcurrent detected: 1234 mA"
- "DCC timing violation: bit width 52 µs"
- "Emergency stop triggered by user"

❌ **Bad Messages**:
- "Error" (too vague)
- "Something went wrong" (no detail)
- "XYZABC123" (cryptic codes)

### Performance Tips

1. **Pre-format Messages**: Use constants for repeated strings
2. **Limit Log Frequency**: Don't log in tight loops
3. **Use Appropriate Levels**: Reserve CRITICAL for real emergencies
4. **Clear Regularly**: User should clear logs after review

---

## Example Usage Scenarios

### Scenario 1: Track Overcurrent

```cpp
// In PicoDccTrack::loop()
if (current_ma > CURRENT_LIMIT_MA) {
    LOG_ERROR(COMPONENT_TRACK, "Overcurrent detected");
    powerOff();
}
```

**Display**:
```
[12.345] ERROR TRACK: Overcurrent detected
```

### Scenario 2: DCC Timing Violation

```cpp
// In PicoDccController::dccexLoop()
if (bit_width_us < MIN_BIT_WIDTH) {
    LOG_WARNING(COMPONENT_CONTROLLER, "DCC timing violation");
}
```

**Display**:
```
[5.678] WARN CTRL: DCC timing violation
```

### Scenario 3: Successful Initialization

```cpp
// In PicoDCCDisplay::init()
if (lcd_driver_->init()) {
    LOG_INFO(COMPONENT_DISPLAY, "LCD initialized");
}
```

**Display**:
```
[0.123] INFO DISP: LCD initialized
```

### Scenario 4: Emergency Stop

```cpp
// In PicoDccController::emergencyStop()
LOG_CRITICAL(COMPONENT_CONTROLLER, "Emergency stop triggered");
clearQueue();
powerOff();
```

**Display** (Red text):
```
[3.456] CRIT CTRL: Emergency stop triggered
```

---

## Integration Checklist

### Phase 1: Storage ✅
- [x] PicoDiagnostic library created
- [x] Circular buffer implemented
- [x] Unit tests passing (9/9)
- [x] `diag_log_init()` called in `main()`

### Phase 2: Display ✅
- [x] Log screen UI created
- [x] LVGL renderer updated
- [x] Navigation buttons wired
- [x] Color-coding implemented

### Phase 3: Main Screen ✅
- [x] Log count indicator added
- [x] "View Logs" button added
- [x] Live count updates working
- [x] Color changes with log presence

### Phase 4: Hardware Testing ⏳
- [ ] Flash firmware to Pico
- [ ] Trigger test errors
- [ ] Verify log capture
- [ ] Test wraparound (>30 entries)
- [ ] Test touch navigation
- [ ] Verify color-coding
- [ ] Test clear operation
- [ ] Performance validation

---

## Memory & Performance Stats

| Metric                  | Value        | Notes                          |
|-------------------------|--------------|--------------------------------|
| Buffer Size             | ~2KB         | 30 entries × ~64 bytes         |
| Display Buffer          | 4KB          | Temporary for UI rendering     |
| Total RAM Impact        | ~6KB         | Minimal overhead               |
| Max Entries             | 30           | Circular buffer with wraparound|
| Add Operation           | O(1)         | Constant time insertion        |
| Read Operation          | O(1)         | Direct index access            |
| Display Update          | O(n)         | Linear in entry count          |
| Update Frequency        | ~10 Hz       | Main screen refresh rate       |

---

## Related Documentation

- **Implementation**: `docs/diagnostic-log-display-implementation.md`
- **Architecture**: `docs/diagnostic-log-display-architecture.md`
- **Main Architecture**: `docs/architecture.md`
- **LCD Integration**: `docs/lcd-integration.md`
- **Unit Tests**: `test/pico_diagnostic_tests.cpp`
- **API Reference**: `lib/pico_diagnostic.h`
