# Diagnostic Log Display Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     PicoDCC Application                          │
│                                                                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Controller   │  │    Track     │  │    Display   │          │
│  │              │  │              │  │              │          │
│  │ LOG_ERROR()  │  │ LOG_WARN()   │  │ LOG_INFO()   │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
│         │                  │                  │                  │
│         └──────────────────┼──────────────────┘                  │
│                            │                                     │
│                            ▼                                     │
│                 ┌─────────────────────┐                         │
│                 │  log_diagnostic()   │                         │
│                 │  (pico_diagnostic.h)│                         │
│                 └──────────┬──────────┘                         │
│                            │                                     │
│                            ▼                                     │
│                 ┌─────────────────────┐                         │
│                 │  diag_log_add()     │                         │
│                 │  (Circular Buffer)  │                         │
│                 └──────────┬──────────┘                         │
│                            │                                     │
│                            ▼                                     │
│              ┌──────────────────────────┐                       │
│              │  g_diag_log_buffer       │                       │
│              │  (30 entries, ~2KB RAM)  │                       │
│              │  ┌────┬────┬────┬─────┐ │                       │
│              │  │ 0  │ 1  │... │ 29  │ │                       │
│              │  └────┴────┴────┴─────┘ │                       │
│              │  head: 5, count: 30     │                       │
│              └──────────┬───────────────┘                       │
│                         │                                        │
│         ┌───────────────┴────────────────┐                     │
│         │                                 │                     │
│         ▼                                 ▼                     │
│  ┌──────────────┐              ┌──────────────────┐           │
│  │ Main Screen  │              │  Log Viewer      │           │
│  │              │  View Logs   │  Screen          │           │
│  │ Logs: 5      ├─────────────►│                  │           │
│  │ [View Logs]  │              │  [TIME] LEVEL... │           │
│  └──────────────┘              │  [TIME] LEVEL... │           │
│                                 │  ...             │           │
│                                 │  [Clear] [Back]  │           │
│                                 └──────────────────┘           │
└─────────────────────────────────────────────────────────────────┘
```

## Data Flow

### 1. Log Entry Creation
```
Component Code (anywhere in system)
    ↓
LOG_ERROR(COMPONENT_TRACK, "Overcurrent detected")
    ↓
log_diagnostic(DIAG_ERROR, "TRACK", "Overcurrent detected")
    ↓
Creates diagnostic_msg_t:
  - level: DIAG_ERROR
  - timestamp: time_us_32() / 1000
  - component: "TRACK"
  - message: "Overcurrent detected"
    ↓
diag_log_add(&msg)
    ↓
Stores in circular buffer at g_diag_log_buffer.entries[head]
Increments head (wraps at 30)
Increments count (caps at 30)
```

### 2. Log Display Retrieval
```
User taps "View Logs" button
    ↓
onViewLogsClicked() event handler
    ↓
LvglRenderer::showLogScreen()
    ↓
LvglRenderer::updateLogScreen()
    ↓
count = diag_log_get_count()  // Returns number of valid entries
    ↓
for (i = 0; i < count; i++) {
    entry = diag_log_get_entry(i)  // 0 = oldest, count-1 = newest
    Format as: "[1.234] ERROR TRACK: Overcurrent detected"
    Append to display buffer
}
    ↓
lv_textarea_set_text(log_table_, formatted_text)
    ↓
Display rendered on LCD (320x240 ST7789T3)
```

### 3. Main Screen Update Loop
```
PicoDCCDisplay::loop() (called from main)
    ↓
updateDiagnosticScreen(track_status)
    ↓
count = diag_log_get_count()
    ↓
snprintf(buf, "Logs: %lu", count)
    ↓
lv_label_set_text(log_count_label_, buf)
    ↓
Change color: count > 0 ? YELLOW : WHITE
```

## Memory Layout

```
┌─────────────────────────────────────────────────┐
│                   RAM (264KB)                    │
├─────────────────────────────────────────────────┤
│                                                  │
│  Application Code & Data                        │
│                                                  │
│  ┌────────────────────────────────────────┐    │
│  │  g_diag_log_buffer  (~2KB)             │    │
│  │  ┌──────────────────────────────────┐  │    │
│  │  │ entries[30] (diagnostic_msg_t)   │  │    │
│  │  │   Each entry: ~64 bytes          │  │    │
│  │  │   - level: 4 bytes               │  │    │
│  │  │   - timestamp: 4 bytes           │  │    │
│  │  │   - component: 8 bytes (ptr)     │  │    │
│  │  │   - message: 48 bytes (ptr)      │  │    │
│  │  └──────────────────────────────────┘  │    │
│  │  head: 1 byte                          │    │
│  │  count: 1 byte                         │    │
│  │  initialized: 1 byte                   │    │
│  └────────────────────────────────────────┘    │
│                                                  │
│  LVGL Display Buffer (2 × 10KB partial)         │
│  Stack & Heap                                   │
│  Other Variables                                │
│                                                  │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│              Flash Storage (264KB)               │
├─────────────────────────────────────────────────┤
│                                                  │
│  PicoDCC Firmware (~200KB)                      │
│  LVGL Library (~40KB)                           │
│  Pico SDK (~24KB)                               │
│                                                  │
│  Last 4KB Sector: PicoConfigStorage             │
│  (Future: Could add persistent log storage)     │
│                                                  │
└─────────────────────────────────────────────────┘
```

## Component Interaction

```
┌──────────────────┐
│ PicoDccController│
│                  │
│  - dccexLoop()   │──┐
│  - safety checks │  │
└──────────────────┘  │
                      │
