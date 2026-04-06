---
name: Architecture Challenge - Protocol Has No Error Recovery
description: No checksums, no sequence numbers, no retransmission — a corrupted packet means a gap or click in audio
type: project
---

**Challenge:** The protocol v2 has:
- Header sync bytes (0xAB 0xCD 0xEF) — for initial sync only
- Metadata once, then pure data packets
- ACK per packet ("1")
- No checksum on data
- No sequence numbers
- No retransmission mechanism

**What happens on corruption:**
1. **Corrupted header:** Parser re-syncs by scanning for 0xAB 0xCD 0xEF. Bytes are lost until next valid header. Audio gap.
2. **Corrupted payload byte:** Written to shared memory as-is. Audio click/pop. No detection.
3. **Lost byte (short packet):** Parser waits for remaining bytes, eventually times out (10ms), resets. Audio gap.
4. **Extra byte (USB glitch):** Shifts all subsequent bytes. Parser eventually loses sync, re-syncs on next header. Multiple corrupted packets.

**Is this a real problem?** USB CDC is reliable at the transport layer. Bytes don't get lost or corrupted in transit. The sync mechanism is there for connection startup (when the serial buffer may have garbage from a previous session), not for runtime recovery.

**But:** If the Arduino resets mid-stream, or the USB cable is briefly disconnected, there's no recovery mechanism. The CLI would need to detect the missing ACK, reconnect, and resend metadata.

**Potential improvements (if needed):**
1. CRC-16 per data packet (2 bytes overhead, catches corruption)
2. Sequence number (1 byte, detects missing packets)
3. Periodic metadata resend (allows recovery from Arduino reset)

**Verdict:** Not needed for USB CDC. Would be needed if the transport moved to Bluetooth, WiFi, or actual UART. Worth noting as a deliberate simplification.
