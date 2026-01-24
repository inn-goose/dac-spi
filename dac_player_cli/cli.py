#!/usr/bin/env python3

import numpy as np
import wave
import argparse


def process_wav_file(wav_file: str):
    with wave.open(wav_file, "rb") as wf:
        nchannels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        framerate = wf.getframerate()
        n_frames = wf.getnframes()

        # Read all frames as bytes
        raw_bytes = wf.readframes(n_frames)

    if nchannels > 2:
        raise Exception("3+ channels mode is not supported")

    no_channels = "mono" if nchannels == 1 else "stereo"
    print(f"// channels: {no_channels}, resolution: {sampwidth * 8} bit, sample_rate: {framerate} Hz\n")

    print(f"static const size_t PCM_SAMPLES_COUNT = {n_frames};\n")

    # Convert bytes to unsigned integers (0–255)
    samples_u8 = np.frombuffer(raw_bytes, dtype=np.uint16)

    # Convert to signed integers (-128 to 127)
    samples_s8 = samples_u8.astype(np.int16)

    if nchannels == 2:
        left_channel  = samples_s8[0::2]
        right_channel = samples_s8[1::2]
    else:
        left_channel = samples_s8
        right_channel = None

    print("static const int32_t PCM_SAMPLES_LEFT[] = {")
    for bb in left_channel:
        print(f"  {bb},")
    print("};")

    if right_channel:
        print("")
        print("static const int32_t PCM_SAMPLES_RIGHT[] = {")
        for bb in right_channel:
            print(f"  {bb},")
        print("};")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="WAV converter")
    parser.add_argument("wav_file")
    args = parser.parse_args()

    process_wav_file(args.wav_file)