┌──────────────────┐  │
│  PicoDccTrack    │  │   LOG_*() calls
│                  │  │        ↓
│  - loop()        │──┼──► pico_diagnostic.h
│  - overcurrent   │  │        ↓
└──────────────────┘  │   log_diagnostic()
                      │        ↓
┌──────────────────┐  │   diag_log_add()
│  PicoDccLoco     │  │        ↓
│                  │  │   g_diag_log_buffer
│  - setSpeed()    │──┘
│  - setFunction() │
└──────────────────┘

        ↕ (read log count)

┌──────────────────┐      ┌──────────────────┐
│ PicoDCCDisplay   │◄─────│  LvglRenderer    │
│                  │      │                  │
│  - loop()        │      │  - createLogScreen()
│  - init()        │      │  - updateLogScreen()
└──────────────────┘      │  - showLogScreen()
                          └──────────────────┘
                                    │
                                    ▼
                          ┌──────────────────┐
                          │   LCD Hardware   │
                          │   ST7789T3       │
                          │   320×240 SPI    │
                          └──────────────────┘
```

## Circular Buffer Behavior

### Empty Buffer (count=0)
```
entries: [ _ | _ | _ | ... | _ ]  (30 slots)
head: 0
count: 0
```

### Adding First Entry (count=1)
```
entries: [ E0 | _ | _ | ... | _ ]
         ↑
        head=0, count=1
```

### Adding 5 Entries (count=5)
```
entries: [ E0 | E1 | E2 | E3 | E4 | _ | ... | _ ]
                                  ↑
                                 head=5, count=5
```

### Full Buffer (count=30)
```
entries: [ E0 | E1 | E2 | ... | E28 | E29 ]
          ↑
         head=0, count=30
```

### Overflow (add 31st entry, wraps)
```
entries: [ E30 | E1 | E2 | ... | E28 | E29 ]
                ↑
               head=1, count=30
Oldest entry (E0) overwritten by E30
```

### Reading Entries
```
diag_log_get_entry(0) → E1  (oldest)
diag_log_get_entry(1) → E2
...
diag_log_get_entry(29) → E30 (newest)
```

## Touch Event Flow

```
User Interaction Flow:

1. Main Screen:
   ┌────────────────┐
   │  Logs: 5       │  ← Shows live count
   │  [View Logs]   │  ← Tap here
   └────────────────┘
          │
          │ onViewLogsClicked()
          ▼
2. Log Viewer Screen:
   ┌────────────────┐
   │ Diagnostic Logs│
   │ ┌────────────┐ │
   │ │[1.2] ERROR │ │
   │ │[2.3] WARN  │ │  ← Scrollable
   │ │[3.4] INFO  │ │
   │ └────────────┘ │
   │ [Clear] [Back] │  ← Tap Back
   └────────────────┘
          │
          │ onBackToMainClicked()
          ▼
3. Back to Main Screen

Alternative:
   [Clear] button → onClearLogsClicked()
                  → diag_log_clear()
                  → updateLogScreen() (now empty)
```

## Build Modes

```
┌──────────────────────────────────────────────────┐
│              CMakeLists.txt                       │
│         if(TEST_BUILD)      else                 │
├─────────────────────────┬────────────────────────┤
│    TEST MODE            │   HARDWARE MODE        │
├─────────────────────────┼────────────────────────┤
│ Compiler: MSVC          │ Compiler: ARM GCC      │
│ Target: Windows .exe    │ Target: Pico .elf/.uf2 │
│                         │                        │
│ Hardware Mocks:         │ Real Hardware:         │
│  - mock_time_ms         │  - time_us_32()        │
│  - MockDisplayRenderer  │  - LvglRenderer        │
│  - No LCD/Touch         │  - ST7789T3 LCD        │
│                         │  - CST328 Touch        │
│                         │                        │
│ Testing Framework:      │ Runtime:               │
│  - CMocka 1.1.0         │  - Pico SDK 2.2.0      │
│  - 9 diagnostic tests   │  - LVGL 8.3            │
│  - Unit test execution  │  - ARM Cortex-M33      │
└─────────────────────────┴────────────────────────┘
```

## Error Handling

```
Scenario: Buffer Uninitialized
    diag_log_add() → Check g_diag_log_buffer.initialized
                  → If false, return early (safe no-op)

Scenario: Invalid Index
    diag_log_get_entry(35) → Check index < count
                           → If invalid, return NULL

Scenario: Display Update Fails
    updateLogScreen() → Check log_table_ != NULL
                      → If NULL, return early

Scenario: Memory Allocation
    No heap allocations → Uses static global buffer
                        → Cannot fail due to OOM
```

---

This architecture ensures:
- ✅ **Thread-safe**: Single writer (Core 0), atomic reads
- ✅ **Memory-efficient**: Fixed 2KB buffer, no dynamic allocation
- ✅ **Fail-safe**: Graceful handling of uninitialized/invalid states
- ✅ **Testable**: Comprehensive unit test coverage
- ✅ **User-friendly**: Touch navigation, color-coded severity
- ✅ **Maintainable**: Clean separation of concerns, well-documented
