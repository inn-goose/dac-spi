---
name: Refactor - CLI Silently Does Nothing Without -s Flag
description: cli.py connects to the board but does nothing if -s is not provided — no error message or help
type: project
---

**File:** `dac_spi_cli/cli.py:222-223`

**Problem:**
```python
if args.stream is not None:
    stream_wav_file(dac_spi, args.stream, args.frame_size, debug=args.debug)
# else: nothing happens, program exits silently
```

If you run `./cli.py /dev/cu.usbmodem2101` without `-s`, it connects to the board, prints "connect board: DONE", and exits. No error, no help. Confusing.

**Potential fix:**
```python
if args.stream is not None:
    stream_wav_file(dac_spi, args.stream, args.frame_size, debug=args.debug)
else:
    print("connected. use -s <file.wav> to stream")
```

Or make `-s` required.

**Risk:** None.
