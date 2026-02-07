#!/usr/bin/env python3

import argparse
import time
import numpy as np
import wave

from core.dac_spi_client import DacSpiClient


class CliError(Exception):
    pass


def connect_dac_spi(port: str, baudrate: int, init_timeout: int):
    print(f"connect DAC SPI: {port}")

    ts = time.time()

    dac_spi = DacSpiClient(
        port=port, baudrate=baudrate, init_timeout=float(init_timeout))

    elapsed = time.time() - ts
    print(f"connect board: DONE, {elapsed:.02f} sec")

    return dac_spi


def _parse_wav_file(wav_file: str):
    with wave.open(wav_file, "rb") as wf:
        n_channels = wf.getnchannels()
        n_frames = wf.getnframes()
        frame_size = wf.getsampwidth()  # words
        raw_bytes = wf.readframes(n_channels * n_frames * frame_size)
        sample_rate = wf.getframerate()

    samples_buffer = np.frombuffer(raw_bytes, dtype=np.int16)
    frames_count = n_channels * n_frames

    resolution = frame_size * 8

    return n_channels, frames_count, samples_buffer, resolution, sample_rate


def _play_frame(dac_spi: DacSpiClient, left_channel, right_channel):
    print(f"_play_frame: {len(left_channel)} / {len(right_channel)}")

    ts = time.time()

    try:
        dac_spi.play_frame(left_channel, right_channel)
    except Exception as ex:
        raise CliError(f"play frame: failed, {str(ex)}")

    elapsed = time.time() - ts
    print(f"play frame: DONE, {elapsed:.02f} sec")


def _stream_frame(dac_spi: DacSpiClient, n_channels, frame_buffer, frame_buffer_size):
    print(f"_stream_frame: channels: {n_channels}, frame_buffer_size: {frame_buffer_size}")

    ts = time.time()

    try:
        res = dac_spi.stream_frame(n_channels, frame_buffer, frame_buffer_size)
    except Exception as ex:
        raise CliError(f"stream frame: failed, {str(ex)}")

    elapsed = time.time() - ts
    print(f"stream frame: DONE, {elapsed:.02f} sec, res: {res}")


def play_wav_file(dac_spi: DacSpiClient, wav_file: str, frame_size: int):
    n_channels, frames_count, samples_buffer, resolution, sample_rate = _parse_wav_file(wav_file)

    if n_channels > 2:
        raise CliError("3+ channels mode is not supported")

    mode = "mono" if n_channels == 1 else "stereo"
    print(f"playing | mode: {mode}, resolution: {resolution} bit, sample_rate: {sample_rate} Hz")

    if n_channels == 2:
        left_frames  = samples_buffer[0::2]
        right_frames = samples_buffer[1::2]
    else:
        left_frames = samples_buffer
        right_frames = None

    if right_frames is not None:
        for start in range(0, frames_count, frame_size):
            left_frame = left_frames[start:start + frame_size].tolist()
            right_frame = right_frames[start:start + frame_size].tolist()
            _play_frame(dac_spi, left_frame, right_frame)
    else:
        for start in range(0, frames_count, frame_size):
            left_frame = left_frames[start:start + frame_size].tolist()
            _play_frame(dac_spi, left_frame, [])


def stream_wav_file(dac_spi: DacSpiClient, wav_file: str, frame_size: int):
    n_channels, frames_count, samples_buffer, resolution, sample_rate = _parse_wav_file(wav_file)

    if n_channels > 2:
        raise CliError("3+ channels mode is not supported")

    mode = "mono" if n_channels == 1 else "stereo"
    print(f"streaming | mode: {mode}, frames_count: {frames_count}, resolution: {resolution} bit, sample_rate: {sample_rate} Hz")

    for start in range(0, frames_count, frame_size):
        frame_buffer = samples_buffer[start:start + frame_size].tolist()
        _stream_frame(dac_spi, n_channels, frame_buffer, len(frame_buffer))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="WAV converter")
    parser.add_argument("port", type=str, metavar="<port>",
                        help="Specify the USP port address for the serial connection")
    parser.add_argument("--baudrate", type=int, default=1000000,
                        metavar="<baud>", help="Set the serial connection speed")
    parser.add_argument("--init-timeout", type=int, default=3, metavar="<sec>",
                        help="Set the MAX Arduino's reset-on-connect timeout in seconds")
    parser.add_argument("-p", "--play", type=str, required=False, metavar="<filename>",
                        help="")
    parser.add_argument("-s", "--stream", type=str, required=False, metavar="<filename>",
                        help="")
    parser.add_argument("--frame-size", type=int, default=1000,
                        help="")
    args = parser.parse_args()

    # connect
    dac_spi = connect_dac_spi(
        args.port, args.baudrate, args.init_timeout)

    if args.play is not None:
        play_wav_file(dac_spi, args.play, args.frame_size)

    if args.stream is not None:
        stream_wav_file(dac_spi, args.stream, args.frame_size)
