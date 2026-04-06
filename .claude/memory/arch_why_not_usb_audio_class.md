---
name: Architecture Challenge - Why Not USB Audio Class?
description: USB Audio device class would make the GIGA appear as a standard sound card — no Python CLI needed, any app can play to it
type: project
---

**Challenge:** The current architecture requires a custom Python CLI to stream audio. You can't use Spotify, VLC, or any standard audio app. The GIGA is invisible to the OS as an audio device.

**USB Audio Class alternative:**
- The STM32H747 supports USB Device with custom class implementations
- Implementing USB Audio Class 1.0 (UAC1) would make the GIGA appear as a USB sound card
- The OS would route audio to it natively — any application works
- No Python CLI, no custom protocol, no WAV file parsing

**Why it's hard:**
1. USB Audio Class on STM32 requires writing a custom USB device descriptor and handling isochronous transfers
2. ArduinoCore-mbed uses the USB stack for Serial (CDC). Running both CDC and Audio on the same USB port requires composite device support
3. Isochronous USB transfers have strict timing requirements — data must be ready every 1ms frame
4. The Arduino GIGA's USB stack may not support custom device classes easily
5. UAC1 limits to 96kHz; UAC2 (async) is much more complex

**Partial alternative: virtual sound card on PC side:**
- A PC-side driver or app that captures system audio and streams it over serial
- Keeps the serial protocol but makes it transparent to applications
- Easier to implement than USB Audio on the device side

**Verdict:** USB Audio is the "right" answer for a product. But it's a completely different project — USB stack work, not embedded audio work. The serial streaming approach is appropriate for a learning/blog project where the focus is on the DAC driver and dual-core architecture.
