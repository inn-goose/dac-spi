#!/usr/bin/env python3

import struct
import argparse
import time
import numpy as np
import wave

from typing import Optional, Tuple

import serial


class CliError(Exception):
    pass


class ArduinoSerialClientError(Exception):
    pass


class ArduinoSerialClient:
    """
    https://pyserial.readthedocs.io/en/latest/pyserial.html
    """

    RESPONSE_READ_TIMEOUT_SEC = 10.0

    def __init__(self, port: str, baudrate: int, init_timeout: float, read_timeout: Optional[float] = None, write_timeout: Optional[float] = None):
        self.port = port
        self.baudrate = baudrate
        self.init_timeout = init_timeout
        self.read_timeout = read_timeout
        self.write_timeout = write_timeout
        self.serial = None

    def init(self):
        if self.serial is not None:
            return

        try:
            self.serial = serial.Serial(
                port=self.port, baudrate=self.baudrate,
                timeout=self.read_timeout or self.RESPONSE_READ_TIMEOUT_SEC,
                write_timeout=self.write_timeout)
        except Exception as ex:
            raise ArduinoSerialClientError(
                f"failed to open serial port with {str(ex)}")

    def _read_raw_response(self) -> Tuple[Optional[str], float]:
        if self.serial is None:
            raise ArduinoSerialClientError("uninitialized serial protocol")

        start_ts = time.monotonic()
        data = self.serial.read(1)  # blocks until 1 byte or timeout
        elapsed = time.monotonic() - start_ts

        if not data:
            return None, elapsed

        return data.decode(), elapsed

    def send_raw_data(self, data, raw=False):
        if self.serial is None:
            raise ArduinoSerialClientError("uninitialized serial protocol")

        w_res = self.serial.write(data)
        if not w_res:
            raise ArduinoSerialClientError(
                "failed to send request, 0 bytes written")

        if raw:
            data = self.serial.read(1)
            if not data:
                raise ArduinoSerialClientError(
                    "failed to read RAW response (timeout)")
            return data

        response, resp_wait_sec = self._read_raw_response()
        if response is None:
            raise ArduinoSerialClientError(
                f"failed to read RAW response, resp_wait_sec = {resp_wait_sec}")

        return response


class DacSpiClient:
    _SYNC = b'\xAB\xCD\xEF'
    _DATA_HEADER = b'\xAB\xCD\xEF\x02'

    def __init__(self, port: str, baudrate: int, init_timeout: int):
        self._connect_board(port, baudrate, init_timeout)

    def _connect_board(self, port: str, baudrate: int, init_timeout: int):
        self._arduino_serial_client = ArduinoSerialClient(
            port=port, baudrate=baudrate, init_timeout=float(init_timeout))

        self._arduino_serial_client.init()

    def send_metadata(self, n_channels, bits_per_sample, sample_rate, packet_size, total_samples, debug=False):
        payload = struct.pack('<BBIHIB19x',
                              n_channels,
                              bits_per_sample,
                              sample_rate,
                              packet_size,
                              total_samples,
                              int(debug))
        raw_data = self._SYNC + b'\x01' + payload
        return self._arduino_serial_client.send_raw_data(raw_data)

    def stream_frame(self, packet_buf: bytearray, raw=False):
        return self._arduino_serial_client.send_raw_data(packet_buf, raw=raw)


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
        frame_size = wf.getsampwidth()  # bytes per sample
        raw_bytes = wf.readframes(n_frames)
        sample_rate = wf.getframerate()

    samples_buffer = np.frombuffer(raw_bytes, dtype='<i2')
    total_samples = len(samples_buffer)
    resolution = frame_size * 8

    return n_channels, total_samples, samples_buffer, resolution, sample_rate


def stream_wav_file(dac_spi: DacSpiClient, wav_file: str, packet_size: int, debug: bool = False):
    n_channels, total_samples, samples_buffer, resolution, sample_rate = _parse_wav_file(
        wav_file)

    if n_channels > 2:
        raise CliError("3+ channels mode is not supported")

    mode = "mono" if n_channels == 1 else "stereo"
    num_packets = (total_samples + packet_size - 1) // packet_size

    print(
        f"streaming | mode: {mode}, total_samples: {total_samples}, resolution: {resolution} bit, sample_rate: {sample_rate} Hz, packet_size: {packet_size}, num_packets: {num_packets}")

    # Send metadata once
    res = dac_spi.send_metadata(n_channels, resolution, sample_rate, packet_size, total_samples, debug=debug)
    print(f"metadata sent, res: {res}")

    # Pre-allocate reusable packet buffer: 4-byte header + payload
    packet_buf = bytearray(DacSpiClient._DATA_HEADER + bytes(packet_size * 2))
    payload_view = np.frombuffer(packet_buf, dtype='<i2', offset=4)

    # Stream data packets
    if debug:
        for i in range(num_packets):
            start = i * packet_size
            end = start + packet_size
            frame = samples_buffer[start:end]

            if len(frame) < packet_size:
                payload_view[len(frame):] = 0
                payload_view[:len(frame)] = frame
            else:
                payload_view[:] = frame

            ts = time.monotonic()
            res = dac_spi.stream_frame(packet_buf)
            elapsed = time.monotonic() - ts
            print(f"packet {i + 1}/{num_packets}: {elapsed:.04f} sec, res: {res}")
    else:
        loop_start = time.monotonic()
        for i in range(num_packets):
            start = i * packet_size
            end = start + packet_size
            frame = samples_buffer[start:end]

            if len(frame) < packet_size:
                payload_view[len(frame):] = 0
                payload_view[:len(frame)] = frame
            else:
                payload_view[:] = frame

            dac_spi.stream_frame(packet_buf, raw=True)

        total_elapsed = time.monotonic() - loop_start
        avg_ms = (total_elapsed / num_packets) * 1000
        print(f"done | {num_packets} packets in {total_elapsed:.03f} sec, avg {avg_ms:.02f} ms/pkt")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="WAV converter")
    parser.add_argument("port", type=str, metavar="<port>",
                        help="Specify the USP port address for the serial connection")
    parser.add_argument("--baudrate", type=int, default=2000000,
                        metavar="<baud>", help="Set the serial connection speed")
    parser.add_argument("--init-timeout", type=int, default=3, metavar="<sec>",
                        help="Set the MAX Arduino's reset-on-connect timeout in seconds")
    parser.add_argument("-s", "--stream", type=str, required=False, metavar="<filename>",
                        help="")
    parser.add_argument("--frame-size", type=int, default=8000,
                        help="")
    parser.add_argument("--debug", action="store_true", default=False,
                        help="Print per-packet timing")
    args = parser.parse_args()

    # connect
    dac_spi = connect_dac_spi(
        args.port, args.baudrate, args.init_timeout)

    if args.stream is not None:
        stream_wav_file(dac_spi, args.stream, args.frame_size, debug=args.debug)
