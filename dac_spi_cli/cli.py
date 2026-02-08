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
                port=self.port, baudrate=self.baudrate, timeout=self.read_timeout, write_timeout=self.write_timeout)
        except Exception as ex:
            raise ArduinoSerialClientError(
                f"failed to open serial port with {str(ex)}")

    def _read_raw_response(self, read_timeout_sec: float) -> Tuple[Optional[str], float]:
        if self.serial is None:
            raise ArduinoSerialClientError("uninitialized serial protocol")

        start_ts = time.time()
        deadline_ts = start_ts + read_timeout_sec

        buffer = b""
        raw_response = None
        resp_wait_sec = read_timeout_sec

        while time.time() < deadline_ts:
            if self.serial.in_waiting > 0:
                buffer += self.serial.read(self.serial.in_waiting)
                raw_response = buffer.decode()
                resp_wait_sec = time.time() - start_ts
                break
            time.sleep(0.05)

        return raw_response, resp_wait_sec

    def send_raw_data(self, data):
        if self.serial is None:
            raise ArduinoSerialClientError("uninitialized serial protocol")

        w_res = self.serial.write(data)
        if not w_res:
            raise ArduinoSerialClientError(
                "failed to send request, 0 bytes written")

        self.serial.flush()

        response, resp_wait_sec = self._read_raw_response(
            self.RESPONSE_READ_TIMEOUT_SEC)
        if response is None:
            raise ArduinoSerialClientError(
                f"failed to read RAW response, resp_wait_sec = {resp_wait_sec}")

        return response


class DacSpiClient:
    _SYNC = b'\xAB\xCD\xEF'

    def __init__(self, port: str, baudrate: int, init_timeout: int):
        self._connect_board(port, baudrate, init_timeout)

    def _connect_board(self, port: str, baudrate: int, init_timeout: int):
        self._arduino_serial_client = ArduinoSerialClient(
            port=port, baudrate=baudrate, init_timeout=float(init_timeout))

        self._arduino_serial_client.init()

    def send_metadata(self, n_channels, bits_per_sample, sample_rate, packet_size, total_samples):
        payload = struct.pack('<BBIHI',
                              n_channels,
                              bits_per_sample,
                              sample_rate,
                              packet_size,
                              total_samples)
        raw_data = self._SYNC + b'\x01' + payload
        return self._arduino_serial_client.send_raw_data(raw_data)

    def stream_frame(self, frame_buffer_np):
        raw_data = self._SYNC + b'\x02' + frame_buffer_np.tobytes()
        return self._arduino_serial_client.send_raw_data(raw_data)


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


def stream_wav_file(dac_spi: DacSpiClient, wav_file: str, packet_size: int):
    n_channels, total_samples, samples_buffer, resolution, sample_rate = _parse_wav_file(
        wav_file)

    if n_channels > 2:
        raise CliError("3+ channels mode is not supported")

    mode = "mono" if n_channels == 1 else "stereo"
    num_packets = (total_samples + packet_size - 1) // packet_size

    print(
        f"streaming | mode: {mode}, total_samples: {total_samples}, resolution: {resolution} bit, sample_rate: {sample_rate} Hz, packet_size: {packet_size}, num_packets: {num_packets}")

    # Send metadata once
    res = dac_spi.send_metadata(n_channels, resolution, sample_rate, packet_size, total_samples)
    print(f"metadata sent, res: {res}")

    # Stream data packets
    for i in range(num_packets):
        start = i * packet_size
        end = start + packet_size
        frame = samples_buffer[start:end]

        # Zero-pad last packet if needed
        if len(frame) < packet_size:
            frame = np.pad(frame, (0, packet_size - len(frame)), constant_values=0)

        ts = time.time()
        res = dac_spi.stream_frame(frame.astype('<i2'))
        elapsed = time.time() - ts
        print(f"packet {i + 1}/{num_packets}: {elapsed:.04f} sec, res: {res}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="WAV converter")
    parser.add_argument("port", type=str, metavar="<port>",
                        help="Specify the USP port address for the serial connection")
    parser.add_argument("--baudrate", type=int, default=1000000,
                        metavar="<baud>", help="Set the serial connection speed")
    parser.add_argument("--init-timeout", type=int, default=3, metavar="<sec>",
                        help="Set the MAX Arduino's reset-on-connect timeout in seconds")
    parser.add_argument("-s", "--stream", type=str, required=False, metavar="<filename>",
                        help="")
    parser.add_argument("--frame-size", type=int, default=1000,
                        help="")
    args = parser.parse_args()

    # connect
    dac_spi = connect_dac_spi(
        args.port, args.baudrate, args.init_timeout)

    if args.stream is not None:
        stream_wav_file(dac_spi, args.stream, args.frame_size)
