---
name: Refactor - Global DacOutput Pointer for ISR
description: dac_output.h uses a global g_dac_instance pointer for the Ticker ISR callback — fragile pattern
type: project
---

**File:** `dac_output.h:12`

**Problem:**
```cpp
static DacOutput* g_dac_instance = nullptr;
```

This global pointer is set in `DacOutput::setup()` and used in the static `_isr_static()` method. It's a common embedded pattern but has issues:
1. Only one DacOutput instance can exist (second instance overwrites the pointer)
2. If `setup()` isn't called, the ISR dereferences nullptr
3. The `static` keyword in a header means each translation unit gets its own copy — but since this is included only from `dac_spi.ino` (via `core_m4.h`), there's only one copy

**Why it exists:** `mbed::Ticker` needs a static callback function. You can't pass a member function pointer. The `mbed::callback()` wrapper with a static function is the standard workaround.

**Potential fix:** Use a lambda capture or `mbed::callback(this, &DacOutput::_isr)` if mbed supports it. Actually, `mbed::Ticker::attach_us` accepts `mbed::Callback<void()>` which CAN capture `this`:
```cpp
_ticker.attach_us(mbed::callback(this, &DacOutput::_isr), sample_period_us);
```
Then make `_isr()` non-static and remove `g_dac_instance`.

**Risk:** Need to verify mbed::Callback works from ISR context with a captured `this`. It should — mbed::Callback is designed for this.

**Estimated gain:** Cleaner code, no global state. Enables multiple DacOutput instances (unlikely to need, but cleaner).
