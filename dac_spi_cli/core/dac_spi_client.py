import struct

from serial_json_rpc.client import SerialJsonRpcClient


class DacSpiClientError(Exception):
    pass


class DacSpiClient:
    _HEADER = b'\xAB\xCD'

    def __init__(self, port: str, baudrate: int, init_timeout: int):
        self._connect_board(port, baudrate, init_timeout)

    def _connect_board(self, port: str, baudrate: int, init_timeout: int):
        self._json_rpc_client = SerialJsonRpcClient(
            port=port, baudrate=baudrate, init_timeout=float(init_timeout))

        _settings = self._json_rpc_client.init()
        # if not _settings:
        #     raise DacSpiClientError("failed to connect DAC SPI, empty settings returned")

    def play_frame(self, left_channel, right_channel):
        resp = self._json_rpc_client.send_request(
            "play_frame", [left_channel, right_channel])
        print(f"play_frame: {resp}")

    def stream_frame(self, n_channels, frame_buffer, frame_buffer_size):
        n_channels = struct.pack('B', n_channels)
        frame_buffer_size = struct.pack('<H', frame_buffer_size)
        payload = b''.join(struct.pack('<h', v) for v in frame_buffer)
        raw_data = self._HEADER + n_channels + frame_buffer_size + payload
        return self._json_rpc_client.send_raw_data(raw_data)
