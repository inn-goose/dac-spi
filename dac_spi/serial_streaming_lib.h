#ifndef __serial_streaming_lib_h__
#define __serial_streaming_lib_h__

namespace SerialStreamingLibrary {

/*
  High-throughput non-blocking serial receiver
  With packet timeout protection

  Packet format:
    [0xAB][0xCD][N_CHANNELS][LEN_LO][LEN_HI][PAYLOAD...]
    PAYLOAD = LEN * int16_t (little-endian)
*/

// Ring Buffer

class RingBuffer {
public:
  RingBuffer()
    : _head(0), _tail(0) {}

  bool available() {
    return _head != _tail;
  }

  void push(uint8_t b) {
    uint16_t next = (_head + 1) & (_BUFFER_SIZE - 1);
    if (next != _tail) {
      _buffer[_head] = b;
      _head = next;
    }
  }

  bool pop(uint8_t *b) {
    if (!available()) return false;
    *b = _buffer[_tail];
    _tail = (_tail + 1) & (_BUFFER_SIZE - 1);
    return true;
  }

private:
  static const uint16_t _BUFFER_SIZE = 32768;  // power of 2
  uint8_t _buffer[_BUFFER_SIZE];
  volatile uint16_t _head;
  volatile uint16_t _tail;
};


// Packet Parser

enum ParserState {
  WAIT_HEADER1,
  WAIT_HEADER2,
  WAIT_NCHANNELS,
  WAIT_LEN_LO,
  WAIT_LEN_HI,
  WAIT_PAYLOAD
};

class StreamingParser {

  // uint8_t n_channels, uint16_t frames_count, int16_t* samples_buffer
  using StreamingProcessor = void (*)(uint8_t, uint16_t, int16_t *);

public:
  StreamingParser(StreamingProcessor streaming_processor)
    : streaming_processor_callback(streaming_processor),
      _ring_buffer(), _state(WAIT_HEADER1), last_byte_read_ts(0),
      _n_channels(0), _samples_count(0), _payload_index(0) {}

  void loop() {
    while (Serial.available()) {
      _ring_buffer.push(Serial.read());
    }
    parse();
  }

  void reset() {
    _state = WAIT_HEADER1;
    _n_channels = 0;
    _samples_count = 0;
    _payload_index = 0;
  }

  void parse() {
    uint8_t b;

    // Timeout check (only when assembling payload)
    if (_state == WAIT_PAYLOAD) {
      if (millis() - last_byte_read_ts > _READ_TIMEOUT_MS) {
        reset();
      }
    }

    while (_ring_buffer.pop(&b)) {
      last_byte_read_ts = millis();

      switch (_state) {
        case WAIT_HEADER1:
          if (b == _HEADER1) _state = WAIT_HEADER2;
          break;

        case WAIT_HEADER2:
          _state = (b == _HEADER2) ? WAIT_NCHANNELS : WAIT_HEADER1;
          break;

        case WAIT_NCHANNELS:
          _n_channels = b;
          if (_n_channels == 0 || _n_channels > _MAX_CHANNELS) {
            reset();
          } else {
            _state = WAIT_LEN_LO;
          }
          break;

        case WAIT_LEN_LO:
          _samples_count = b;
          _state = WAIT_LEN_HI;
          break;

        case WAIT_LEN_HI:
          _samples_count |= ((uint16_t)b << 8);

          if (_samples_count > _SAMPLES_BUFFER_SIZE) {
            reset();
          } else {
            _state = WAIT_PAYLOAD;
            _payload_index = 0;
          }
          break;

        case WAIT_PAYLOAD:
          _payload_bytes[_payload_index++] = b;

          if (_payload_index >= _samples_count * 2) {
            // convert payload to samples in one go
            for (uint16_t i = 0; i < _samples_count; i++) {
              _samples_buffer[i] = (int16_t)(_payload_bytes[2 * i] | (_payload_bytes[2 * i + 1] << 8));
            }
            streaming_processor_callback(_n_channels, _samples_count, _samples_buffer);
            reset();
          }
          break;
      }
    }
  }

private:
  static const int _READ_TIMEOUT_MS = 10;  // adjust for baud rate
  unsigned long long last_byte_read_ts;

  static const uint8_t _MAX_CHANNELS = 2;
  static const uint16_t _SAMPLES_BUFFER_SIZE = 16000;  // ring buffer size - header size

  static const uint8_t _HEADER1 = 0xAB;
  static const uint8_t _HEADER2 = 0xCD;

  RingBuffer _ring_buffer;

  ParserState _state = WAIT_HEADER1;

  uint8_t _n_channels;
  uint16_t _samples_count;
  int16_t _samples_buffer[_SAMPLES_BUFFER_SIZE];

  uint16_t _payload_index;
  uint8_t _payload_bytes[_SAMPLES_BUFFER_SIZE * 2];  // byte * 2 = i16

  StreamingProcessor streaming_processor_callback;
};


}  // SerialStreamingLibrary

#endif  // !__serial_streaming_lib_h__
