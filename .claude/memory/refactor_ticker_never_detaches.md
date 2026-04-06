---
name: Refactor - Ticker Never Detaches After Playback Ends
description: mbed::Ticker runs forever once started — ISR fires at sample rate even when idle, consuming CPU and power
type: project
---

**File:** `dac_output.h:126-131` and `dac_output.h:149`

**Problem:** The Ticker is attached once and never detached:
```cpp
if (!_ticker_running) {
  _ticker.attach_us(mbed::callback(&DacOutput::_isr_static), sample_period_us);
  _ticker_running = true;
}
// ...
// In ISR:
if (!_playing) return;  // idle tick — Ticker stays running
// DON'T detach — Ticker keeps running, ISR returns early until next buffer
```

At 44.1kHz, the ISR fires 44,100 times per second even when no audio is playing. Each ISR entry has overhead (context save, function call, `if (!_playing) return`, context restore). On Cortex-M4 this is probably ~1-2μs per interrupt = ~44-88μs/sec wasted. Negligible, but not zero.

**Why it was designed this way:** The comment says "DON'T detach — Ticker keeps running." The reason is that `attach_us()` / `detach()` cycles have their own overhead and can cause timing glitches at buffer boundaries. Keeping the ticker running eliminates the startup jitter when a new buffer arrives.

**Potential concern:** If sample_rate changes between tracks (e.g., 22050 → 44100), the Ticker period doesn't update. The current code only attaches once. A new `start_playback()` with a different sample rate would play at the old rate.

**Potential fix for sample rate change:**
```cpp
if (!_ticker_running || sample_rate != _current_sample_rate) {
  if (_ticker_running) _ticker.detach();
  _ticker.attach_us(..., 1000000UL / sample_rate);
  _ticker_running = true;
  _current_sample_rate = sample_rate;
}
```

**Risk:** Medium. The sample rate issue is a real bug if you play files with different sample rates in sequence.

**Estimated gain:** Correctness fix for mixed sample rates. Power savings from detaching when truly idle (minor).
